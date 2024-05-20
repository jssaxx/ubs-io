/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */
#include "underfs.h"
#include "bio_log.h"
#include "bio_trace.h"
#include "bio_config_instance.h"
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#ifdef USE_DEBUG_TOOLS
#include "bio_tracepoint_helper.h"
#endif

namespace ock {
namespace bio {
#ifdef _ceph_Integrate
BResult UnderFs::Init()
{
    if (mInited) {
        return BIO_OK;
    }

    BioConfigPtr config = BioConfig::Instance();
    mCfgPath = config->GetUnderFsConfig().cfgPath;
    mCluster = config->GetUnderFsConfig().cluster;
    mUser = config->GetUnderFsConfig().user;
    mPool = config->GetUnderFsConfig().pools.at(0);

    int ret = rados_create2(&mConn, mCluster.c_str(), mUser.c_str(), 0);
    if (ret < 0 || mConn == nullptr) {
        LOG_ERROR("Failed to create, ret:" << ret);
        return BIO_UFS_IOERR;
    }

    ret = rados_conf_read_file(mConn, mCfgPath.c_str());
    if (ret < 0) {
        LOG_ERROR("Failed to read config, ret:" << ret);
        rados_shutdown(mConn);
        return BIO_UFS_IOERR;
    }

    ret = rados_connect(mConn);
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
    if (ret < 0) {
        LOG_ERROR("Failed to create ioctx, ret:" << ret);
        rados_shutdown(mConn);
        return BIO_UFS_IOERR;
    }

    LOG_INFO("UnderFS initialize succeed, path:" << mCfgPath << ", cluster:" << mCluster << ", user:" << mUser <<
        ", pool:" << mPool << ".");
    mInited = true;
    return BIO_OK;
}

void UnderFs::Stop()
{
    if (mIoCtx) {
        rados_ioctx_destroy(mIoCtx);
    }
    rados_shutdown(mConn);
    mInited = false;
}

BResult UnderFs::Put(const char *key, const char *value, const size_t len)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_INFO("UnderFs put key:" << key);

    BIO_TRACE_START(UFS_TRACE_PUT);
    LVOS_TP_START(SERVER_UNDERFS_PUT, &ret, -1)
    ret = rados_write(mIoCtx, key, value, len, 0);
    LVOS_TP_END
    BIO_TRACE_END(UFS_TRACE_PUT, ret);
    if (ret < 0) {
        LOG_ERROR("Failed to write object, ret:" << ret << ".");
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult UnderFs::Get(const char *key, char *value, const size_t len, const uint64_t off)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_INFO("UnderFs get key:" << key);

    BIO_TRACE_START(UFS_TRACE_GET);
    LVOS_TP_START(SERVER_UNDERFS_GET, &ret, -1)
    ret = rados_read(mIoCtx, key, value, len, off);
    LVOS_TP_END
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

BResult UnderFs::Delete(const char *key)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_INFO("UnderFs delete key:" << key);

    BIO_TRACE_START(UFS_TRACE_DEL);
    LVOS_TP_START(SERVER_UNDERFS_DELETE, &ret, -1)
    ret = rados_remove(mIoCtx, key);
    LVOS_TP_END
    BIO_TRACE_END(UFS_TRACE_DEL, ret);
    if (ret == -ENOENT) {
        BIO_TRACE_END(UFS_TRACE_DEL, BIO_NOT_EXISTS);
        LOG_WARN("Fail to check file, not exist, " << key);
        return BIO_NOT_EXISTS;
    }
    if (ret < 0) {
        LOG_ERROR("Failed to remove object, ret:" << ret);
        return BIO_UFS_IOERR;
    }
    return BIO_OK;
}

BResult UnderFs::Stat(const char *key, ObjStat &stat)
{
    int ret = BIO_UFS_IOERR;
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_INFO("UnderFs stat key:" << key);

    BIO_TRACE_START(UFS_TRACE_STAT);
    LVOS_TP_START(SERVER_UNDERFS_STAT, &ret, -1)
    ret = rados_stat(mIoCtx, key, &stat.size, &stat.time);
    LVOS_TP_END
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

BResult UnderFs::List(const char *prefix, std::unordered_map<std::string, UnderFs::ObjStat> &objStat)
{
    ChkTrue(mIoCtx != nullptr, BIO_NOT_READY, "Io context is nullptr, because of underFS not ready.");
    LOG_INFO("UnderFs list prefix:" << prefix);

    rados_list_ctx_t listCtx;
    int ret;
    LVOS_TP_START(SERVER_UNDERFS_LIST, &ret, -1)
    ret = rados_nobjects_list_open(mIoCtx, &listCtx);
    LVOS_TP_END
    if (ret < 0) {
        LOG_ERROR("Failed to list open, ret:" << ret);
        return BIO_UFS_IOERR;
    }

    BIO_TRACE_START(UFS_TRACE_LIST);
    char *entry = nullptr;
    while (rados_nobjects_list_next(listCtx, const_cast<const char **>(&entry), nullptr, nullptr) != (-ENOENT)) {
        if (memcmp(entry, prefix, strlen(prefix)) == 0) {
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
#else
BResult UnderFs::Init()
{
    static const std::string CEPH_PATH = "./ceph/";
    LVOS_TP_START(NO_PROCESS_UNDERFS_INIT, 0);
    if (mInited) {
        return BIO_OK;
    }
    LVOS_TP_END;

    DIR *dir = nullptr;
    mEmulationCephPath = CEPH_PATH;
    LVOS_TP_START(UNDERFS_OPEN_DIR_FAIL, &dir, nullptr);
    dir = opendir(mEmulationCephPath.c_str());
    LVOS_TP_END;
    if (dir == nullptr) {
        int status = mkdir(mEmulationCephPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        LVOS_TP_START(UNDERFS_MKDIR_FAIL, &status, BIO_UFS_IOERR);
        LVOS_TP_END;
        if (status == 0) {
            LOG_INFO("Succeed to create directory, " << mEmulationCephPath.c_str());
        } else {
            LOG_ERROR("Failed to create directory, " << mEmulationCephPath.c_str() << ", status:" << status);
            return BIO_UFS_IOERR;
        }
        dir = opendir(mEmulationCephPath.c_str());
    }

    LOG_INFO("UnderFS initialize succeed, emulation path:" << mEmulationCephPath << ".");
    closedir(dir);
    mInited = true;
    return BIO_OK;
}

void UnderFs::Stop()
{
    mInited = false;
}

BResult UnderFs::Put(const char *key, const char *value, const size_t len)
{
    std::string keyPath = mEmulationCephPath;
    keyPath += key;

    using namespace std;

    LOG_INFO("Put key:" << key);

    BIO_TRACE_START(UFS_TRACE_PUT);
    fstream file;
    file.open(keyPath.c_str(), ios::out | ios::binary);
    int isOpen = static_cast<int>(file.is_open());
    LVOS_TP_START(SERVER_UNDERFS_PUT, &isOpen, 0)
    LVOS_TP_END
    if (!isOpen) {
        LOG_ERROR("Fail to create file, " << keyPath.c_str());
        return BIO_UFS_IOERR;
    }

    file.write(value, len);
    file.close();
    BIO_TRACE_END(UFS_TRACE_PUT, 0);
    return BIO_OK;
}

BResult UnderFs::Get(const char *key, char *value, const size_t len, const uint64_t off)
{
    std::string keyPath = mEmulationCephPath;
    keyPath += key;

    using namespace std;

    LOG_INFO("Get key:" << key);

    BIO_TRACE_START(UFS_TRACE_GET);
    fstream file;
    file.open(keyPath.c_str(), ios::in);
    int isOpen = static_cast<int>(file.is_open());
    LVOS_TP_START(SERVER_UNDERFS_GET, &isOpen, 0)
    LVOS_TP_END
    if (!isOpen) {
        LOG_ERROR("Fail to open file, " << keyPath.c_str());
        return BIO_NOT_EXISTS;
    }

    file.seekg(off, ios::beg);
    file.read(value, len);
    file.close();
    BIO_TRACE_END(UFS_TRACE_GET, 0);
    return BIO_OK;
}

BResult UnderFs::Delete(const char *key)
{
    std::string keyPath = mEmulationCephPath;
    keyPath += key;

    BIO_TRACE_START(UFS_TRACE_DEL);
    std::ifstream infile(keyPath.c_str());
    int isGood = static_cast<int>(infile.good());
    LVOS_TP_START(SERVER_UNDERFS_DELETE, &isGood, 1);
    LVOS_TP_END;
    if (!isGood) {
        BIO_TRACE_END(UFS_TRACE_DEL, BIO_NOT_EXISTS);
        LOG_WARN("Fail to check file, not exist, " << keyPath.c_str());
        return BIO_NOT_EXISTS;
    }
    infile.close();

    int ret = remove(keyPath.c_str());
    LVOS_TP_START(UNDERFS_DELETE_ERR, &ret, BIO_ERR);
    LVOS_TP_END;
    if (ret != 0) {
        BIO_TRACE_END(UFS_TRACE_DEL, BIO_UFS_IOERR);
        LOG_ERROR("Fail to delete file, " << keyPath.c_str());
        return BIO_UFS_IOERR;
    }
    BIO_TRACE_END(UFS_TRACE_DEL, 0);
    return BIO_OK;
}

BResult UnderFs::Stat(const char *key, UnderFs::ObjStat &objStat)
{
    std::string keyPath = mEmulationCephPath;
    keyPath += key;

    BIO_TRACE_START(UFS_TRACE_STAT);
    struct stat file_stat;
    int ret = BIO_UFS_IOERR;
    LVOS_TP_START(SERVER_UNDERFS_STAT, &ret, BIO_UFS_IOERR)
    ret = stat(keyPath.c_str(), &file_stat);
    LVOS_TP_END
    if (ret != 0) {
        LOG_ERROR("Fail to check file, " << keyPath.c_str());
        BIO_TRACE_END(UFS_TRACE_STAT, BIO_NOT_EXISTS);
        return BIO_NOT_EXISTS;
    }
    objStat.size = file_stat.st_size;
    objStat.time = file_stat.st_ctime;
    BIO_TRACE_END(UFS_TRACE_STAT, 0);
    return BIO_OK;
}

BResult UnderFs::List(const char *prefix, std::unordered_map<std::string, UnderFs::ObjStat> &objStat)
{
    std::string keyPath = mEmulationCephPath;
    struct dirent *ptr;

    DIR *dir = opendir(keyPath.c_str());
    while ((ptr = readdir(dir)) != nullptr) {
        if (memcmp(ptr->d_name, prefix, strlen(prefix)) == 0) {
            struct stat file_stat;
            if (stat(((keyPath + ptr->d_name).c_str()), &file_stat) != 0) {
                LOG_ERROR("Fail to stat file " << (keyPath + ptr->d_name) << ", errorno " << errno << ".");
                continue;
            }
            UnderFs::ObjStat statInfo = { static_cast<uint32_t>(file_stat.st_size), file_stat.st_ctime };
            objStat.insert({ ptr->d_name, statInfo });
        }
    }
    closedir(dir);
    return BIO_OK;
}
#endif
}
}
