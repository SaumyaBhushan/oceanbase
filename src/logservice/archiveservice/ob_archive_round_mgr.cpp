/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "ob_archive_round_mgr.h"
#include "lib/atomic/ob_atomic.h"   // ATOMIC*
#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "ob_archive_define.h"      // *ID
#include "observer/ob_server_event_history_table_operator.h"   // SERVER_EVENT

namespace oceanbase
{
namespace archive
{
using namespace oceanbase::common;
using namespace oceanbase::share;

ObArchiveRoundMgr::ObArchiveRoundMgr() :
  key_(),
  round_start_scn_(),
  compatible_(false),
  log_archive_state_(),
  backup_dest_(),
  rwlock_(common::ObLatchIds::ARCHIVE_ROUND_MGR_LOCK)
{
}

ObArchiveRoundMgr::~ObArchiveRoundMgr()
{
  destroy();
}

int ObArchiveRoundMgr::init()
{
  ARCHIVE_LOG(INFO, "ObArchiveRoundMgr init succ");
  return OB_SUCCESS;
}

void ObArchiveRoundMgr::destroy()
{
  WLockGuard guard(rwlock_);

  key_.reset();
  round_start_scn_.reset();
  compatible_ = false;
  log_archive_state_.status_ = ObArchiveRoundState::Status::INVALID;
  backup_dest_.reset();
}


int ObArchiveRoundMgr::set_archive_start(const ArchiveKey &key,
    const share::ObTenantLogArchiveStatus::COMPATIBLE compatible,
    const share::ObBackupDest &dest)
{
  int ret = OB_SUCCESS;
  WLockGuard guard(rwlock_);

  if (OB_UNLIKELY(!key.is_valid() || !dest.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    ARCHIVE_LOG(WARN, "invalid arguments", K(ret), K(key), K(compatible), K(dest));
  } else if (share::ObTenantLogArchiveStatus::COMPATIBLE::NONE != compatible
      && share::ObTenantLogArchiveStatus::COMPATIBLE::COMPATIBLE_VERSION_1 != compatible
      && share::ObTenantLogArchiveStatus::COMPATIBLE::COMPATIBLE_VERSION_2 != compatible) {
    ret = OB_NOT_SUPPORTED;
    ARCHIVE_LOG(ERROR, "compatible not support", K(ret), K(key), K(compatible));
  } else if (OB_FAIL(backup_dest_.deep_copy(dest))) {
    ARCHIVE_LOG(WARN, "backup dest set failed", K(ret), K(dest));
  } else {
    key_ = key;
    compatible_ = compatible >= share::ObTenantLogArchiveStatus::COMPATIBLE::COMPATIBLE_VERSION_2;
    log_archive_state_.status_ = ObArchiveRoundState::Status::DOING;
    ARCHIVE_LOG(INFO, "set_archive_start succ", KPC(this));
  }

  return ret;
}

void ObArchiveRoundMgr::set_archive_force_stop(const ArchiveKey &key)
{
  WLockGuard guard(rwlock_);

  if (OB_UNLIKELY(! key.is_valid())) {
    ARCHIVE_LOG(WARN, "invalid arguments", K(key));
  } else {
    key_ = key;
    log_archive_state_.status_ = ObArchiveRoundState::Status::STOP;
  }
}

void ObArchiveRoundMgr::set_archive_interrupt(const ArchiveKey &key)
{
  WLockGuard guard(rwlock_);

  if (OB_UNLIKELY(! key.is_valid())) {
    ARCHIVE_LOG(WARN, "invalid arguments", K(key));
  } else {
    key_ = key;
    log_archive_state_.status_ = ObArchiveRoundState::Status::INTERRUPTED;
  }
}

int ObArchiveRoundMgr::get_backup_dest(const ArchiveKey &key,
    share::ObBackupDest &dest)
{
  int ret = OB_SUCCESS;
  RLockGuard guard(rwlock_);
  if (key != key_) {
    ret = OB_EAGAIN;
  } else if (OB_FAIL(dest.deep_copy(backup_dest_))) {
    ARCHIVE_LOG(WARN, "backup dest deep_copy failed", K(ret), K(backup_dest_));
  }
  return ret;
}

void ObArchiveRoundMgr::get_archive_round_info(ArchiveKey &key,
    ObArchiveRoundState &state) const
{
  RLockGuard guard(rwlock_);
  key = key_;
  state = log_archive_state_;
}

void ObArchiveRoundMgr::get_archive_round_compatible(ArchiveKey &key,
    bool &compatible)
{
  RLockGuard guard(rwlock_);
  key = key_;
  compatible = compatible_;
}

bool ObArchiveRoundMgr::is_in_archive_status(const ArchiveKey &key) const
{
  RLockGuard guard(rwlock_);
  return key == key_ && log_archive_state_.is_doing();
}

bool ObArchiveRoundMgr::is_in_interrupt_status(const ArchiveKey &key) const
{
  RLockGuard guard(rwlock_);
  return key == key_ && log_archive_state_.is_interrupted();
}

bool ObArchiveRoundMgr::is_in_archive_stopping_status(const ArchiveKey &key) const
{
  RLockGuard guard(rwlock_);
  return key == key_ && log_archive_state_.is_stopping();
}

bool ObArchiveRoundMgr::is_in_archive_stop_status(const ArchiveKey &key) const
{
  RLockGuard guard(rwlock_);
  return key == key_ && log_archive_state_.is_stop();
}

void ObArchiveRoundMgr::update_log_archive_status(const ObArchiveRoundState::Status status)
{
  WLockGuard guard(rwlock_);
  log_archive_state_.status_ = status;
}

int ObArchiveRoundMgr::mark_fata_error(const ObLSID &id,
    const ArchiveKey &key,
    const ObArchiveInterruptReason &reason)
{
  int ret = OB_SUCCESS;
  WLockGuard guard(rwlock_);
  if (key != key_) {
    ARCHIVE_LOG(WARN, "encount error with different archive key, just skip", K(key), KPC(this));
  } else {
    ARCHIVE_LOG(ERROR, "archive mark_fata_error", K(key), K(id), K(reason));
    if (!log_archive_state_.is_interrupted()) {
      log_archive_state_.set_interrupted();
      SERVER_EVENT_ADD("log_archive", "mark_fata_error",
                       "id", id.id(),
                       "reason", reason.get_str(),
                       "ret_code", reason.get_code(),
                       "lbt", reason.get_lbt());
    }
  }
  return ret;
}

} // namespace archive
} // namespace oceanbase
