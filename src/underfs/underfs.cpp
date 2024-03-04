/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */
#include "underfs.h"
#include "bio_log.h"
#include "bio_trace.h"
#include "bio_functions.h"
#include "message.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <iostream>
#include <fstream>

namespace ock {
namespace bio {
#ifdef _ceph_Integrate
BResult UnderFs::Init()
{
    std::string cfg = "/etc/ceph/ceph.conf";
    int ret;

    if (mInited) {
        return BIO_OK;
    }

    ret = rados_create2(&mConn, mCluster.c_str(), mUser.c_str(), 0);
    if (ret < 0 || mConn == nullptr) {
        LOG_ERROR("Failed to create, ret:" << ret);
        return BIO_ERR;
    }

    ret = rados_conf_read_file(mConn, cfg.c_str());
    if (ret < 0) {
        LOG_ERROR("Failed to read config, ret:" << ret);
        rados_shutdown(mConn);
        return BIO_ERR;
    }

    ret = rados_connect(mConn);
    if (ret < 0) {
        LOG_ERROR("Failed to connect, ret:" << ret);
        rados_shutdown(mConn);
        return BIO_ERR;
    }

    if (rados_pool_lookup(mConn, mPool.c_str()) < 0) {
        if (rados_pool_create(mConn, mPool.c_str()) < 0) {
            LOG_ERROR("Failed to create pool, ret:" << ret);
            rados_shutdown(mConn);
            return BIO_ERR;
        }
    }

    ret = rados_ioctx_create(mConn, mPool.c_str(), &mIoCtx);
    if (ret < 0) {
        LOG_ERROR("Failed to create ioctx, ret:" << ret);
        rados_shutdown(mConn);
        return BIO_ERR;

    }

    LOG_INFO("Underfs init succeed");
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
    return;
}

BResult UnderFs::Put(const char *key, const char *value, const size_t len)
{
    int ret;

    ChkTrueNot(mIoCtx != nullptr, BIO_NOT_READY);
    BIO_TRACE_START(UFS_TRACE_PUT);
    ret = rados_write(mIoCtx, key, value, len, 0);
    BIO_TRACE_END(UFS_TRACE_PUT, ret);
    if (ret < 0) {
        LOG_ERROR("Failed to write object, ret:" << ret);
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult UnderFs::Get(const char *key, char *value, const size_t len, const uint64_t off)
{
    int ret;

    ChkTrueNot(mIoCtx != nullptr, BIO_NOT_READY);
    BIO_TRACE_START(UFS_TRACE_GET);
    ret = rados_read(mIoCtx, key, value, len, off);
    BIO_TRACE_END(UFS_TRACE_GET, ret);
    if (ret < 0) {
        LOG_ERROR("Failed to read object, ret:" << ret);
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult UnderFs::Delete(const char *key)
{
    int ret;

    ChkTrueNot(mIoCtx != nullptr, BIO_NOT_READY);
    BIO_TRACE_START(UFS_TRACE_DEL);
    ret = rados_remove(mIoCtx, key);
    BIO_TRACE_END(UFS_TRACE_DEL, ret);
    if (ret == -ENOENT) {
        BIO_TRACE_END(UFS_TRACE_DEL, BIO_NOT_EXISTS);
        LOG_WARN("Fail to check file, not exist, " << key);
        return BIO_NOT_EXISTS;
    }
    if (ret < 0) {
        LOG_ERROR("Failed to remove object, ret:" << ret);
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult UnderFs::Stat(const char *key, ObjStat &stat)
{
    int ret;

    ChkTrueNot(mIoCtx != nullptr, BIO_NOT_READY);
    BIO_TRACE_START(UFS_TRACE_STAT);
    ret = rados_stat(mIoCtx, key, &stat.size, &stat.mTime);
    BIO_TRACE_END(UFS_TRACE_STAT, ret);
    if (ret < 0) {
        LOG_ERROR("Failed to stat object, ret:" << ret);
        return BIO_ERR;
    }
    return BIO_OK;
}

BResult UnderFs::List(const char *prefix, std::vector<ObjStat> &objStat)
{
    ChkTrueNot(mIoCtx != nullptr, BIO_NOT_READY);

    rados_list_ctx_t *listCtx = nullptr;
    int ret = rados_nobjects_list_open(mIoCtx, listCtx);
    if (ret < 0) {
        LOG_ERROR("Failed to list open, ret:" << ret);
        return BIO_ERR;
    }

    BIO_TRACE_START(UFS_TRACE_LIST);
    char *entry = nullptr;
    while (rados_nobjects_list_next(listCtx, &entry, nullptr, nullptr) != (-ENOENT)) {
        LOG_INFO("List result, entry:" << entry);
    }
    BIO_TRACE_END(UFS_TRACE_LIST, ret);
    return BIO_OK;
}
#else
BResult UnderFs::Init()
{
    static std::string cephPath = CEPH_PATH;

    if (mInited) {
        return BIO_OK;
    }

    DIR* dir = opendir(cephPath.c_str());
    if (dir == nullptr) {
        int status = mkdir(cephPath.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (status == 0) {
            LOG_INFO("Succeed to create directory, " << cephPath.c_str());
        } else {
            LOG_ERROR("Failed to create directory, " << cephPath.c_str() << ", status:" << status);
            return BIO_ERR;
        }
    } else {
        LOG_INFO("Exist to check directory, " << cephPath.c_str());
        closedir(dir);
    }

    LOG_INFO("Underfs init succeed");
    mInited = true;
    return BIO_OK;
}

void UnderFs::Stop()
{
    mInited = false;
    return;
}

BResult UnderFs::Put(const char *key, const char *value, const size_t len)
{
    std::string keyPath = CEPH_PATH_EXT;
    keyPath += key;

    using namespace std;

    LOG_INFO("Put key:" << key);

    BIO_TRACE_START(UFS_TRACE_PUT);
    fstream file;
    file.open(keyPath.c_str(), ios::out | ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Fail to create file, " << keyPath.c_str());
        return BIO_ERR;
    }

    file.write(value, len);
    file.close();
    BIO_TRACE_END(UFS_TRACE_PUT, 0);
    return BIO_OK;
}

BResult UnderFs::Get(const char *key, char *value, const size_t len, const uint64_t off)
{
    std::string keyPath = CEPH_PATH_EXT;
    keyPath += key;

    using namespace std;

    LOG_INFO("Get key:" << key);

    BIO_TRACE_START(UFS_TRACE_GET);
    fstream file;
    file.open(keyPath.c_str(), ios::in);
    if (!file.is_open()) {
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
    std::string keyPath = CEPH_PATH_EXT;
    keyPath += key;

    using namespace std;

    LOG_INFO("Del key:" << key);

    BIO_TRACE_START(UFS_TRACE_DEL);
    ifstream infile(keyPath.c_str());
    if (!infile.good()) {
        BIO_TRACE_END(UFS_TRACE_DEL, BIO_NOT_EXISTS);
        LOG_WARN("Fail to check file, not exist, " << keyPath.c_str());
        return BIO_NOT_EXISTS;
    }
    infile.close();

    //删除文件
    if (remove(keyPath.c_str()) != 0) {
        BIO_TRACE_END(UFS_TRACE_DEL, BIO_ERR);
        LOG_ERROR("Fail to delete file, " << keyPath.c_str());
        return BIO_ERR;
    }
    BIO_TRACE_END(UFS_TRACE_DEL, 0);
    return BIO_OK;
}

BResult UnderFs::Stat(const char *key, ObjStat &objStat)
{
    std::string keyPath = CEPH_PATH_EXT;
    keyPath += key;

    using namespace std;

    BIO_TRACE_START(UFS_TRACE_STAT);
    struct stat file_stat;
    if (stat(keyPath.c_str(), &file_stat) != 0) {
        LOG_ERROR("Fail to check file, " << keyPath.c_str());
        return BIO_NOT_EXISTS;
    }
    CopyKey(objStat.key, key, KEY_MAX_SIZE);
    objStat.size = file_stat.st_size;
    objStat.time = file_stat.st_ctime;
    BIO_TRACE_END(UFS_TRACE_STAT, 0);
    return BIO_OK;
}

BResult UnderFs::List(const char *prefix, std::vector<ObjStat> &objStat)
{
    std::string keyPath = CEPH_PATH_EXT;
    struct dirent *ptr;
    DIR *dir = opendir(keyPath.c_str());
    while ((ptr = readdir(dir)) != nullptr) {
        if (memcmp(ptr->d_name, prefix, strlen(prefix)) == 0) {
            struct stat file_stat;
            if (stat(keyPath.c_str(), &file_stat) != 0) {
                LOG_ERROR("Fail to check file, " << keyPath.c_str());
                continue;
            }
            ObjStat statInfo;
            CopyKey(statInfo.key, ptr->d_name, KEY_MAX_SIZE);
            statInfo.size = file_stat.st_size;
            statInfo.time = file_stat.st_ctime;
            objStat.push_back(statInfo);
        }
    }
    closedir(dir);
    return BIO_OK;
}

#endif
}
}
