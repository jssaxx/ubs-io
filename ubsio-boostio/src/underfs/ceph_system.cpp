/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include "ceph_system.h"
#include <cstring>
#include "bio_config_instance.h"
#include "bio_log.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"
#include "underfs_config.h"

namespace ock {
namespace bio {
BResult CephSystem::Init()
{
    if (mInited) {
        return BIO_OK;
    }

    LoadCephConfig();
    int ret = BIO_UFS_IOERR;
    BIO_TP_START(UNDERFS_CEPH_CREAT_FAIL, &ret, -1);
    ret = rados_create2(&mConn, mCluster.c_str(), mUser.c_str(), 0);
    BIO_TP_END;
    if (ret < 0 || mConn == nullptr) {
        LOG_ERROR("Failed to create, ret:" << ret);
        return BIO_UFS_IOERR;
    }
    ret = rados_conf_read_file(mConn, mCfgPath.c_str());
    BIO_TP_START(UNDERFS_CEPH_READ_FILE_FAIL, &ret, -1);
    BIO_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to read config, ret:" << ret);
        rados_shutdown(mConn);
        return BIO_UFS_IOERR;
    }

    ret = rados_connect(mConn);
    BIO_TP_START(UNDERFS_CEPH_CONNECT_FAIL, &ret, -1);
    BIO_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to connect, ret:" << ret);
        rados_shutdown(mConn);
        return BIO_UFS_IOERR;
    }

    if (rados_pool_lookup(mConn, mPool.c_str()) < 0) {
        if (rados_pool_create(mConn, mPool.c_str()) < 0) {
            LOG_ERROR("Failed to create pool, ret:" << ret);
            rados_shutdown(mConn);
            return BIO_UFS_IOERR;
        }
    }

    ret = rados_ioctx_create(mConn, mPool.c_str(), &mIoCtx);
    BIO_TP_START(UNDERFS_CEPH_IOCTX_CREAT_FAIL, &ret, -1);
    BIO_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to create ioctx, ret:" << ret);
        rados_shutdown(mConn);
        return BIO_UFS_IOERR;
    }

    LOG_INFO("UnderFS initialize succeed, cluster:" << mCluster << ", user:" << mUser << ", pool:" << mPool << ".");
    mInited = true;
    return BIO_OK;
}

void CephSystem::Stop()
{
    if (mIoCtx) {
        rados_ioctx_destroy(mIoCtx);
    }
    rados_shutdown(mConn);
    mInited = false;
}

BResult CephSystem::Put(const char *key, const char *value, const size_t len)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs put key:" << key);

    BIO_TRACE_START(UFS_TRACE_PUT);
    BIO_TP_START(SERVER_UNDERFS_PUT, &ret, -1);
    ret = rados_write(mIoCtx, key, value, len, 0);
    BIO_TP_END;
    BIO_TRACE_END(UFS_TRACE_PUT, ret);
    if (ret < 0) {
        LOG_ERROR("Failed to write object, ret:" << ret << ".");
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult CephSystem::Get(const char *key, char *value, const size_t len, const uint64_t off)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs get key:" << key);

    BIO_TRACE_START(UFS_TRACE_GET);
    BIO_TP_START(SERVER_UNDERFS_GET, &ret, -1);
    ret = rados_read(mIoCtx, key, value, len, off);
    BIO_TP_END;
    BIO_TP_START(UNDERFS_CEPH_GET_FAIL, &ret, (-ENOENT));
    BIO_TP_END;
    int res = (ret < 0) ? BIO_UFS_IOERR : BIO_OK;
    BIO_TRACE_END(UFS_TRACE_GET, res);
    if (ret == -ENOENT) {
        LOG_WARN("Fail to get object " << key << ", not exist.");
        return BIO_NOT_EXISTS;
    }
    if (ret < 0) {
        LOG_ERROR("Failed to read object " << key << ", ret:" << ret);
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult CephSystem::Delete(const char *key)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs delete key:" << key);

    BIO_TRACE_START(UFS_TRACE_DEL);
    BIO_TP_START(SERVER_UNDERFS_DELETE, &ret, -1);
    ret = rados_remove(mIoCtx, key);
    BIO_TP_END;
    BIO_TP_START(UNDERFS_CEPH_DELETE_NOT_EXIST, &ret, (-ENOENT));
    BIO_TP_END;
    BIO_TRACE_END(UFS_TRACE_DEL, ret);
    if (ret == -ENOENT) {
        LOG_WARN("Fail to check file, not exist, " << key);
        return BIO_NOT_EXISTS;
    }
    if (ret < 0) {
        LOG_ERROR("Failed to remove object, ret:" << ret);
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult CephSystem::Stat(const char *key, ObjStat &stat)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs stat key:" << key);

    BIO_TRACE_START(UFS_TRACE_STAT);
    BIO_TP_START(SERVER_UNDERFS_STAT, &ret, -1);
    ret = rados_stat(mIoCtx, key, &stat.size, &stat.time);
    BIO_TP_END;
    BIO_TP_START(UNDERFS_CEPH_STAT_NOT_EXIST, &ret, (-ENOENT));
    BIO_TP_END;
    BIO_TRACE_END(UFS_TRACE_STAT, ret);
    if (ret == -ENOENT) {
        LOG_WARN("Fail to stat object " << key << ", not exist.");
        return BIO_NOT_EXISTS;
    }
    if (ret < 0) {
        LOG_ERROR("Failed to stat object " << key << ", ret:" << ret);
        return BIO_UFS_IOERR;
    }
    BIO_TP_START(SERVER_UNDERFS_STAT_SIZE, &stat.size, IO_MAX_LEN + 1);
    BIO_TP_END;
    if (stat.size > IO_MAX_LEN) {
        LOG_ERROR("invalid file size: " << stat.size << ".");
        return BIO_NOT_EXISTS;
    }
    return BIO_OK;
}

BResult CephSystem::List(const char *prefix, std::unordered_map<std::string, CephSystem::ObjStat> &objStat)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs list prefix:" << prefix);

    rados_list_ctx_t listCtx;
    BIO_TP_START(SERVER_UNDERFS_LIST, &ret, -1);
    ret = rados_nobjects_list_open(mIoCtx, &listCtx);
    BIO_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to list open, ret:" << ret);
        return BIO_UFS_IOERR;
    }

    BIO_TRACE_START(UFS_TRACE_LIST);
    char *entry = nullptr;
    size_t prefixLength = strlen(prefix);
    while (rados_nobjects_list_next(listCtx, const_cast<const char **>(&entry), nullptr, nullptr) == 0) {
        if (memcmp(entry, prefix, prefixLength) == 0) {
            ObjStat objectStat;
            ret = this->Stat(entry, objectStat);
            if (ret != 0) {
                LOG_ERROR("Fail to stat object " << entry << ", ret: " << ret << ".");
                continue;
            }
            objStat.insert({entry, objectStat});
        }
    }
    rados_nobjects_list_close(listCtx);
    BIO_TRACE_END(UFS_TRACE_LIST, ret);
    return BIO_OK;
}

void CephSystem::LoadCephConfig()
{
    auto instance = UnderFsConfig::Instance();
    if (instance == nullptr) {
        return;
    }
    BioConfig::UnderFsConfig config = instance->GetUnderFsConfig();
    mCfgPath = config.cephConfig.cfgPath;
    mCluster = config.cephConfig.cluster;
    mUser = config.cephConfig.user;
    mPool = config.cephConfig.pools.at(0);
}
} // namespace bio
} // namespace ock