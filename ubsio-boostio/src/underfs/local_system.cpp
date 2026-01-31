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

#include "local_system.h"
#include <iostream>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"
#include "bio_str_util.h"
#include "bio_err.h"
#include "bio_log.h"
#include "bio_types.h"


namespace ock {
namespace bio {
BResult LocalSystem::Init()
{
    static const std::string CEPH_PATH = "./ceph/";
    BIO_TP_START(NO_PROCESS_UNDERFS_INIT, 0);
    if (mInited) {
        return BIO_OK;
    }
    BIO_TP_END;

    DIR *dir = nullptr;
    mEmulationCephPath = CEPH_PATH;
    BIO_TP_START(UNDERFS_OPEN_DIR_FAIL, &dir, nullptr);
    dir = opendir(mEmulationCephPath.c_str());
    BIO_TP_END;
    if (dir == nullptr) {
        int status = BIO_UFS_IOERR;
        BIO_TP_START(UNDERFS_MKDIR_FAIL, &status, BIO_UFS_IOERR);
        status = mkdir(mEmulationCephPath.c_str(), S_IRWXU | S_IRGRP | S_IXGRP);
        BIO_TP_END;
        if (status == BIO_OK) {
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

void LocalSystem::Stop()
{
    mInited = false;
}

BResult LocalSystem::Put(const char *key, const char *value, const size_t len)
{
    using namespace std;

    std::string keyPath = mEmulationCephPath;
    keyPath += key;
    std::vector<std::string> list;
    StrUtil::Split(keyPath, "/", list);
    if (list.size() > NO_3) {
        std::string prefix = mEmulationCephPath;
        for (uint32_t i = NO_2; i < list.size() - NO_1; i++) {
            prefix += list[i];
            prefix += "/";
            auto ret = mkdir(prefix.c_str(), S_IRWXU | S_IRGRP | S_IXGRP);
            if (ret != BIO_OK) { return BIO_ERR; }
        }
    }

    LOG_DEBUG("Put key:" << key);

    BIO_TRACE_START(UFS_TRACE_PUT);
    fstream file;
    file.open(keyPath.c_str(), ios::out | ios::binary);
    int isOpen = static_cast<int>(file.is_open());
    BIO_TP_START(SERVER_UNDERFS_PUT, &isOpen, 0)
    BIO_TP_END;
    if (!isOpen) {
        LOG_ERROR("Fail to create file, " << keyPath.c_str());
        return BIO_UFS_IOERR;
    }

    file.write(value, len);
    file.close();
    BIO_TRACE_END(UFS_TRACE_PUT, 0);
    return BIO_OK;
}

BResult LocalSystem::Get(const char *key, char *value, const size_t len, const uint64_t off)
{
    std::string keyPath = mEmulationCephPath;
    keyPath += key;

    char *canonicalPath = realpath(keyPath.c_str(), nullptr);
    if (canonicalPath == nullptr) {
        LOG_WARN("Fail to check file, not exist, " << keyPath.c_str());
        return BIO_NOT_EXISTS;
    }

    using namespace std;

    LOG_DEBUG("Get key:" << key);

    BIO_TRACE_START(UFS_TRACE_GET);
    fstream file;
    file.open(canonicalPath, ios::in);
    free(canonicalPath);
    canonicalPath = nullptr;
    int isOpen = static_cast<int>(file.is_open());
    BIO_TP_START(SERVER_UNDERFS_GET, &isOpen, 0)
    BIO_TP_END;
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

BResult LocalSystem::Delete(const char *key)
{
    std::string keyPath = mEmulationCephPath;
    keyPath += key;

    char *canonicalPath = realpath(keyPath.c_str(), nullptr);
    if (canonicalPath == nullptr) {
        LOG_WARN("Fail to check file, not exist, " << keyPath.c_str());
        return BIO_NOT_EXISTS;
    }

    BIO_TRACE_START(UFS_TRACE_DEL);
    std::ifstream infile(canonicalPath);
    int isGood = static_cast<int>(infile.good());
    BIO_TP_START(SERVER_UNDERFS_DELETE, &isGood, 1);
    BIO_TP_END;
    if (!isGood) {
        BIO_TRACE_END(UFS_TRACE_DEL, BIO_NOT_EXISTS);
        LOG_WARN("Fail to check file, not exist, " << keyPath.c_str());
        free(canonicalPath);
        canonicalPath = nullptr;
        return BIO_NOT_EXISTS;
    }
    infile.close();

    int ret = -1;
    BIO_TP_START(UNDERFS_DELETE_ERR, &ret, BIO_ERR);
    ret = remove(canonicalPath);
    BIO_TP_END;
    free(canonicalPath);
    canonicalPath = nullptr;
    if (ret != BIO_OK) {
        BIO_TRACE_END(UFS_TRACE_DEL, BIO_UFS_IOERR);
        LOG_ERROR("Fail to delete file, " << keyPath.c_str());
        return BIO_UFS_IOERR;
    }
    BIO_TRACE_END(UFS_TRACE_DEL, 0);
    return BIO_OK;
}

BResult LocalSystem::Stat(const char *key, ObjStat &objStat)
{
    std::string keyPath = mEmulationCephPath;
    keyPath += key;

    BIO_TRACE_START(UFS_TRACE_STAT);
    struct stat fileStat;
    int ret = BIO_UFS_IOERR;
    BIO_TP_START(SERVER_UNDERFS_STAT, &ret, BIO_UFS_IOERR);
    ret = stat(keyPath.c_str(), &fileStat);
    BIO_TP_END;
    if (ret != 0) {
        LOG_ERROR("Fail to check file, " << keyPath.c_str());
        BIO_TRACE_END(UFS_TRACE_STAT, BIO_NOT_EXISTS);
        return BIO_NOT_EXISTS;
    }

    if (fileStat.st_size < 0) {
        LOG_ERROR("invalid file size: " << fileStat.st_size << ".");
        return BIO_NOT_EXISTS;
    }
    objStat.size = static_cast<uint64_t>(fileStat.st_size);
    objStat.time = fileStat.st_ctime;
    BIO_TRACE_END(UFS_TRACE_STAT, 0);
    return BIO_OK;
}

BResult LocalSystem::List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat)
{
    std::string keyPath = mEmulationCephPath;
    struct dirent *ptr;

    DIR *dir = opendir(keyPath.c_str());
    if (dir == nullptr) {
        LOG_ERROR("Fail to open directory: " << keyPath.c_str() << ".");
        return BIO_UFS_IOERR;
    }
    size_t prefixLen = strlen(prefix);
    while ((ptr = readdir(dir)) != nullptr) {
        if (memcmp(ptr->d_name, prefix, prefixLen) == 0) {
            struct stat fileStat;
            if (stat(((keyPath + ptr->d_name).c_str()), &fileStat) != 0) {
                LOG_ERROR("Fail to stat file " << (keyPath + ptr->d_name) << ", errorno " << errno << ".");
                continue;
            }
            ObjStat statInfo = { static_cast<uint32_t>(fileStat.st_size), fileStat.st_ctime };
            objStat.insert({ ptr->d_name, statInfo });
        }
    }
    closedir(dir);
    return BIO_OK;
}
}
}