/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include "ceph_system.h"
#include <cstring>
#include "bio_tracepoint_helper.h"
#include "bio_config_instance.h"
#include "bio_log.h"
#include "bio_trace.h"
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
    LVOS_TP_START(UNDERFS_CEPH_CREAT_FAIL, &ret, -1);
    ret = rados_create2(&mConn, mCluster.c_str(), mUser.c_str(), 0);
    LVOS_TP_END;
    if (ret < 0 || mConn == nullptr) {
        LOG_ERROR("Failed to create, ret:" << ret);
        return BIO_UFS_IOERR;
    }
    ret = rados_conf_read_file(mConn, mCfgPath.c_str());
    LVOS_TP_START(UNDERFS_CEPH_READ_FILE_FAIL, &ret, -1);
    LVOS_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to read config, ret:" << ret);
        rados_shutdown(mConn);
        return BIO_UFS_IOERR;
    }

    ret = rados_connect(mConn);
    LVOS_TP_START(UNDERFS_CEPH_CONNECT_FAIL, &ret, -1);
    LVOS_TP_END;
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
    LVOS_TP_START(UNDERFS_CEPH_IOCTX_CREAT_FAIL, &ret, -1);
    LVOS_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to create ioctx, ret:" << ret);
        rados_shutdown(mConn);
        return BIO_UFS_IOERR;
    }

    LOG_INFO("UnderFS initialize succeed, cluster:" << mCluster << ", user:" << mUser <<
        ", pool:" << mPool << ".");
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
    LVOS_TP_START(SERVER_UNDERFS_PUT, &ret, -1);
    ret = rados_write(mIoCtx, key, value, len, 0);
    LVOS_TP_END;
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
    LVOS_TP_START(SERVER_UNDERFS_GET, &ret, -1);
    ret = rados_read(mIoCtx, key, value, len, off);
    LVOS_TP_END;
    LVOS_TP_START(UNDERFS_CEPH_GET_FAIL, &ret, (-ENOENT));
    LVOS_TP_END;
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
    LVOS_TP_START(SERVER_UNDERFS_DELETE, &ret, -1);
    ret = rados_remove(mIoCtx, key);
    LVOS_TP_END;
    LVOS_TP_START(UNDERFS_CEPH_DELETE_NOT_EXIST, &ret, (-ENOENT));
    LVOS_TP_END;
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
    LVOS_TP_START(SERVER_UNDERFS_STAT, &ret, -1);
    ret = rados_stat(mIoCtx, key, &stat.size, &stat.time);
    LVOS_TP_END;
    LVOS_TP_START(UNDERFS_CEPH_STAT_NOT_EXIST, &ret, (-ENOENT));
    LVOS_TP_END;
    BIO_TRACE_END(UFS_TRACE_STAT, ret);
    if (ret == -ENOENT) {
        LOG_WARN("Fail to stat object " << key << ", not exist.");
        return BIO_NOT_EXISTS;
    }
    if (ret < 0) {
        LOG_ERROR("Failed to stat object " << key << ", ret:" << ret);
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult CephSystem::List(const char *prefix, std::unordered_map<std::string, CephSystem::ObjStat> &objStat)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_DEBUG("UnderFs list prefix:" << prefix);

    rados_list_ctx_t listCtx;
    LVOS_TP_START(SERVER_UNDERFS_LIST, &ret, -1);
    ret = rados_nobjects_list_open(mIoCtx, &listCtx);
    LVOS_TP_END;
    if (ret < 0) {
        LOG_ERROR("Failed to list open, ret:" << ret);
        return BIO_UFS_IOERR;
    }

    BIO_TRACE_START(UFS_TRACE_LIST);
    char *entry = nullptr;
    size_t prefixLength = strlen(prefix);
    while (rados_nobjects_list_next(listCtx, const_cast<const char **>(&entry), nullptr, nullptr) != (-ENOENT)) {
        if (memcmp(entry, prefix, prefixLength) == 0) {
            ObjStat objectStat;
            ret = this->Stat(entry, objectStat);
            if (ret != 0) {
                LOG_ERROR("Fail to stat object " << entry << ", ret: " << ret << ".");
                continue;
            }
            objStat.insert({ entry, objectStat });
        }
    }
    rados_nobjects_list_close(listCtx);
    BIO_TRACE_END(UFS_TRACE_LIST, ret);
    return BIO_OK;
}

void CephSystem::LoadCephConfig()
{
    BioConfig::UnderFsConfig config = UnderFsConfig::Instance()->GetUnderFsConfig();
    mCfgPath = config.cephConfig.cfgPath;
    mCluster = config.cephConfig.cluster;
    mUser = config.cephConfig.user;
    mPool = config.cephConfig.pools.at(0);
}
}
}