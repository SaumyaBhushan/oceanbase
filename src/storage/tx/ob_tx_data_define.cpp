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

#include "storage/tx/ob_tx_data_define.h"
#include "lib/utility/ob_unify_serialize.h"
#include "storage/tx_table/ob_tx_table.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{

namespace storage
{

int ObUndoStatusList::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  const int64_t len = get_serialize_size_();
  if (OB_UNLIKELY(OB_ISNULL(buf) || buf_len <= 0 || pos > buf_len)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "serialize ObUndoStatusList failed.", KR(ret), KP(buf), K(buf_len),
                K(pos));
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, UNIS_VERSION))) {
    STORAGE_LOG(WARN, "encode UNIS_VERSION of undo status list failed.", KR(ret), KP(buf),
                K(buf_len), K(pos));
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, len))) {
    STORAGE_LOG(WARN, "encode length of undo status list failed.", KR(ret), KP(buf), K(buf_len),
                K(pos));
  } else if (OB_FAIL(serialize_(buf, buf_len, pos))) {
    STORAGE_LOG(WARN, "serialize_ undo status list failed.", KR(ret), KP(buf), K(buf_len), K(pos));
  }
  return ret;
}

int ObUndoStatusList::serialize_(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  ObArray<ObUndoStatusNode *> node_arr;
  node_arr.reset();
  ObUndoStatusNode *node = head_;
  // generate undo status node stack
  while (OB_NOT_NULL(node)) {
    node_arr.push_back(node);
    node = node->next_;
  }

  LST_DO_CODE(OB_UNIS_ENCODE, undo_node_cnt_);
  // pop undo status node to serialize
  for (int i = node_arr.count() - 1; OB_SUCC(ret) && i >= 0; i--) {
    node = node_arr[i];
    for (int i = 0; OB_SUCC(ret) && i < node->size_; i++) {
      LST_DO_CODE(OB_UNIS_ENCODE, node->undo_actions_[i]);
    }
  }

  return ret;
}

int ObUndoStatusList::deserialize(const char *buf,
                                  const int64_t data_len,
                                  int64_t &pos,
                                  ObSliceAlloc &slice_allocator)
{
  int ret = OB_SUCCESS;
  int64_t version = 0;
  int64_t undo_status_list_len = 0;

  if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &version))) {
    STORAGE_LOG(WARN, "decode version fail", K(version), K(data_len), K(pos), K(ret));
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &undo_status_list_len))) {
    STORAGE_LOG(WARN, "decode data len fail", K(undo_status_list_len), K(data_len), K(pos), K(ret));
  } else if (version != UNIS_VERSION) {
    ret = OB_VERSION_NOT_MATCH;
    STORAGE_LOG(WARN, "object version mismatch", K(ret), K(version));
  } else if (OB_UNLIKELY(undo_status_list_len < 0)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "can't decode object with negative length", K(undo_status_list_len));
  } else if (OB_UNLIKELY(data_len < undo_status_list_len + pos)) {
    ret = OB_DESERIALIZE_ERROR;
    STORAGE_LOG(WARN, "buf length not correct", K(undo_status_list_len), K(pos), K(data_len));
  } else {
    int64_t original_pos = pos;
    pos = 0;
    if (OB_FAIL(deserialize_(buf + original_pos, undo_status_list_len, pos, slice_allocator))) {
      STORAGE_LOG(WARN, "deserialize_ fail", "slen", undo_status_list_len, K(pos), K(ret));
    }
    pos += original_pos;
  }

  return ret;
}

int ObUndoStatusList::deserialize_(const char *buf,
                                   const int64_t data_len,
                                   int64_t &pos,
                                   ObSliceAlloc &slice_allocator)
{
  int ret = OB_SUCCESS;
  ObUndoStatusNode *cur_node = nullptr;
  transaction::ObUndoAction action;
  LST_DO_CODE(OB_UNIS_DECODE, undo_node_cnt_);
  while (OB_SUCC(ret) && pos < data_len) {
    LST_DO_CODE(OB_UNIS_DECODE, action);
    // allcate new undo status node if needed
    if (OB_ISNULL(cur_node) || cur_node->size_ >= TX_DATA_UNDO_ACT_MAX_NUM_PER_NODE) {
      void *buf = nullptr;
      if (OB_ISNULL(buf = slice_allocator.alloc())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        STORAGE_LOG(WARN, "allocate memory when deserialize ObTxData failed.", KR(ret));
      } else {
        cur_node = new (buf) ObUndoStatusNode;

        // update undo status list link after allocated new node
        ObUndoStatusNode *tmp_node = head_;
        head_ = cur_node;
        cur_node->next_ = tmp_node;
      }
    }

    cur_node->undo_actions_[cur_node->size_++] = action;
  }

  return ret;
}

int64_t ObUndoStatusList::get_serialize_size() const
{
  int64_t data_len = get_serialize_size_();
  int64_t len = 0;
  len += serialization::encoded_length_vi64(UNIS_VERSION);
  len += serialization::encoded_length_vi64(data_len);
  len += data_len;
  return len;
}

int64_t ObUndoStatusList::get_serialize_size_() const
{
  int64_t len = 0;
  ObUndoStatusNode *node_ptr = head_;
  LST_DO_CODE(OB_UNIS_ADD_LEN, undo_node_cnt_);
  while (OB_NOT_NULL(node_ptr)) {
    for (int i = 0; i < node_ptr->size_; i++) {
      LST_DO_CODE(OB_UNIS_ADD_LEN, node_ptr->undo_actions_[i]);
    }
    node_ptr = node_ptr->next_;
  }
  return len;
}

bool ObUndoStatusList::is_contain(const int64_t seq_no) const
{
  bool bool_ret = false;
  SpinRLockGuard guard(lock_);
  ObUndoStatusNode *node_ptr = head_;
  while (OB_NOT_NULL(node_ptr)) {
    for (int i = 0; i < node_ptr->size_; i++) {
      if (true == node_ptr->undo_actions_[i].is_contain(seq_no)) {
        bool_ret = true;
        break;
      }
    }
    if (bool_ret) break;
    node_ptr = node_ptr->next_;
  }
  return bool_ret;
}

DEF_TO_STRING(ObUndoStatusList)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(KP_(head), K_(undo_node_cnt));
  ObUndoStatusNode *node_ptr = head_;
  while (nullptr != node_ptr) {
    for (int i = 0; i < node_ptr->size_; i++) {
      transaction::ObUndoAction undo_action = node_ptr->undo_actions_[i];
      J_KV(K(undo_action));
    }
    node_ptr = node_ptr->next_;
  }
  J_OBJ_END();
  return pos;
}

void ObUndoStatusList::dump_2_text(FILE *fd) const
{
  if (OB_ISNULL(fd)) {
    return;
  }

  ObUndoStatusNode *node = head_;
  bool has_undo = false;
  if (OB_NOT_NULL(node)) {
    has_undo = true;
    fprintf(fd, "    UNDO_STATUS:{");
  }
  while (OB_NOT_NULL(node)) {
    for (int64_t i = node->size_ - 1; i >= 0; i--) {
      fprintf(fd, "{from:%ld, to:%ld}", node->undo_actions_[i].undo_from_, node->undo_actions_[i].undo_to_);
    }
    node = node->next_;
  }
  if (has_undo) {
    fprintf(fd, "}");
  }
}

void ObTxCommitData::reset()
{
  tx_id_ = INT64_MAX;
  state_ = RUNNING;
  commit_version_.reset();
  start_scn_.reset();
  end_scn_.reset();
  is_in_tx_data_table_ = false;
}

const char* ObTxCommitData::get_state_string(int32_t state)
{
  STATIC_ASSERT(RUNNING == 0, "Invalid State Enum");
  STATIC_ASSERT(COMMIT == 1, "Invalid State Enum");
  STATIC_ASSERT(ELR_COMMIT == 2, "Invalid State Enum");
  STATIC_ASSERT(ABORT == 3, "Invalid State Enum");
  STATIC_ASSERT(MAX_STATE_CNT == 4, "Invalid State Enum");
  const static int cnt = MAX_STATE_CNT;
  const static char STATE_TO_CHAR[cnt][20] = {"RUNNING", "COMMIT", "ELR_COMMIT", "ABORT"};
  return STATE_TO_CHAR[state];
}

int ObTxData::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  const int64_t len = get_serialize_size_();

  if (OB_UNLIKELY(OB_ISNULL(buf) || buf_len <= 0 || pos > buf_len)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "serialize of ObTxDat failed.", KR(ret), KP(buf), K(buf_len), K(pos));
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, UNIS_VERSION))) {
    STORAGE_LOG(WARN, "encode UNIS_VERSION of ObTxData failed.", KR(ret), KP(buf), K(buf_len),
                K(pos));
  } else if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, len))) {
    STORAGE_LOG(WARN, "encode length of ObTxData failed.", KR(ret), KP(buf), K(buf_len), K(pos));
  } else if (OB_FAIL(serialize_(buf, buf_len, pos))) {
    STORAGE_LOG(WARN, "serialize_ of ObTxData failed.", KR(ret), KP(buf), K(buf_len), K(pos));
  }
  return ret;
}

int ObTxData::serialize_(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  // LST_DO_CODE(OB_UNIS_ENCODE, state_, commit_version_, start_scn_, end_scn_);

  if (OB_FAIL(tx_id_.serialize(buf, buf_len, pos))) {
    STORAGE_LOG(WARN, "serialize tx_id fail.", KR(ret), K(pos), K(buf_len));
  } else if (OB_FAIL(serialization::encode_vi32(buf, buf_len, pos, state_))) {
    STORAGE_LOG(WARN, "serialize state fail.", KR(ret), K(pos), K(buf_len));
  } else if (OB_FAIL(commit_version_.serialize(buf, buf_len, pos))) {
    STORAGE_LOG(WARN, "serialize commit_version fail.", KR(ret), K(pos), K(buf_len));
  } else if (OB_FAIL(start_scn_.serialize(buf, buf_len, pos))) {
    STORAGE_LOG(WARN, "serialize start_scn fail.", KR(ret), K(pos), K(buf_len));
  } else if (OB_FAIL(end_scn_.serialize(buf, buf_len, pos))) {
    STORAGE_LOG(WARN, "serialize end_scn fail.", KR(ret), K(pos), K(buf_len));
  } else if (OB_FAIL(undo_status_list_.serialize(buf, buf_len, pos))) {
    STORAGE_LOG(WARN, "serialize undo_status_list fail.", KR(ret), K(pos), K(buf_len));
  }

  return ret;
}

int64_t ObTxData::get_serialize_size() const
{
  int64_t data_len = get_serialize_size_();
  int64_t len = 0;
  len += serialization::encoded_length_vi64(UNIS_VERSION);
  len += serialization::encoded_length_vi64(data_len);
  len += data_len;
  return len;
}

int64_t ObTxData::get_serialize_size_() const
{
  int64_t len = 0;
  // LST_DO_CODE(OB_UNIS_ADD_LEN, state_, commit_version_, start_scn_, end_scn_);
  len += tx_id_.get_serialize_size();
  len += serialization::encoded_length_vi32(state_);
  len += commit_version_.get_serialize_size();
  len += start_scn_.get_serialize_size();
  len += end_scn_.get_serialize_size();
  len += undo_status_list_.get_serialize_size();
  return len;
}

int ObTxData::deserialize(const char *buf,
                          const int64_t data_len,
                          int64_t &pos,
                          ObSliceAlloc &slice_allocator)
{
  int ret = OB_SUCCESS;
  int64_t version = 0;
  int64_t len = 0;

  if (OB_UNLIKELY(nullptr == buf || data_len <= 0 || pos > data_len)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid arguments.", KP(buf), K(data_len), K(ret));
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &version))) {
    STORAGE_LOG(WARN, "deserialize version of tx data failed.", KR(ret), K(version));
  } else if (version != UNIS_VERSION) {
    ret = OB_VERSION_NOT_MATCH;
    STORAGE_LOG(WARN, "deserialize version of tx data failed.", KR(ret), K(version));
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &len))) {
    STORAGE_LOG(WARN, "length from deserialize is invalid.", KR(ret), K(pos), K(len), K(data_len));
  } else if (OB_UNLIKELY(pos + len > data_len)) {
    ret = OB_INVALID_SIZE;
    STORAGE_LOG(WARN, "length from deserialize is invalid.", KR(ret), K(pos), K(len), K(data_len));
  } else if (OB_FAIL(deserialize_(buf, data_len, pos, slice_allocator))) {
    STORAGE_LOG(WARN, "deserialize tx data failed.", KR(ret));
  }

  return ret;
}

int ObTxData::deserialize_(const char *buf,
                           const int64_t data_len,
                           int64_t &pos,
                           ObSliceAlloc &slice_allocator)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(tx_id_.deserialize(buf, data_len, pos))) {
    STORAGE_LOG(WARN, "deserialize tx_id fail.", KR(ret), K(pos), K(data_len));
  } else if (OB_FAIL(serialization::decode_vi32(buf, data_len, pos, &state_))) {
    STORAGE_LOG(WARN, "deserialize state fail.", KR(ret), K(pos), K(data_len));
  } else if (OB_FAIL(commit_version_.deserialize(buf, data_len, pos))) {
    STORAGE_LOG(WARN, "deserialize commit_version fail.", KR(ret), K(pos), K(data_len));
  } else if (OB_FAIL(start_scn_.deserialize(buf, data_len, pos))) {
    STORAGE_LOG(WARN, "deserialize start_scn fail.", KR(ret), K(pos), K(data_len));
  } else if (OB_FAIL(end_scn_.deserialize(buf, data_len, pos))) {
    STORAGE_LOG(WARN, "deserialize end_scn fail.", KR(ret), K(pos), K(data_len));
  } else if (OB_FAIL(undo_status_list_.deserialize(buf, data_len, pos, slice_allocator))) {
    STORAGE_LOG(WARN, "deserialize undo_status_list fail.", KR(ret), K(pos), K(data_len));
  }

  return ret;
}

void ObTxData::reset()
{
  ObTxCommitData::reset();
  undo_status_list_.reset();
}

ObTxData::ObTxData(const ObTxData &rhs)
{
  *this = rhs;
}

ObTxData &ObTxData::operator=(const ObTxData &rhs)
{
  tx_id_ = rhs.tx_id_;
  state_ = rhs.state_;
  commit_version_ = rhs.commit_version_;
  start_scn_ = rhs.start_scn_;
  end_scn_ = rhs.end_scn_;
  undo_status_list_ = rhs.undo_status_list_;
  is_in_tx_data_table_ = rhs.is_in_tx_data_table_;
  return *this;
}

ObTxData &ObTxData::operator=(const ObTxCommitData &rhs)
{
  tx_id_ = rhs.tx_id_;
  state_ = rhs.state_;
  commit_version_ = rhs.commit_version_;
  start_scn_ = rhs.start_scn_;
  end_scn_ = rhs.end_scn_;
  is_in_tx_data_table_ = rhs.is_in_tx_data_table_;
  undo_status_list_.reset();
  return *this;
}

bool ObTxData::is_valid_in_tx_data_table() const
{
  bool bool_ret = true;

  if (ObTxData::RUNNING == state_) {
    if (!end_scn_.is_valid()) {
      bool_ret = false;
      STORAGE_LOG(ERROR, "tx data end log ts is invalid", KPC(this));
    } else if (OB_ISNULL(undo_status_list_.head_)) {
      bool_ret = false;
      STORAGE_LOG(ERROR, "tx data undo status list is invalid", KPC(this));
    } else {
      bool_ret = true;
    }
  } else if (state_ < 0 || state_ >= MAX_STATE_CNT) {
    bool_ret = false;
    STORAGE_LOG(ERROR, "tx data state is invalid", KPC(this));
  } else if (!start_scn_.is_valid()) {
    bool_ret = false;
    STORAGE_LOG(ERROR, "tx data start_scn is invalid", KPC(this));
  } else if (!end_scn_.is_valid()) {
    bool_ret = false;
    STORAGE_LOG(ERROR, "tx data end_scn is invalid", KPC(this));
  } else if (end_scn_ < start_scn_) {
    bool_ret = false;
    STORAGE_LOG(ERROR, "tx data end_scn is less than start_scn", KPC(this));
  } else if (!commit_version_.is_valid() && state_ != RUNNING && state_ != ABORT) {
    bool_ret = false;
    STORAGE_LOG(ERROR, "tx data commit_version is invalid but state is not running or abort",
                KPC(this));
  }

  return bool_ret;
}

int ObTxData::add_undo_action(ObTxTable *tx_table, transaction::ObUndoAction &new_undo_action, ObUndoStatusNode *undo_node)
{
  // STORAGE_LOG(DEBUG, "do add_undo_action");
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(undo_status_list_.lock_);
  ObTxDataTable *tx_data_table = nullptr;
  ObUndoStatusNode *node = undo_status_list_.head_;
  if (OB_ISNULL(tx_table)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "tx table is nullptr.", KR(ret));
  } else if (OB_ISNULL(tx_data_table = tx_table->get_tx_data_table())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "tx data table in tx table is nullptr.", KR(ret));
  } else {
    merge_undo_actions_(tx_data_table, node, new_undo_action);
    // generate new node if current node cannot be inserted
    if (OB_ISNULL(node) || node->size_ >= TX_DATA_UNDO_ACT_MAX_NUM_PER_NODE) {
      // STORAGE_LOG(DEBUG, "generate new undo status node");
      ObUndoStatusNode *new_node = nullptr;
      if (OB_NOT_NULL(undo_node)) {
        new_node = undo_node;
        undo_node = NULL;
      } else if (OB_FAIL(tx_data_table->alloc_undo_status_node(new_node))) {
        STORAGE_LOG(WARN, "alloc_undo_status_node() fail", KR(ret));
      }
      if (OB_SUCC(ret)) {
        new_node->next_ = node;
        undo_status_list_.head_ = new_node;
        node = new_node;
        undo_status_list_.undo_node_cnt_++;
      }
    }

    node->undo_actions_[node->size_++] = new_undo_action;
    if (undo_status_list_.undo_node_cnt_ * TX_DATA_SLICE_SIZE > OB_MAX_TX_SERIALIZE_SIZE) {
      ret = OB_SIZE_OVERFLOW;
      STORAGE_LOG(WARN, "Too many undo actions. The size of tx data is overflow.", KR(ret),
                  K(undo_status_list_.undo_node_cnt_), KPC(this));
    }
  }

  if (OB_NOT_NULL(undo_node)) {
    tx_data_table->free_undo_status_node(undo_node);
  }

  return ret;
}

int ObTxData::merge_undo_actions_(ObTxDataTable *tx_data_table,
                                   ObUndoStatusNode *&node,
                                   transaction::ObUndoAction &new_undo_action)
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret) && OB_NOT_NULL(node)) {
    for (int i = node->size_ - 1; i >= 0; i--) {
      if (new_undo_action.is_contain(node->undo_actions_[i])
          || node->undo_actions_[i].is_contain(new_undo_action)) {
        new_undo_action.merge(node->undo_actions_[i]);
        node->size_--;
      } else {
        break;
      }
    }

    if (0 == node->size_) {
      // all undo actions in this node are merged, free it
      // STORAGE_LOG(DEBUG, "current node is empty, now free it");
      ObUndoStatusNode *node_to_free = node;
      undo_status_list_.head_ = node->next_;
      node = undo_status_list_.head_;
      tx_data_table->free_undo_status_node(node_to_free);
      if (undo_status_list_.undo_node_cnt_ <= 0) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(ERROR, "invalid undo node count int undo status list.", KR(ret),
                    K(undo_status_list_));
      } else {
        undo_status_list_.undo_node_cnt_--;
      }
    } else {
      // merge undo actions done
      break;
    }
  }

  return ret;
}
  

bool ObTxData::equals_(ObTxData &rhs)
{
  bool bool_ret = true;
  if (tx_id_ != rhs.tx_id_) {
    bool_ret = false;
    STORAGE_LOG(INFO, "tx_id is not equal.");
  } else if (state_ != rhs.state_) {
    bool_ret = false;
    STORAGE_LOG(INFO, "state is not equal.");
  } else if (commit_version_ != rhs.commit_version_) {
    bool_ret = false;
    STORAGE_LOG(INFO, "commit_version is not equal.");
  } else if (start_scn_ != rhs.start_scn_) {
    bool_ret = false;
    STORAGE_LOG(INFO, "start_scn is not equal.");
  } else if (end_scn_ != rhs.end_scn_) {
    bool_ret = false;
    STORAGE_LOG(INFO, "end_scn is not equal.");
  } else if (undo_status_list_.undo_node_cnt_ != rhs.undo_status_list_.undo_node_cnt_) {
    bool_ret = false;
    STORAGE_LOG(INFO, "undo_node_cnt is not equal.");
  } else {
    ObUndoStatusNode *l_node = undo_status_list_.head_;
    ObUndoStatusNode *r_node = rhs.undo_status_list_.head_;

    while ((nullptr != l_node) && (nullptr != r_node)) {
      if (l_node->size_ != r_node->size_) {
        bool_ret = false;
        break;
      }
      for (int i = 0; i < l_node->size_; i++) {
        if ((l_node->undo_actions_[i].undo_from_ != r_node->undo_actions_[i].undo_from_)
            || (l_node->undo_actions_[i].undo_to_ != r_node->undo_actions_[i].undo_to_)) {
          bool_ret = false;
          break;
        }
      }

      if (false == bool_ret) {
        break;
      } else {
        l_node = l_node->next_;
        r_node = r_node->next_;
      }
    }

    if (true == bool_ret) {
      if (nullptr != l_node || nullptr != r_node) {
        bool_ret = false;
      }
    }
    if (false == bool_ret) {
      STORAGE_LOG(INFO, "undo status is not equal.");
    }
  }

  return bool_ret;
}


void ObTxData::print_to_stderr(const ObTxData &tx_data)
{
  fprintf(stderr,
          "TX_DATA:{tx_id=%-20ld in_tx_data_table=%-6s start_log_scn=%-20s end_log_scn=%-20s commit_version=%-20s "
          "state=%s",
          tx_data.tx_id_.get_id(),
          tx_data.is_in_tx_data_table_ ? "True" : "False",
          to_cstring(tx_data.start_scn_),
          to_cstring(tx_data.end_scn_),
          to_cstring(tx_data.commit_version_),
          get_state_string(tx_data.state_));

  tx_data.undo_status_list_.dump_2_text(stderr);
}

void ObTxData::dump_2_text(FILE *fd) const
{
  if (OB_ISNULL(fd)) {
    return;
  }

  fprintf(fd,
          "TX_DATA:\n{\n    tx_id=%-20ld\n    in_tx_data_table=%-6s\n    start_log_scn=%-20s\n    end_log_scn=%-20s\n  "
          "  commit_version=%-20s\n    state=%s\n",
          tx_id_.get_id(),
          is_in_tx_data_table_ ? "True" : "False",
          to_cstring(start_scn_),
          to_cstring(end_scn_),
          to_cstring(commit_version_),
          get_state_string(state_));

  undo_status_list_.dump_2_text(fd);

  fprintf(fd, "\n}\n");
}

DEF_TO_STRING(ObTxData)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(tx_id),
       "state", get_state_string(state_),
       "in_tx_data_table", is_in_tx_data_table_ ? "True" : "False",
       K_(commit_version),
       K_(start_scn),
       K_(end_scn),
       K_(undo_status_list));
  J_OBJ_END();
  return pos;
}

TxDataHashNode *TxDataHashMapAllocHandle::alloc_node(ObTxData *tx_data)
{
  void *hash_node_ptr = reinterpret_cast<void *>(ObTxData::get_hash_node_by_tx_data(tx_data));
  return new (hash_node_ptr) TxDataHashNode();
}
void TxDataHashMapAllocHandle::free_node(TxDataHashNode *node)
{
  if (nullptr != node) {
    // free undo status node first
    ObTxData *tx_data = ObTxData::get_tx_data_by_hash_node(node);
    free_undo_list_(tx_data->undo_status_list_.head_);
    tx_data->undo_status_list_.head_ = nullptr;

    // free slice memory
    void *slice_ptr = reinterpret_cast<void *>(node);
    slice_allocator_->free(slice_ptr);
  }
}

void TxDataHashMapAllocHandle::free_undo_list_(ObUndoStatusNode *node_ptr)
{
  ObUndoStatusNode *node_to_free = nullptr;
  while (nullptr != node_ptr) {
    node_to_free = node_ptr;
    node_ptr = node_ptr->next_;
    slice_allocator_->free(reinterpret_cast<void *>(node_to_free));
  }
}

DEF_TO_STRING(ObUndoStatusNode)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K(size_), KP(next_));
  J_OBJ_END();
  return pos;
}

void ObTxDataMemtableWriteGuard::reset()
{
  for (int i = 0; i < MAX_TX_DATA_MEMTABLE_CNT; i++) {
    if (handles_[i].is_valid()) {
      ObTxDataMemtable *tx_data_memtable = nullptr;
      handles_[i].get_tx_data_memtable(tx_data_memtable);
      tx_data_memtable->dec_write_ref();
    }
    handles_[i].reset();
  }
  size_ = 0;
}

}  // namespace storage

}  // namespace oceanbase
