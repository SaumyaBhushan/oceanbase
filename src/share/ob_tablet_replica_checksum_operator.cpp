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

#define USING_LOG_PREFIX SHARE

#include "share/ob_tablet_replica_checksum_operator.h"
#include "share/tablet/ob_tablet_to_ls_operator.h"
#include "share/schema/ob_part_mgr_util.h"
#include "share/schema/ob_table_param.h"
#include "lib/mysqlclient/ob_mysql_transaction.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/schema/ob_column_schema.h"
#include "share/tablet/ob_tablet_info.h"
#include "share/config/ob_server_config.h"

namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;

ObTabletReplicaReportColumnMeta::ObTabletReplicaReportColumnMeta()
  : compat_version_(0),
    checksum_method_(0),
    checksum_bytes_(0),
    column_checksums_(),
    is_inited_(false)
{}

ObTabletReplicaReportColumnMeta::~ObTabletReplicaReportColumnMeta()
{
  reset();
}

void ObTabletReplicaReportColumnMeta::reset()
{
  is_inited_ = false;
  compat_version_ = 0;
  checksum_method_ = 0;
  checksum_bytes_ = 0;
  column_checksums_.reset();
}

bool ObTabletReplicaReportColumnMeta::is_valid() const
{
  return is_inited_ && column_checksums_.count() > 0;
}

int ObTabletReplicaReportColumnMeta::init(const ObIArray<int64_t> &column_checksums)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTabletReplicaReportColumnMeta inited twice", KR(ret), K(*this));
  } else if (column_checksums.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_FAIL(column_checksums_.assign(column_checksums))) {
    LOG_WARN("fail to assign column_checksums", KR(ret));
  } else {
    checksum_bytes_ = (sizeof(int16_t) + sizeof(int64_t) + sizeof(int8_t)) * 2;
    checksum_method_ = 0; // TODO
    is_inited_ = true;
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::assign(const ObTabletReplicaReportColumnMeta &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    if (other.column_checksums_.empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret));
    } else if (OB_FAIL(column_checksums_.assign(other.column_checksums_))) {
      LOG_WARN("fail to assign column_checksums", KR(ret));
    } else {
      compat_version_ = other.compat_version_;
      checksum_method_ = other.checksum_method_;
      checksum_bytes_ = other.checksum_bytes_;
      is_inited_ = true;
    }
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  int64_t serialize_size = get_serialize_size();
  if (OB_UNLIKELY(NULL == buf) || (serialize_size > buf_len)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments.", KP(buf), KR(ret), K(serialize_size), K(buf_len));
  } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, MAGIC_NUMBER))) {
    LOG_WARN("fail to encode magic number", KR(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, compat_version_))) {
    LOG_WARN("fail to encode compat version", KR(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, checksum_method_))) {
    LOG_WARN("fail to encode checksum method", KR(ret));
  } else if (OB_FAIL(serialization::encode_i8(buf, buf_len, pos, checksum_bytes_))) {
    LOG_WARN("fail to encode checksum bytes", KR(ret));
  } else if (OB_FAIL(column_checksums_.serialize(buf, buf_len, pos))) {
    LOG_WARN("fail to serialize column_checksums", KR(ret));
  }
  return ret;
}

int64_t ObTabletReplicaReportColumnMeta::get_serialize_size() const
{
  int64_t len = 0;
  len += serialization::encoded_length_i64(MAGIC_NUMBER);
  len += serialization::encoded_length_i8(compat_version_);
  len += serialization::encoded_length_i8(checksum_method_);
  len += serialization::encoded_length_i8(checksum_bytes_);
  len += column_checksums_.get_serialize_size();
  return len;
}

int ObTabletReplicaReportColumnMeta::deserialize(const char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t magic_number = 0;
  if (OB_ISNULL(buf) || (buf_len < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", KR(ret), K(buf), K(buf_len));
  } else if (OB_FAIL(serialization::decode_i64(buf, buf_len, pos, &magic_number))) {
    LOG_WARN("fail to encode magic number", KR(ret));
  } else if (OB_FAIL(serialization::decode_i8(buf, buf_len, pos, &compat_version_))) {
    LOG_WARN("fail to deserialize compat version", KR(ret));
  } else if (OB_FAIL(serialization::decode_i8(buf, buf_len, pos, &checksum_method_))) {
    LOG_WARN("fail to deserialize checksum method", KR(ret));
  } else if (OB_FAIL(serialization::decode_i8(buf, buf_len, pos, &checksum_bytes_))) {
    LOG_WARN("fail to deserialize checksum bytes", KR(ret));
  } else if (OB_FAIL(column_checksums_.deserialize(buf, buf_len, pos))) {
    LOG_WARN("fail to deserialize column checksums", KR(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int64_t ObTabletReplicaReportColumnMeta::get_string_length() const
{
  int64_t len = 0;
  len += sizeof("magic:%lX,");
  len += sizeof("compat:%d,");
  len += sizeof("method:%d,");
  len += sizeof("bytes:%d,");
  len += sizeof("colcnt:%d,");
  len += sizeof("%d:%ld,") * column_checksums_.count();
  len += get_serialize_size();
  return len;
}

int64_t ObTabletReplicaReportColumnMeta::get_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  int32_t column_cnt = column_checksums_.count();
  common::databuff_printf(buf, buf_len, pos, "magic:%lX,", MAGIC_NUMBER);
  common::databuff_printf(buf, buf_len, pos, "compat:%d,", compat_version_);
  common::databuff_printf(buf, buf_len, pos, "method:%d,", checksum_method_);
  common::databuff_printf(buf, buf_len, pos, "bytes:%d,", checksum_bytes_);
  common::databuff_printf(buf, buf_len, pos, "colcnt:%d,", column_cnt);

  for (int32_t i = 0; i < column_cnt; ++i) {
    if (column_cnt - 1 != i) {
      common::databuff_printf(buf, buf_len, pos, "%d:%ld,", i, column_checksums_.at(i));
    } else {
      common::databuff_printf(buf, buf_len, pos, "%d:%ld", i, column_checksums_.at(i));
    }
  }
  return pos;
}

int ObTabletReplicaReportColumnMeta::check_checksum(
    const ObTabletReplicaReportColumnMeta &other,
    const int64_t pos, bool &is_equal) const
{
  int ret = OB_SUCCESS;
  is_equal = true;
  const int64_t col_ckm_cnt = column_checksums_.count();
  const int64_t other_col_ckm_cnt = other.column_checksums_.count();
  if ((pos < 0) || (pos > col_ckm_cnt) || (pos > other_col_ckm_cnt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", KR(ret), K(pos), K(col_ckm_cnt), K(other_col_ckm_cnt),
      K(column_checksums_), K(other.column_checksums_));
  } else if (column_checksums_.at(pos) != other.column_checksums_.at(pos)) {
    is_equal = false;
    LOG_WARN("column checksum is not equal!", K(pos), "col_ckm", column_checksums_.at(pos),
      "other_col_ckm", other.column_checksums_.at(pos), K(col_ckm_cnt), K(other_col_ckm_cnt),
      K(column_checksums_), K(other.column_checksums_));
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::check_all_checksums(
    const ObTabletReplicaReportColumnMeta &other, 
    bool &is_equal) const
{
  int ret = OB_SUCCESS;
  is_equal = true;
  if (column_checksums_.count() != other.column_checksums_.count()) {
    is_equal = false;
    LOG_WARN("column cnt is not equal!", "cur_cnt", column_checksums_.count(),
      "other_cnt", other.column_checksums_.count(), K(*this), K(other));
  } else {
    const int64_t column_ckm_cnt = column_checksums_.count();
    for (int64_t i = 0; OB_SUCC(ret) && is_equal && (i < column_ckm_cnt); ++i) {
      if (OB_FAIL(check_checksum(other, i, is_equal))) {
        LOG_WARN("fail to check checksum", KR(ret), K(i), K(column_ckm_cnt));
      }
    }
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::check_equal(
    const ObTabletReplicaReportColumnMeta &other, 
    bool &is_equal) const
{
  int ret = OB_SUCCESS;
  is_equal = true;
  if (compat_version_ != other.compat_version_) {
    is_equal = false;
    LOG_WARN("compat version is not equal !", K(*this), K(other));
  } else if (checksum_method_ != other.checksum_method_) {
    is_equal = false;
    LOG_WARN("checksum method is different !", K(*this), K(other));
  } else if (OB_FAIL(check_all_checksums(other, is_equal))) {
    LOG_WARN("fail to check all checksum", KR(ret), K(*this), K(other));
  }
  return ret;
}

int ObTabletReplicaReportColumnMeta::get_column_checksum(const int64_t pos, int64_t &checksum) const
{
  int ret = OB_SUCCESS;
  checksum = -1;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletReplicaReportColumnMeta is not inited", KR(ret));
  } else if (pos < 0 || pos >= column_checksums_.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", KR(ret), K(pos), K(column_checksums_));
  } else {
    checksum = column_checksums_.at(pos);
  }
  return ret;
}


/****************************** ObTabletReplicaChecksumItem ******************************/

ObTabletReplicaChecksumItem::ObTabletReplicaChecksumItem()
  : tenant_id_(OB_INVALID_ID),
    ls_id_(),
    tablet_id_(),
    server_(),
    row_count_(0),
    compaction_scn_(),
    data_checksum_(0),
    column_meta_()
{}

void ObTabletReplicaChecksumItem::reset()
{
  tenant_id_ = OB_INVALID_ID;
  ls_id_.reset();
  tablet_id_.reset();
  server_.reset();
  row_count_ = 0;
  compaction_scn_.reset();
  data_checksum_ = 0;
  column_meta_.reset();
}

bool ObTabletReplicaChecksumItem::is_key_valid() const
{
  return OB_INVALID_ID != tenant_id_
      && ls_id_.is_valid_with_tenant(tenant_id_)
      && tablet_id_.is_valid_with_tenant(tenant_id_)
      && server_.is_valid();
}

bool ObTabletReplicaChecksumItem::is_valid() const
{
  return is_key_valid() && column_meta_.is_valid();
}

bool ObTabletReplicaChecksumItem::is_same_tablet(const ObTabletReplicaChecksumItem &other) const
{
  return is_key_valid()
      && other.is_key_valid()
      && tenant_id_ == other.tenant_id_
      && ls_id_ == other.ls_id_
      && tablet_id_ == other.tablet_id_;
}

int ObTabletReplicaChecksumItem::verify_checksum(const ObTabletReplicaChecksumItem &other) const
{
  int ret = OB_SUCCESS;
  if (compaction_scn_ == other.compaction_scn_) {
    bool column_meta_equal = false;
    if (OB_FAIL(column_meta_.check_equal(other.column_meta_, column_meta_equal))) {
      LOG_WARN("fail to check column meta equal", KR(ret), K(other), K(*this));
    } else if (!column_meta_equal) {
      ret = OB_CHECKSUM_ERROR;
    } else if ((row_count_ != other.row_count_) || (data_checksum_ != other.data_checksum_)) {
      ret = OB_CHECKSUM_ERROR;
    }
  } else {
    LOG_INFO("no need to check data checksum", K(other), K(*this));
  }
  return ret;
}

int ObTabletReplicaChecksumItem::assign_key(const ObTabletReplicaChecksumItem &other)
{
  int ret = OB_SUCCESS;
  if (!other.is_key_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(other));
  } else {
    tenant_id_ = other.tenant_id_;
    tablet_id_ = other.tablet_id_;
    ls_id_ = other.ls_id_;
    server_ = other.server_;
  }
  return ret;
}

int ObTabletReplicaChecksumItem::assign(const ObTabletReplicaChecksumItem &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    reset();
    if (OB_FAIL(column_meta_.assign(other.column_meta_))) {
      LOG_WARN("fail to assign column meta", KR(ret), K(other));
    } else {
      tenant_id_ = other.tenant_id_;
      tablet_id_ = other.tablet_id_;
      ls_id_ = other.ls_id_;
      server_ = other.server_;
      row_count_ = other.row_count_;
      compaction_scn_ = other.compaction_scn_;
      data_checksum_ = other.data_checksum_;
    }
  }
  return ret;
}

ObTabletReplicaChecksumItem &ObTabletReplicaChecksumItem::operator=(const ObTabletReplicaChecksumItem &other)
{
  assign(other);
  return *this;
}

/****************************** ObTabletReplicaChecksumOperator ******************************/

int ObTabletReplicaChecksumOperator::batch_remove_with_trans(
    ObMySQLTransaction &trans,
    const uint64_t tenant_id,
    const common::ObIArray<share::ObTabletReplica> &tablet_replicas)
{
  int ret = OB_SUCCESS;
  const int64_t replicas_count = tablet_replicas.count();
  if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id || replicas_count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), "tablet_replica cnt", replicas_count);
  } else {
    int64_t start_idx = 0;
    int64_t end_idx = min(MAX_BATCH_COUNT, replicas_count);
    while (OB_SUCC(ret) && (start_idx < end_idx)) {
      if (OB_FAIL(inner_batch_remove_by_sql_(tenant_id, tablet_replicas, start_idx, end_idx, trans))) {
        LOG_WARN("fail to inner batch remove", KR(ret), K(tenant_id), K(start_idx), K(end_idx));
      } else {
        start_idx = end_idx;
        end_idx = min(start_idx + MAX_BATCH_COUNT, replicas_count);
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::inner_batch_remove_by_sql_(
    const uint64_t tenant_id,
    const common::ObIArray<share::ObTabletReplica> &tablet_replicas,
    const int64_t start_idx,
    const int64_t end_idx,
    ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id)
      || (tablet_replicas.count() <= 0)
      || (start_idx < 0)
      || (start_idx > end_idx)
      || (end_idx > tablet_replicas.count()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(start_idx), K(end_idx),
             "tablet_replica cnt", tablet_replicas.count());
  } else {
    ObSqlString sql;
    int64_t affected_rows = 0;
    if (OB_FAIL(sql.assign_fmt("DELETE FROM %s WHERE tenant_id = '%lu' AND (tablet_id, svr_ip, svr_port, ls_id) IN(",
                OB_ALL_TABLET_REPLICA_CHECKSUM_TNAME, tenant_id))) {
      LOG_WARN("fail to assign sql", KR(ret), K(tenant_id));
    } else {
      char ip[OB_MAX_SERVER_ADDR_SIZE] = "";
      for (int64_t idx = start_idx; OB_SUCC(ret) && (idx < end_idx); ++idx) {
        if (OB_UNLIKELY(!tablet_replicas.at(idx).get_server().ip_to_string(ip, sizeof(ip)))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("convert server ip to string failed", KR(ret), "server", tablet_replicas.at(idx).get_server());
        } else if (OB_FAIL(sql.append_fmt("('%lu', '%s', %d, %ld)%s",
            tablet_replicas.at(idx).get_tablet_id().id(),
            ip,
            tablet_replicas.at(idx).get_server().get_port(),
            tablet_replicas.at(idx).get_ls_id().id(),
            ((idx == end_idx - 1) ? ")" : ", ")))) {
          LOG_WARN("fail to assign sql", KR(ret), K(tenant_id), K(idx), K(start_idx), K(end_idx));
        }
      }

      const uint64_t meta_tenant_id = gen_meta_tenant_id(tenant_id);
      if (FAILEDx(trans.write(meta_tenant_id, sql.ptr(), affected_rows))) {
        LOG_WARN("fail to execute sql", KR(ret), K(meta_tenant_id), K(sql));
      } else {
        LOG_INFO("will batch delete tablet replica checksum", K(affected_rows));
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::remove_residual_checksum(
    ObISQLClient &sql_client,
    const uint64_t tenant_id,
    const ObAddr &server,
    const int64_t limit,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  char ip[OB_MAX_SERVER_ADDR_SIZE] = "";
  ObSqlString sql;
  const uint64_t sql_tenant_id = gen_meta_tenant_id(tenant_id);
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id)
                  || is_virtual_tenant_id(tenant_id)
                  || !server.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(server));
  } else if (OB_UNLIKELY(!server.ip_to_string(ip, sizeof(ip)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("convert server ip to string failed", KR(ret), K(server));
  } else if (OB_FAIL(sql.assign_fmt("DELETE FROM %s WHERE tenant_id = %lu AND svr_ip = '%s' AND"
             " svr_port = %d limit %ld", OB_ALL_TABLET_REPLICA_CHECKSUM_TNAME, tenant_id, ip,
             server.get_port(), limit))) {
    LOG_WARN("assign sql string failed", KR(ret), K(sql));
  } else if (OB_FAIL(sql_client.write(sql_tenant_id, sql.ptr(), affected_rows))) {
    LOG_WARN("execute sql failed", KR(ret), K(sql), K(sql_tenant_id));
  } else if (affected_rows > 0) {
    LOG_INFO("finish to remove residual checksum", KR(ret), K(tenant_id), K(affected_rows));
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::batch_get(
    const uint64_t tenant_id,
    const ObTabletLSPair &start_pair,
    const int64_t batch_cnt,
    const SCN &compaction_scn,
    ObISQLClient &sql_proxy,
    ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;

  ObSqlString sql;
  int64_t remain_cnt = batch_cnt;
  int64_t ori_items_cnt = 0;

  while (OB_SUCC(ret) && (remain_cnt > 0)) {
    sql.reuse();
    const int64_t limit_cnt = ((remain_cnt >= MAX_BATCH_COUNT) ? MAX_BATCH_COUNT : remain_cnt);
    ori_items_cnt = items.count();
    ObTabletLSPair last_pair;
    if (remain_cnt == batch_cnt) {
      last_pair = start_pair;
    } else {
      if (ori_items_cnt > 0) {
        const ObTabletReplicaChecksumItem &last_item = items.at(ori_items_cnt - 1);
        if (OB_FAIL(last_pair.init(last_item.tablet_id_, last_item.ls_id_))) {
          LOG_WARN("fail to init last tablet_ls_pair", KR(ret), K(last_item));
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("err unexpected, about tablet replica items count", KR(ret), K(tenant_id), K(start_pair),
          K(batch_cnt), K(remain_cnt), K(ori_items_cnt));
      }
    }

    if (FAILEDx(construct_batch_get_sql_str_(tenant_id, last_pair, limit_cnt, compaction_scn, sql))) {
      LOG_WARN("fail to construct load sql", KR(ret), K(tenant_id), K(last_pair), K(limit_cnt),
        K(compaction_scn));
    } else if (OB_FAIL(batch_get(tenant_id, sql, sql_proxy, items))) {
      LOG_WARN("fail to batch get tablet replica checksum items", KR(ret), K(tenant_id), K(sql));
    } else {
      const int64_t curr_items_cnt = items.count();
      if (curr_items_cnt - ori_items_cnt == limit_cnt) {
        // in case the checksum of three replica belong to one tablet, were split into two batch-get,
        // we will remove the last several item which belong to one tablet
        // if current round item's count less than limit_cnt, it means already to the end, no need to handle.    
        int64_t tmp_items_cnt = curr_items_cnt;
        ObTabletReplicaChecksumItem tmp_item;
        if (OB_FAIL(tmp_item.assign_key(items.at(tmp_items_cnt - 1)))) {
          LOG_WARN("fail to assign key", KR(ret), "tmp_item", items.at(tmp_items_cnt - 1));
        }
        
        while (OB_SUCC(ret) && (tmp_items_cnt > 0)) {
          if (tmp_item.is_same_tablet(items.at(tmp_items_cnt - 1))) {
            if (OB_FAIL(items.remove(tmp_items_cnt - 1))) {
              LOG_WARN("fail to remove item from array", KR(ret), K(tmp_items_cnt), K(items));
            } else {
              --tmp_items_cnt;
            }
          } else {
            break;
          } 
        }
        
        if (OB_SUCC(ret)) {
          if (tmp_items_cnt == 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected err about item count", KR(ret), K(tmp_item), K(curr_items_cnt), K(ori_items_cnt));
          } else {
            remain_cnt -= limit_cnt;
          }
        }
      } else {
        remain_cnt = 0; // already get all checksum item, finish batch_get
      }
    }
  }

  return ret;
}

int ObTabletReplicaChecksumOperator::batch_get(
    const uint64_t tenant_id,
    const ObIArray<ObTabletLSPair> &pairs,
    ObISQLClient &sql_proxy,
    ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  items.reset();
  const int64_t pairs_cnt = pairs.count();
  hash::ObHashMap<ObTabletLSPair, bool> pair_map;
  if (OB_UNLIKELY(pairs_cnt < 1 || OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), K(pairs_cnt));
  }
  // Step 1: check repeatable ObTabletLSPair by hash map
  if (FAILEDx(inner_init_tablet_pair_map_(pairs, pair_map))) {
    LOG_WARN("fail to init tablet_ls_pair map", KR(ret));
  }
  // Step 2: cut tablet replica checksum items into small batches
  int64_t start_idx = 0;
  int64_t end_idx = min(MAX_BATCH_COUNT, pairs_cnt);
  ObSqlString sql;
  while (OB_SUCC(ret) && (start_idx < end_idx)) {
    sql.reuse();
    if (OB_FAIL(construct_batch_get_sql_str_(tenant_id, pairs, start_idx, end_idx, sql))) {
      LOG_WARN("fail to construct batch get sql", KR(ret), K(tenant_id), K(pairs), 
        K(start_idx), K(end_idx));
    } else if (OB_FAIL(inner_batch_get_by_sql_(tenant_id, sql, sql_proxy, items))) {
      LOG_WARN("fail to inner batch get by sql", KR(ret), K(tenant_id), K(sql));
    } else {
      start_idx = end_idx;
      end_idx = min(start_idx + MAX_BATCH_COUNT, pairs_cnt);
    }
  }
  // Step 3: check tablet replica checksum item and set flag in map
  if (OB_SUCC(ret)) {
    int overwrite_flag = 1;
    ARRAY_FOREACH_N(items, idx, cnt) {
      ObTabletLSPair tmp_pair;
      if (OB_FAIL(tmp_pair.init(items.at(idx).tablet_id_, items.at(idx).ls_id_))) {
        LOG_WARN("fail to init tablet_ls_pair", KR(ret), K(items.at(idx)), K(idx));
      } else if (OB_FAIL(pair_map.set_refactored(tmp_pair, true, overwrite_flag))) {
        LOG_WARN("fail to set_fefactored", KR(ret), K(tenant_id), K(tmp_pair));
      }
    }
    // print tablet_ls_pair which not exist in tablet replica checksum table
    if (OB_SUCC(ret)) {
      FOREACH_X(iter, pair_map, OB_SUCC(ret)) {
        if (!iter->second) {
          LOG_TRACE("tablet replica checksum item not exist in tablet replica checksum table",
            KR(ret),  K(tenant_id), "tablet_ls_pair", iter->first);
        }
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::inner_init_tablet_pair_map_(
    const ObIArray<ObTabletLSPair> &pairs,
    hash::ObHashMap<ObTabletLSPair, bool> &pair_map)
{
  int ret = OB_SUCCESS;
  const int64_t pairs_cnt = pairs.count();
  if (FAILEDx(pair_map.create(hash::cal_next_prime(pairs_cnt * 2),
      ObModIds::OB_HASH_BUCKET))) {
    LOG_WARN("fail to create pair_map", KR(ret), K(pairs_cnt));
  } else {
    ARRAY_FOREACH_N(pairs, idx, cnt) {
      // if same talet_id exist, return error
      if (OB_FAIL(pair_map.set_refactored(pairs.at(idx), false))) {
        if (OB_HASH_EXIST == ret) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("exist repeatable tablet_ls_pair", KR(ret), K(pairs), K(idx));
        } else {
          LOG_WARN("fail to set refactored", KR(ret), K(pairs), K(idx));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (pair_map.size() != pairs_cnt) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid pair_map size", "size", pair_map.size(), K(pairs_cnt));
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::batch_get(
    const uint64_t tenant_id,
    const ObSqlString &sql,
    ObISQLClient &sql_proxy,
    ObIArray<ObTabletReplicaChecksumItem> &items)
{
  return inner_batch_get_by_sql_(tenant_id, sql, sql_proxy, items);
}

int ObTabletReplicaChecksumOperator::construct_batch_get_sql_str_(
    const uint64_t tenant_id,
    const ObTabletLSPair &start_pair,
    const int64_t batch_cnt,
    const SCN &compaction_scn,
    ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  if ((batch_cnt < 1) || (!compaction_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(batch_cnt), K(compaction_scn));
  } else if (OB_FAIL(sql.append_fmt("SELECT * FROM %s WHERE tenant_id = '%lu' and tablet_id > '%lu' "
      "and compaction_scn = %lu", OB_ALL_TABLET_REPLICA_CHECKSUM_TNAME, tenant_id,
      start_pair.get_tablet_id().id(), compaction_scn.get_val_for_inner_table_field()))) {
    LOG_WARN("fail to assign sql", KR(ret), K(tenant_id), K(start_pair), K(compaction_scn));
  } else if (OB_FAIL(sql.append_fmt(" ORDER BY tenant_id, tablet_id, svr_ip, svr_port limit %ld",
      batch_cnt))) {
    LOG_WARN("fail to assign sql string", KR(ret), K(tenant_id), K(batch_cnt));
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::construct_batch_get_sql_str_(
    const uint64_t tenant_id,
    const ObIArray<ObTabletLSPair> &pairs,
    const int64_t start_idx,
    const int64_t end_idx,
    ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  const int64_t pairs_cnt = pairs.count();
  if (start_idx < 0 || end_idx > pairs_cnt || start_idx > end_idx ||
      pairs_cnt < 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(start_idx), K(end_idx), K(pairs_cnt));
  } else if (OB_FAIL(sql.append_fmt("SELECT * FROM %s WHERE tenant_id = '%lu' and (tablet_id, ls_id)"
      " IN ((", OB_ALL_TABLET_REPLICA_CHECKSUM_TNAME, tenant_id))) {
    LOG_WARN("fail to assign sql", KR(ret), K(tenant_id));
  } else {
    for (int64_t idx = start_idx; OB_SUCC(ret) && (idx < end_idx); ++idx) {
      const ObTabletLSPair &pair = pairs.at(idx);
      if (OB_UNLIKELY(!pair.is_valid())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid tablet_ls_pair", KR(ret), K(tenant_id), K(pair));
      } else if (OB_FAIL(sql.append_fmt(
          "'%lu', %ld%s",
          pair.get_tablet_id().id(),
          pair.get_ls_id().id(),
          ((idx == end_idx - 1) ? ")" : "), (")))) {
        LOG_WARN("fail to assign sql", KR(ret), K(tenant_id), K(pair));
      }
    }
  }

  if (FAILEDx(sql.append_fmt(") ORDER BY tenant_id, tablet_id, ls_id, svr_ip, svr_port"))) {
    LOG_WARN("fail to assign sql string", KR(ret), K(tenant_id), K(pairs_cnt));
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::inner_batch_get_by_sql_(
    const uint64_t tenant_id,
    const ObSqlString &sql,
    ObISQLClient &sql_proxy,
    ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else {
    const uint64_t meta_tenant_id = gen_meta_tenant_id(tenant_id);
    SMART_VAR(ObISQLClient::ReadResult, result) {
      if (OB_FAIL(sql_proxy.read(result, meta_tenant_id, sql.ptr()))) {
        LOG_WARN("fail to execute sql", KR(ret), K(tenant_id), K(meta_tenant_id), "sql", sql.ptr());
      } else if (OB_ISNULL(result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get mysql result", KR(ret), "sql", sql.ptr());
      } else if (OB_FAIL(construct_tablet_replica_checksum_items_(*result.get_result(), items))) {
        LOG_WARN("fail to construct tablet checksum items", KR(ret), K(items));
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::construct_tablet_replica_checksum_items_(
    sqlclient::ObMySQLResult &res,
    ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  ObTabletReplicaChecksumItem item;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(res.next())) {
      if (OB_ITER_END != ret) {
        LOG_WARN("fail to get next result", KR(ret));
      }
    } else {
      item.reset();
      if (OB_FAIL(construct_tablet_replica_checksum_item_(res, item))) {
        LOG_WARN("fail to construct tablet checksum item", KR(ret));
      } else if (OB_FAIL(items.push_back(item))) {
        LOG_WARN("fail to push back checksum item", KR(ret), K(item));
      }
    }
  }
  if (OB_ITER_END == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::construct_tablet_replica_checksum_item_(
    sqlclient::ObMySQLResult &res,
    ObTabletReplicaChecksumItem &item)
{
  int ret = OB_SUCCESS;
  int64_t int_tenant_id = -1;
  int64_t int_tablet_id = -1;
  ObString ip;
  int64_t port = OB_INVALID_INDEX;
  int64_t ls_id = OB_INVALID_ID;
  uint64_t compaction_scn_val = 0;
  ObString column_meta_hex_str;

  (void)GET_COL_IGNORE_NULL(res.get_int, "tenant_id", int_tenant_id);
  (void)GET_COL_IGNORE_NULL(res.get_int, "tablet_id", int_tablet_id);
  (void)GET_COL_IGNORE_NULL(res.get_varchar, "svr_ip", ip);
  (void)GET_COL_IGNORE_NULL(res.get_int, "svr_port", port);
  (void)GET_COL_IGNORE_NULL(res.get_int, "ls_id", ls_id);
  (void)GET_COL_IGNORE_NULL(res.get_uint, "compaction_scn", compaction_scn_val);
  (void)GET_COL_IGNORE_NULL(res.get_int, "row_count", item.row_count_);
  (void)GET_COL_IGNORE_NULL(res.get_int, "data_checksum", item.data_checksum_);
  (void)GET_COL_IGNORE_NULL(res.get_varchar, "b_column_checksums", column_meta_hex_str);

  if (OB_FAIL(item.compaction_scn_.convert_for_inner_table_field(compaction_scn_val))) {
    LOG_WARN("fail to convert val to SCN", KR(ret), K(compaction_scn_val));
  } else {
    item.tenant_id_ = (uint64_t)int_tenant_id;
    item.tablet_id_ = (uint64_t)int_tablet_id;
    item.ls_id_ = ls_id;
    if (OB_UNLIKELY(!item.server_.set_ip_addr(ip, port))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to set ip_addr", KR(ret), K(item), K(ip), K(port));
    } else if (OB_FAIL(set_column_meta_with_hex_str(column_meta_hex_str, item.column_meta_))) {
      LOG_WARN("fail to deserialize column meta from hex str", KR(ret));
    }
  }

  LOG_TRACE("construct tablet checksum item", KR(ret), K(item));
  return ret;
}


int ObTabletReplicaChecksumOperator::batch_update_with_trans(
    common::ObMySQLTransaction &trans,
    const uint64_t tenant_id,
    const common::ObIArray<ObTabletReplicaChecksumItem> &items)
{
  return batch_insert_or_update_with_trans_(tenant_id, items, trans, true);
}

int ObTabletReplicaChecksumOperator::batch_insert_or_update_with_trans_(
    const uint64_t tenant_id,
    const ObIArray<ObTabletReplicaChecksumItem> &items,
    common::ObMySQLTransaction &trans,
    const bool is_update)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY((OB_INVALID_TENANT_ID == tenant_id) || (items.count() <= 0))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), "items count", items.count());
  } else {
    int64_t start_idx = 0;
    int64_t end_idx = min(MAX_BATCH_COUNT, items.count());
    while (OB_SUCC(ret) && (start_idx < end_idx)) {
      if (OB_FAIL(inner_batch_insert_or_update_by_sql_(tenant_id, items, start_idx,
          end_idx, trans, is_update))) {
        LOG_WARN("fail to inner batch insert", KR(ret), K(tenant_id), K(start_idx), K(is_update));
      } else {
        start_idx = end_idx;
        end_idx = min(start_idx + MAX_BATCH_COUNT, items.count());
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::inner_batch_insert_or_update_by_sql_(
    const uint64_t tenant_id,
    const ObIArray<ObTabletReplicaChecksumItem> &items,
    const int64_t start_idx,
    const int64_t end_idx,
    ObISQLClient &sql_client,
    const bool is_update)
{
  int ret = OB_SUCCESS;
  const int64_t item_cnt = items.count();
  if (OB_UNLIKELY((!is_valid_tenant_id(tenant_id))
      || (item_cnt <= 0)
      || (start_idx < 0)
      || (start_idx >= end_idx)
      || (end_idx > item_cnt))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id), "items count", item_cnt, 
      K(start_idx), K(end_idx));
  } else {
    int64_t affected_rows = 0;
    ObSqlString sql;

    if (OB_FAIL(sql.assign_fmt("INSERT INTO %s (tenant_id, tablet_id, ls_id, svr_ip, svr_port, row_count, "
          "compaction_scn, data_checksum, column_checksums, b_column_checksums, gmt_modified, gmt_create) VALUES",
          OB_ALL_TABLET_REPLICA_CHECKSUM_TNAME))) {
        LOG_WARN("fail to assign sql", KR(ret), K(tenant_id));
    } else {
      ObArenaAllocator allocator;
      char *hex_buf = NULL;

      for (int64_t idx = start_idx; OB_SUCC(ret) && (idx < end_idx); ++idx) {
        const ObTabletReplicaChecksumItem &cur_item = items.at(idx);
        const uint64_t compaction_scn_val = cur_item.compaction_scn_.get_val_for_inner_table_field();
        char ip[OB_MAX_SERVER_ADDR_SIZE] = "";
        ObString visible_column_meta;
        ObString hex_column_meta;

        if (OB_UNLIKELY(!cur_item.server_.ip_to_string(ip, sizeof(ip)))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("fail to convert server ip to string", KR(ret), "server", cur_item.server_);
        } else if (OB_FAIL(get_visible_column_meta(cur_item.column_meta_, allocator, visible_column_meta))) {
          LOG_WARN("fail to get visible column meta str", KR(ret));
        } else if (OB_FAIL(get_hex_column_meta(cur_item.column_meta_, allocator, hex_column_meta))) {
          LOG_WARN("fail to get hex column meta str", KR(ret));
        } else if (OB_FAIL(sql.append_fmt("('%lu', '%lu', %ld, '%s', %d, %ld, %lu, %ld, "
                  "'%.*s', '%.*s', now(6), now(6))%s", cur_item.tenant_id_, cur_item.tablet_id_.id(),
                  cur_item.ls_id_.id(), ip, cur_item.server_.get_port(), cur_item.row_count_,
                  compaction_scn_val, cur_item.data_checksum_, visible_column_meta.length(),
                  visible_column_meta.ptr(), hex_column_meta.length(), hex_column_meta.ptr(),
                  ((idx == end_idx - 1) ? " " : ", ")))) {
          LOG_WARN("fail to assign sql", KR(ret), K(idx), K(end_idx), K(cur_item));
        }
      }

      if (is_update) {
        if (FAILEDx(sql.append_fmt(" ON DUPLICATE KEY UPDATE "))) {
          LOG_WARN("fail to append sql string", KR(ret), K(sql));
        } else if (OB_FAIL(sql.append(" row_count = values(row_count)"))
            || OB_FAIL(sql.append(", compaction_scn = values(compaction_scn)"))
            || OB_FAIL(sql.append(", data_checksum = values(data_checksum)"))
            || OB_FAIL(sql.append(", column_checksums = values(column_checksums)"))
            || OB_FAIL(sql.append(", b_column_checksums = values(b_column_checksums)"))) {
          LOG_WARN("fail to append sql string", KR(ret), K(sql));
        }
      }
    }
    
    const uint64_t meta_tenant_id = gen_meta_tenant_id(tenant_id);
    if (FAILEDx(sql_client.write(meta_tenant_id, sql.ptr(), affected_rows))) {
      LOG_WARN("fail to execute sql", KR(ret), K(tenant_id), K(meta_tenant_id), K(sql));
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::check_column_checksum(
    const uint64_t tenant_id,
    const ObTableSchema &data_table_schema,
    const ObTableSchema &index_table_schema,
    const SCN &compaction_scn,
    ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_tenant_id(tenant_id) 
      || !data_table_schema.is_valid() 
      || !index_table_schema.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(tenant_id), K(data_table_schema), K(index_table_schema));
  } else {
    const bool is_global_index = index_table_schema.is_global_index_table();
    if (is_global_index) {
      if (OB_FAIL(check_global_index_column_checksum(tenant_id, data_table_schema, index_table_schema,
          compaction_scn, sql_proxy))) {
        LOG_WARN("fail to check global index column checksum", KR(ret), K(tenant_id), K(compaction_scn));
      }
    } else {
      if (OB_FAIL(check_local_index_column_checksum(tenant_id, data_table_schema, index_table_schema,
          compaction_scn, sql_proxy))) {
        LOG_WARN("fail to check local index column checksum", KR(ret), K(tenant_id), K(compaction_scn));
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::check_global_index_column_checksum(
    const uint64_t tenant_id,
    const ObTableSchema &data_table_schema,
    const ObTableSchema &index_table_schema,
    const SCN &compaction_scn,
    ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  const int64_t default_column_cnt = ObTabletReplicaReportColumnMeta::DEFAULT_COLUMN_CNT;
  int64_t check_cnt = 0;
  bool need_verify = false;
  uint64_t index_table_id = UINT64_MAX;
  uint64_t data_table_id = UINT64_MAX;
  // map element: <column_id, checksum_sum>
  hash::ObHashMap<int64_t, int64_t> data_column_ckm_sum_map;
  hash::ObHashMap<int64_t, int64_t> index_column_ckm_sum_map;

  ObArray<ObTabletID> data_schema_tablet_ids;
  ObArray<ObTabletID> index_schema_tablet_ids;
  SMART_VAR(ObArray<ObTabletReplicaChecksumItem>, data_table_ckm_items) {
    SMART_VAR(ObArray<ObTabletReplicaChecksumItem>, index_table_ckm_items) {
      if (OB_FAIL(data_column_ckm_sum_map.create(default_column_cnt, ObModIds::OB_SSTABLE_CREATE_INDEX))) {
        LOG_WARN("fail to create data table column ckm_sum map", KR(ret), K(default_column_cnt));
      } else if (OB_FAIL(index_column_ckm_sum_map.create(default_column_cnt, ObModIds::OB_SSTABLE_CREATE_INDEX))) {
        LOG_WARN("fail to create index table column ckm_sum map", KR(ret), K(default_column_cnt));
      } else {
        index_table_id = index_table_schema.get_table_id();
        data_table_id = data_table_schema.get_table_id();
        ObTabletID unused_tablet_id;
        ObColumnChecksumErrorInfo ckm_error_info(tenant_id, compaction_scn, true, data_table_id, index_table_id,
          unused_tablet_id, unused_tablet_id);

        if (OB_FAIL(get_tablet_replica_checksum_items_(tenant_id, sql_proxy, index_table_schema, 
            index_schema_tablet_ids, index_table_ckm_items))) {
          LOG_WARN("fail to get index table tablet replica ckm_items", KR(ret), K(tenant_id), K(index_table_schema));
        } else if (OB_FAIL(need_verify_checksum_(compaction_scn, need_verify, index_schema_tablet_ids,
            index_table_ckm_items))) {
          if (OB_EAGAIN != ret) {
            LOG_WARN("fail to check need verify checksum", KR(ret), K(index_table_id), K(data_table_id),
              K(compaction_scn));
          }
        } else if (!need_verify) {
          LOG_INFO("do not need verify checksum", K(index_table_id), K(data_table_id), K(compaction_scn));
        } else if (OB_FAIL(get_column_checksum_sum_map_(index_table_schema, compaction_scn,
            index_column_ckm_sum_map, index_table_ckm_items))) {
          if (OB_EAGAIN != ret) {
            LOG_WARN("fail to get index table column checksum_sum map", KR(ret), K(index_table_id), K(data_table_id),
              K(compaction_scn));
          } else if (REACH_TIME_INTERVAL(10 * 1000 * 1000)) {
            LOG_WARN("fail to get index table tablet checksum items", KR(ret), K(index_table_schema));
          }
        } else if (OB_FAIL(get_tablet_replica_checksum_items_(tenant_id, sql_proxy, data_table_schema, 
            data_schema_tablet_ids, data_table_ckm_items))) {
          LOG_WARN("fail to get data table tablet replica ckm_items", KR(ret), K(tenant_id), K(data_table_schema));
        } else if (OB_FAIL(need_verify_checksum_(compaction_scn, need_verify, data_schema_tablet_ids,
            data_table_ckm_items))) {
          if (OB_EAGAIN != ret) {
            LOG_WARN("fail to check need verify checksum", KR(ret), K(index_table_id), K(data_table_id),
              K(compaction_scn));
          }
        } else if (!need_verify) {
          LOG_INFO("do not need verify checksum", K(index_table_id), K(data_table_id), K(compaction_scn));
        } else if (OB_FAIL(get_column_checksum_sum_map_(data_table_schema, compaction_scn,
            data_column_ckm_sum_map, data_table_ckm_items))) {
          if (OB_EAGAIN != ret) {
            LOG_WARN("fail to get data table column checksum_sum map", KR(ret), K(data_table_id), K(index_table_id),
              K(compaction_scn));
          } else if (REACH_TIME_INTERVAL(10 * 1000 * 1000)) {
            LOG_WARN("fail to get data table tablet checksum items", KR(ret), K(data_table_schema));
          }
        } else if (OB_FAIL(compare_column_checksum_(data_table_schema, index_table_schema, data_column_ckm_sum_map,
            index_column_ckm_sum_map, check_cnt, ckm_error_info))) {
          if (OB_CHECKSUM_ERROR == ret) {
            LOG_ERROR("data table and global index table column checksum are not equal", KR(ret), K(ckm_error_info));
            int tmp_ret = OB_SUCCESS;
            if (OB_TMP_FAIL(ObColumnChecksumErrorOperator::insert_column_checksum_err_info(sql_proxy, tenant_id,
                ckm_error_info))) {
              LOG_WARN("fail to insert global index column checksum error info", KR(tmp_ret), K(ckm_error_info));
            }
          }
        }
      }
    } // end smart_var
  } // end smart_var

  if (data_column_ckm_sum_map.created()) {
    data_column_ckm_sum_map.destroy();
  }
  if (index_column_ckm_sum_map.created()) {
    index_column_ckm_sum_map.destroy();
  }
  LOG_INFO("finish verify global index table columns checksum", KR(ret), K(tenant_id), K(data_table_id), 
    K(index_table_id), K(check_cnt));

  return ret;
}

int ObTabletReplicaChecksumOperator::check_local_index_column_checksum(
    const uint64_t tenant_id,
    const ObTableSchema &data_table_schema,
    const ObTableSchema &index_table_schema,
    const SCN &compaction_scn,
    ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  const uint64_t index_table_id = index_table_schema.get_table_id();
  const uint64_t data_table_id = data_table_schema.get_table_id();
  const int64_t default_column_cnt = ObTabletReplicaReportColumnMeta::DEFAULT_COLUMN_CNT;
  int64_t check_cnt = 0;
  bool need_verify = false;

  ObArray<ObTabletID> data_schema_tablet_ids;
  ObArray<ObTabletID> index_schema_tablet_ids;
  SMART_VAR(ObArray<ObTabletReplicaChecksumItem>, data_table_ckm_items) {
    SMART_VAR(ObArray<ObTabletReplicaChecksumItem>, index_table_ckm_items) {
      if (OB_FAIL(get_tablet_replica_checksum_items_(tenant_id, sql_proxy, index_table_schema, 
          index_schema_tablet_ids, index_table_ckm_items))) {
        LOG_WARN("fail to get index table tablet replica ckm_items", KR(ret), K(tenant_id), K(index_table_schema));
      } else if (OB_FAIL(need_verify_checksum_(compaction_scn, need_verify, index_schema_tablet_ids,
          index_table_ckm_items))) {
        if (OB_EAGAIN != ret) {
          LOG_WARN("fail to check need verify checksum", KR(ret), K(index_table_id), K(data_table_id),
            K(compaction_scn));
        }
      } else if (!need_verify) {
        LOG_INFO("do not need verify checksum", K(index_table_id), K(data_table_id), K(compaction_scn));
      } else if (OB_FAIL(get_tablet_replica_checksum_items_(tenant_id, sql_proxy, data_table_schema, 
          data_schema_tablet_ids, data_table_ckm_items))) {
        LOG_WARN("fail to get data table tablet replica ckm_items", KR(ret), K(tenant_id), K(data_table_schema));
      } else if (OB_FAIL(need_verify_checksum_(compaction_scn, need_verify, data_schema_tablet_ids,
          data_table_ckm_items))) {
        if (OB_EAGAIN != ret) {
          LOG_WARN("fail to check need verify checksum", KR(ret), K(index_table_id), K(data_table_id),
            K(compaction_scn));
        }
      } else if (data_schema_tablet_ids.count() != index_schema_tablet_ids.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("tablet count of local index table is not same with data table", KR(ret), "data_table_tablet_cnt",
          data_schema_tablet_ids.count(), "index_table_tablet_cnt", index_schema_tablet_ids.count());
      } else {
        // map element: <column_id, checksum>
        hash::ObHashMap<int64_t, int64_t> data_column_ckm_map;
        hash::ObHashMap<int64_t, int64_t> index_column_ckm_map;
        if (OB_FAIL(data_column_ckm_map.create(default_column_cnt, ObModIds::OB_SSTABLE_CREATE_INDEX))) {
          LOG_WARN("fail to create data table column ckm map", KR(ret), K(default_column_cnt));
        } else if (OB_FAIL(index_column_ckm_map.create(default_column_cnt, ObModIds::OB_SSTABLE_CREATE_INDEX))) {
          LOG_WARN("fail to create index table column ckm map", KR(ret), K(default_column_cnt));
        } 

        // One tablet of local index table is mapping to one tablet of data table
        const int64_t tablet_cnt = data_schema_tablet_ids.count();
        for (int64_t i = 0; (i < tablet_cnt) && OB_SUCC(ret); ++i) {
          if (OB_FAIL(data_column_ckm_map.clear())) {
            LOG_WARN("fail to clear hash map", KR(ret), K(default_column_cnt));
          } else if (OB_FAIL(index_column_ckm_map.clear())) {
            LOG_WARN("fail to clear hash map", KR(ret), K(default_column_cnt));
          } else {
            const ObTabletID &data_tablet_id = data_schema_tablet_ids.at(i);
            const ObTabletID &index_tablet_id = index_schema_tablet_ids.at(i);
            int64_t data_tablet_idx = OB_INVALID_INDEX;
            int64_t index_tablet_idx = OB_INVALID_INDEX;
            if (OB_FAIL(find_checksum_item_by_id_(data_tablet_id, data_table_ckm_items, compaction_scn, data_tablet_idx))) {
              LOG_WARN("fail to find checksum item by tablet_id", KR(ret), K(data_tablet_id), K(compaction_scn));
            } else if (OB_FAIL(find_checksum_item_by_id_(index_tablet_id, index_table_ckm_items, compaction_scn, index_tablet_idx))) {
              LOG_WARN("fail to find checksum item by tablet_id", KR(ret), K(index_tablet_id), K(compaction_scn));
            } else {
              // compare column checksum of index schema tablet and data schema tablet
              check_cnt = 0;
              const ObTabletReplicaChecksumItem &data_ckm_item = data_table_ckm_items.at(data_tablet_idx);
              const ObTabletReplicaChecksumItem &index_ckm_item = index_table_ckm_items.at(index_tablet_idx);

              ObColumnChecksumErrorInfo ckm_error_info(tenant_id, compaction_scn, false, data_table_id, index_table_id,
                data_tablet_id, index_tablet_id);

              if (OB_FAIL(get_column_checksum_map_(data_table_schema, compaction_scn,
                  data_column_ckm_map, data_ckm_item))) {
                LOG_WARN("fail to get column ckm map of one data table tablet", KR(ret), K(data_ckm_item));
              } else if (OB_FAIL(get_column_checksum_map_(index_table_schema, compaction_scn,
                  index_column_ckm_map, index_ckm_item))) {
                LOG_WARN("fail to get column ckm map of one index table tablet", KR(ret), K(index_ckm_item));
              } else if (OB_FAIL(compare_column_checksum_(data_table_schema, index_table_schema, data_column_ckm_map,
                  index_column_ckm_map, check_cnt, ckm_error_info))) {
                if (OB_CHECKSUM_ERROR == ret) {
                  LOG_ERROR("data table and local index table column checksum are not equal", KR(ret), K(ckm_error_info));
                  int tmp_ret = OB_SUCCESS;
                  if (OB_TMP_FAIL(ObColumnChecksumErrorOperator::insert_column_checksum_err_info(sql_proxy, tenant_id,
                      ckm_error_info))) {
                    LOG_WARN("fail to insert local index column checksum error info", KR(tmp_ret), K(ckm_error_info));
                  }
                }
              }
            }
          }
        } // end loop

        if (data_column_ckm_map.created()) {
          data_column_ckm_map.destroy();
        }
        if (index_column_ckm_map.created()) {
          index_column_ckm_map.destroy();
        }
      }
    }
  }

  LOG_INFO("finish verify local index table columns checksum", KR(ret), K(tenant_id), K(data_table_id), 
    K(index_table_id), K(check_cnt), K(data_schema_tablet_ids.count()));

  return ret;
}

int ObTabletReplicaChecksumOperator::get_column_checksum_sum_map_(
    const ObTableSchema &table_schema,
    const SCN &compaction_scn,
    hash::ObHashMap<int64_t, int64_t> &column_ckm_sum_map,
    const ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  ObArray<ObColDesc> column_descs;
  if (items.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(items.count()));
  } else if (OB_FAIL(table_schema.get_multi_version_column_descs(column_descs))) {
    LOG_WARN("fail to get multi version column descs", KR(ret), K(table_schema));
  } else {
    const int64_t column_descs_cnt = column_descs.count();
    const int64_t items_cnt = items.count();
    const int64_t column_checksums_cnt = items.at(0).column_meta_.column_checksums_.count();

    if (column_checksums_cnt > column_descs_cnt) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table schema column id count must not less than column checksum count exclude hidden column",
        KR(ret), K(column_checksums_cnt), K(column_descs_cnt));
    }

    uint64_t pre_tablet_id = OB_INVALID_ID;
    // items are order by tablet_id
    for (int64_t i = 0; (i < items_cnt) && OB_SUCC(ret); ++i) {
      const ObTabletReplicaChecksumItem &cur_item = items.at(i);
      if (cur_item.compaction_scn_ == compaction_scn) {
        const ObTabletReplicaReportColumnMeta &cur_column_meta = cur_item.column_meta_;
        if (pre_tablet_id == OB_INVALID_ID) {
          for (int64_t j = 0; (j < column_checksums_cnt) && OB_SUCC(ret); ++j) {
            const int64_t cur_column_id = column_descs.at(j).col_id_;
            const int64_t cur_column_checksum = cur_column_meta.column_checksums_.at(j);
            if (OB_FAIL(column_ckm_sum_map.set_refactored(cur_column_id, cur_column_checksum))) {
              LOG_WARN("fail to set column ckm_sum to map", KR(ret), K(cur_column_id), 
                K(cur_column_checksum), K(column_checksums_cnt));
            } 
          }
        } else {
          if (cur_item.tablet_id_.id() != pre_tablet_id) { // start new tablet
            for (int64_t j = 0; (j < column_checksums_cnt) && OB_SUCC(ret); ++j) {
              const int64_t cur_column_id = column_descs.at(j).col_id_;
              const int64_t cur_column_checksum = cur_column_meta.column_checksums_.at(j);
              int64_t last_column_checksum_sum = 0;
              if (OB_FAIL(column_ckm_sum_map.get_refactored(cur_column_id, last_column_checksum_sum))) {
                LOG_WARN("fail to get column ckm_sum from map", KR(ret), K(cur_column_id));
              } else if (OB_FAIL(column_ckm_sum_map.set_refactored(cur_column_id, 
                  cur_column_checksum + last_column_checksum_sum, true))) {
                LOG_WARN("fail to set column ckm_sum to map", KR(ret), K(cur_column_id), K(cur_column_checksum),
                  "cur_column_ckm_sum", (cur_column_checksum + last_column_checksum_sum));
              } 
            }
          } 
        }
        pre_tablet_id = cur_item.tablet_id_.id();
      }
    } // end for loop
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::get_column_checksum_map_(
      const ObTableSchema &table_schema,
      const SCN &compaction_scn,
      hash::ObHashMap<int64_t, int64_t> &column_ckm_map,
      const ObTabletReplicaChecksumItem &item)
{
  int ret = OB_SUCCESS;
  ObArray<ObColDesc> column_descs;
  if (!item.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(item));
  } else if (OB_FAIL(table_schema.get_multi_version_column_descs(column_descs))) {
    LOG_WARN("fail to get multi version column descs", KR(ret), K(table_schema));
  } else {
    const int64_t column_descs_cnt = column_descs.count();
    const int64_t column_checksums_cnt = item.column_meta_.column_checksums_.count();

    // TODO donglou, make sure whether they are equal or not;
    if (column_checksums_cnt > column_descs_cnt) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table schema column id count must not less than column checksum count exclude hidden column",
        KR(ret), K(column_checksums_cnt), K(column_descs_cnt));
    }

    if (item.compaction_scn_ == compaction_scn) {
      const ObTabletReplicaReportColumnMeta &column_meta = item.column_meta_;
      for (int64_t i = 0; (i < column_checksums_cnt) && OB_SUCC(ret); ++i) {
        const int64_t cur_column_id = column_descs.at(i).col_id_;
        const int64_t cur_column_checksum = column_meta.column_checksums_.at(i);
        if (OB_FAIL(column_ckm_map.set_refactored(cur_column_id, cur_column_checksum))) {
          LOG_WARN("fail to set column checksum to map", KR(ret), K(cur_column_id), 
            K(cur_column_checksum), K(column_checksums_cnt));
        } 
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("snapshot_version mismtach", KR(ret), K(item.compaction_scn_), K(compaction_scn));
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::find_checksum_item_by_id_(
    const ObTabletID &tablet_id,
    ObIArray<ObTabletReplicaChecksumItem> &items,
    const SCN &compaction_scn,
    int64_t &idx)
{
  int ret = OB_SUCCESS;
  idx = OB_INVALID_INDEX;
  const int64_t item_cnt = items.count();
  if (!tablet_id.is_valid() || (item_cnt < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_id), K(item_cnt));
  } else {
    for (int64_t i = 0; i < item_cnt; ++i) {
      if ((items.at(i).tablet_id_ == tablet_id)
          && (items.at(i).compaction_scn_ == compaction_scn)) {
        idx = i;
        break;
      }
    }

    if (idx == OB_INVALID_INDEX) {
      ret = OB_ENTRY_NOT_EXIST;
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::get_tablet_replica_checksum_items_(
    const uint64_t tenant_id,
    ObMySQLProxy &sql_proxy,
    const ObTableSchema &table_schema,
    ObIArray<ObTabletID> &tablet_ids,
    ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  if (!is_valid_tenant_id(tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tenant_id));
  } else if (OB_FAIL(get_table_all_tablet_id_(table_schema, tablet_ids))) {
    LOG_WARN("fail to get table all tablet id", KR(ret), K(table_schema));
  } else if (tablet_ids.count() > 0) {
    const uint64_t table_id = table_schema.get_table_id();
    ObArray<ObTabletLSPair> pairs;
    ObArray<ObLSID> ls_ids;

    // sys_table's tablet->ls relation won't be written into __all_tablet_to_ls
    if (is_sys_tenant(tenant_id) || is_sys_table(table_id)) {
      for (int64_t i = 0; (i < tablet_ids.count()) && OB_SUCC(ret); ++i) {
        ObLSID tmp_ls_id(ObLSID::SYS_LS_ID);
        if (OB_FAIL(ls_ids.push_back(tmp_ls_id))) {
          LOG_WARN("fail to push back ls_id", KR(ret), K(tenant_id), K(table_id));
        }
      }
    } else if (OB_FAIL(ObTabletToLSTableOperator::batch_get_ls(sql_proxy, tenant_id, tablet_ids, ls_ids))) {
      LOG_WARN("fail to batch get ls", KR(ret), K(tenant_id), K(tablet_ids));
    }

    if (OB_SUCC(ret) && (ls_ids.count() != tablet_ids.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("count mismatch", KR(ret), K(ls_ids.count()), K(tablet_ids.count())); 
    }
    
    if (OB_SUCC(ret)) {
      SMART_VAR(ObArray<ObTabletLSPair>, pairs) {
        const int64_t ls_id_cnt = ls_ids.count();
        for (int64_t i = 0; (i < ls_id_cnt) && OB_SUCC(ret); ++i) {
          ObTabletLSPair cur_pair;
          const ObTabletID &cur_tablet_id = tablet_ids.at(i);
          const ObLSID &cur_ls_id = ls_ids.at(i);
          if (OB_FAIL(cur_pair.init(cur_tablet_id, cur_ls_id))) {
            LOG_WARN("fail to init tablet_ls_pair", KR(ret), K(i), K(cur_tablet_id), K(cur_ls_id));
          } else if (OB_FAIL(pairs.push_back(cur_pair))) {
            LOG_WARN("fail to push back pair", KR(ret), K(cur_pair));
          }
        }

        if (OB_FAIL(ret)){
        } else if (OB_UNLIKELY(pairs.count() != ls_id_cnt)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("some unexpected err about tablet_ls_pair count", KR(ret), K(ls_id_cnt), K(pairs.count()));
        } else if (OB_FAIL(ObTabletReplicaChecksumOperator::batch_get(tenant_id, pairs, sql_proxy, items))) {
          LOG_WARN("fail to batch get tablet checksum item", KR(ret), K(tenant_id), 
                   "pairs_count", pairs.count());
        } 
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::get_table_all_tablet_id_(
    const ObTableSchema &table_schema,
    ObIArray<ObTabletID> &schema_tablet_ids)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!table_schema.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(table_schema));
  } else {
    const uint64_t table_id = table_schema.get_table_id();
    // TODO donglou, sys table can use table_schema.get_tablet_ids ?
    if (is_sys_table(table_id)) {
      const ObTabletID &tablet_id = table_schema.get_tablet_id();
      if (OB_UNLIKELY(!tablet_id.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected err, invalid tablet_id", KR(ret), K(table_id), K(tablet_id));
      } else if (OB_FAIL(schema_tablet_ids.push_back(tablet_id))) {
        LOG_WARN("fail to push back tablet_id", KR(ret), K(tablet_id));
      }
    } else if (table_schema.has_tablet()) {
      if (OB_FAIL(table_schema.get_tablet_ids(schema_tablet_ids))) {
        LOG_WARN("fail to get tablet_ids from table schema", KR(ret));
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::need_verify_checksum_(
    const SCN &compaction_scn,
    bool &need_verify,
    ObIArray<ObTabletID> &schema_tablet_ids,
    ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  need_verify = false;
  const int64_t item_cnt = items.count();
  const int64_t schema_tablet_cnt = schema_tablet_ids.count();
  if (item_cnt <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(item_cnt));
  } else {
    SCN min_compaction_scn = SCN::max_scn();
    SCN max_compaction_scn = SCN::min_scn();
    for (int64_t i = 0; i < item_cnt; ++i) {
      const SCN &cur_compaction_scn = items.at(i).compaction_scn_;
      if (cur_compaction_scn < min_compaction_scn) {
        min_compaction_scn = cur_compaction_scn;
      }
      if (cur_compaction_scn > max_compaction_scn) {
        max_compaction_scn = cur_compaction_scn;
      }
    }

    if ((min_compaction_scn == compaction_scn)
        && (max_compaction_scn == compaction_scn)) {
      need_verify = true;
    } else if ((min_compaction_scn < compaction_scn)
               && (max_compaction_scn == compaction_scn)) {
      hash::ObHashMap<uint64_t, bool> reported_tablet_ids;
      if (OB_FAIL(reported_tablet_ids.create(schema_tablet_cnt, ObModIds::OB_SSTABLE_CREATE_INDEX))) {
        LOG_WARN("fail to create reported tablet ids map", KR(ret), K(schema_tablet_cnt));
      }
      for (int64_t i = 0; (i < item_cnt) && OB_SUCC(ret); ++i) {
        if (items.at(i).compaction_scn_ == compaction_scn) {
          if (OB_FAIL(reported_tablet_ids.set_refactored(items.at(i).tablet_id_.id(), true, true/*overwrite*/))) {
            LOG_WARN("fail to set to hashmap", KR(ret), K(items.at(i)));
          }
        }
      }

      // when each tablet has at lease one replica which finished compaction
      // with @compaction_scn, we will validate checksum
      for (int64_t i = 0; (i < schema_tablet_cnt) && OB_SUCC(ret); ++i) {
        bool value = false;
        const ObTabletID &cur_tablet_id = schema_tablet_ids.at(i);
        if (OB_FAIL(reported_tablet_ids.get_refactored(cur_tablet_id.id(), value))) {
          if (OB_HASH_NOT_EXIST == ret) {
            ret = OB_EAGAIN;
            LOG_WARN("tablet has not reported", KR(ret), K(cur_tablet_id), K(schema_tablet_cnt));
          }
        }
      }

      if (OB_SUCC(ret)) {
        need_verify = true;
      }
    } else {
      LOG_INFO("snapshot version not match, no need nerify", K(min_compaction_scn), K(max_compaction_scn));
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::compare_column_checksum_(
    const ObTableSchema &data_table_schema,
    const ObTableSchema &index_table_schema,
    const hash::ObHashMap<int64_t, int64_t> &data_column_ckm_map,
    const hash::ObHashMap<int64_t, int64_t> &index_column_ckm_map,
    int64_t &check_cnt,
    ObColumnChecksumErrorInfo &ckm_error_info)
{
  int ret = OB_SUCCESS;
  check_cnt = 0;

  if ((data_column_ckm_map.size() < 1) || (index_column_ckm_map.size() < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(data_column_ckm_map.size()), K(data_column_ckm_map.size()));
  }

  for (hash::ObHashMap<int64_t, int64_t>::const_iterator iter = index_column_ckm_map.begin();
       OB_SUCC(ret) && (iter != index_column_ckm_map.end()); ++iter) {
    if ((iter->first == OB_HIDDEN_TRANS_VERSION_COLUMN_ID) || (iter->first == OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID)) {
      // there not exists a promise: these two hidden columns checksums in data table and index table are equal。
      // thus, we skip them
    } else {
      int64_t data_table_column_ckm = 0;
      // is_virtual_generated_column is only tag in data table
      const ObColumnSchemaV2 *column_schema = data_table_schema.get_column_schema(iter->first);
      if (NULL == column_schema) {
        column_schema = index_table_schema.get_column_schema(iter->first);
      }
      if (OB_ISNULL(column_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, column schema must not be NULL", KR(ret));
      } else if ((column_schema->is_shadow_column()) || (!column_schema->is_column_stored_in_sstable())) {
        // shadow column only exists in index table; if not stored in sstable, means virtual column/ROWID fake column
        LOG_INFO("column do not need to compare checksum", K(iter->first), K(column_schema->is_shadow_column()));
      } else if (OB_FAIL(data_column_ckm_map.get_refactored(iter->first, data_table_column_ckm))) {
        LOG_WARN("fail to get column ckm_sum, cuz this column_id not exist in data_table", KR(ret), "column_id",
          iter->first, K(data_table_schema), K(index_table_schema));
      } else {
        ++check_cnt;
        if (data_table_column_ckm != iter->second) {
          ret = OB_CHECKSUM_ERROR;
          ckm_error_info.column_id_ = iter->first;
          ckm_error_info.index_column_checksum_ = iter->second;
          ckm_error_info.data_column_checksum_ = data_table_column_ckm; 
        }
      }
    }
  } // end loop
  return ret;
}

void ObTabletReplicaChecksumOperator::print_detail_tablet_replica_checksum(
    const ObIArray<ObTabletReplicaChecksumItem> &items)
{
  const int64_t item_cnt = items.count();
  for (int64_t i = 0; i < item_cnt; ++i) {
    const ObTabletReplicaChecksumItem &cur_item = items.at(i);
    FLOG_WARN("detail tablet replica checksum", K(i), K(cur_item));
  }
}

int ObTabletReplicaChecksumOperator::set_column_meta_with_hex_str(
    const common::ObString &hex_str,
    ObTabletReplicaReportColumnMeta &column_meta)
{
  int ret = OB_SUCCESS;
  const int64_t hex_str_len = hex_str.length();
  if (hex_str_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(hex_str_len), K(hex_str));
  } else {
    const int64_t deserialize_size = ObTabletReplicaReportColumnMeta::MAX_OCCUPIED_BYTES;
    int64_t deserialize_pos = 0;
    char *deserialize_buf = NULL;
    ObArenaAllocator allocator;

    if (OB_ISNULL(deserialize_buf = static_cast<char *>(allocator.alloc(deserialize_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", KR(ret), K(deserialize_size));
    } else if (OB_FAIL(hex_to_cstr(hex_str.ptr(), hex_str_len, deserialize_buf, deserialize_size))) {
      LOG_WARN("fail to get cstr from hex", KR(ret), K(hex_str_len), K(deserialize_size));
    } else if (OB_FAIL(column_meta.deserialize(deserialize_buf, deserialize_size, deserialize_pos))) {
      LOG_WARN("fail to deserialize from str to build column meta", KR(ret), "column_meta_str", hex_str.ptr());
    } else if (deserialize_pos > deserialize_size) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("deserialize size overflow", KR(ret), K(deserialize_pos), K(deserialize_size));
    }
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::get_visible_column_meta(
    const ObTabletReplicaReportColumnMeta &column_meta,
    common::ObIAllocator &allocator,
    common::ObString &column_meta_visible_str)
{
  int ret = OB_SUCCESS;
  char *column_meta_str = NULL;
  const int64_t length = column_meta.get_string_length() * 2;
  int64_t pos = 0;

  if (OB_UNLIKELY(!column_meta.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("column meta is not valid", KR(ret), K(column_meta));
  } else if (OB_UNLIKELY(length > OB_MAX_LONGTEXT_LENGTH + 1)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("column meta too long", KR(ret), K(length), K(column_meta));
  } else if (OB_ISNULL(column_meta_str = static_cast<char *>(allocator.alloc(length)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc buf", KR(ret), K(length));
  } else if (FALSE_IT(pos = column_meta.get_string(column_meta_str, length))) {
    //nothing
  } else if (OB_UNLIKELY(pos >= length)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("size overflow", KR(ret), K(pos), K(length));
  } else {
    column_meta_visible_str.assign(column_meta_str, pos);
  }
  return ret;
}

int ObTabletReplicaChecksumOperator::get_hex_column_meta(
    const ObTabletReplicaReportColumnMeta &column_meta,
    common::ObIAllocator &allocator,
    common::ObString &column_meta_hex_str)
{
  int ret = OB_SUCCESS;
  char *serialize_buf = NULL;
  const int64_t serialize_size = column_meta.get_serialize_size();
  int64_t serialize_pos = 0;
  char *hex_buf = NULL;
  const int64_t hex_size = 2 * serialize_size;
  int64_t hex_pos = 0;
  if (OB_UNLIKELY(!column_meta.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("column_meta is invlaid", KR(ret), K(column_meta));
  } else if (OB_UNLIKELY(hex_size > OB_MAX_LONGTEXT_LENGTH + 1)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("format str is too long", KR(ret), K(hex_size), K(column_meta));
  } else if (OB_ISNULL(serialize_buf = static_cast<char *>(allocator.alloc(serialize_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc buf", KR(ret), K(serialize_size));
  } else if (OB_FAIL(column_meta.serialize(serialize_buf, serialize_size, serialize_pos))) {
    LOG_WARN("failed to serialize column meta", KR(ret), K(column_meta), K(serialize_size), K(serialize_pos));
  } else if (OB_UNLIKELY(serialize_pos > serialize_size)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("serialize error", KR(ret), K(serialize_pos), K(serialize_size));
  } else if (OB_ISNULL(hex_buf = static_cast<char*>(allocator.alloc(hex_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", KR(ret), K(hex_size));
  } else if (OB_FAIL(hex_print(serialize_buf, serialize_pos, hex_buf, hex_size, hex_pos))) {
    LOG_WARN("fail to print hex", KR(ret), K(serialize_pos), K(hex_size), K(serialize_buf));
  } else if (OB_UNLIKELY(hex_pos > hex_size)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("encode error", KR(ret), K(hex_pos), K(hex_size));
  } else {
    column_meta_hex_str.assign_ptr(hex_buf, hex_size);
  }
  return ret;
}

} // share
} // oceanbase
