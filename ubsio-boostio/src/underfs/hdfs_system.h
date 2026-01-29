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

#ifndef BOOSTIO_HDFSSYSTEM_H
#define BOOSTIO_HDFSSYSTEM_H

#include <dlfcn.h>
#include "file_system.h"
#include "hdfs.h"

namespace ock {
namespace bio {
class HdfsSystem : public FileSystem {
public:
    ~HdfsSystem()
    {
        if (handler != nullptr) {
            dlclose(handler);
        }
    }

    BResult Init() override;

    void Stop() override;

    BResult Put(const char *key, const char *value, const size_t len) override;

    BResult Get(const char *key, char *value, const size_t len, const uint64_t off) override;

    BResult Delete(const char *key) override;

    BResult Stat(const char *key, ObjStat &objStat) override;

    BResult List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) override;

private:
    void *LoadFunction(const char *name);
    BResult InitOperations();
    BResult LoadHdfsLibrary();
    BResult LoadHdfsConfig();
    BResult CreateDirectory(const std::string path);
    BResult ListDirectoryRecursive(const char *path, std::unordered_map<std::string, HdfsSystem::ObjStat> &objStat);
    BResult ListFilesInDirectory(const char *prefix, std::unordered_map<std::string, HdfsSystem::ObjStat> &objStat);
    bool IsEmptyDirectorie(const std::string &path);
    void DeleteEmptyDirImpl(const std::string &path);
    void DeleteEmptyDirectories(const std::string &path);

private:
    using CreateDirFuncPtr = int (*)(hdfsFS, const char *);
    using SetWorkingDirFuncPtr = int (*)(hdfsFS, const char *);
    using DisconnectFuncPtr = int (*)(hdfsFS);
    using StreamBuilderAllocFuncPtr = hdfsStreamBuilder *(*)(hdfsFS, const char *, int);
    using StreamBuilderBuildFuncPtr = hdfsFile (*)(hdfsStreamBuilder *);
    using WriteFuncPtr = int32_t (*)(hdfsFS, hdfsFile, const void *, int32_t);
    using FlushFuncPtr = int (*)(hdfsFS, hdfsFile);
    using CloseFileFuncPtr = int (*)(hdfsFS, hdfsFile);
    using PreadFuncPtr = int32_t (*)(hdfsFS, hdfsFile, int64_t, void *, int32_t);
    using DeleteFuncPtr = int (*)(hdfsFS, const char *, int);
    using ExistsFuncPtr = int (*)(hdfsFS, const char *);
    using GetPathInfoFuncPtr = hdfsFileInfo *(*)(hdfsFS, const char *);
    using FreeFileInfoFuncPtr = void (*)(hdfsFileInfo *, int);
    using ListDirectoryFuncPtr = hdfsFileInfo *(*)(hdfsFS, const char *, int *);
    using NewBuilderFuncPtr = hdfsBuilder *(*)(void);
    using BuilderSetNameNodeFuncPtr = void (*)(hdfsBuilder *, const char *);
    using BuilderSetNameNodePortFuncPtr = void (*)(hdfsBuilder *, uint16_t);
    using BuilderConnectFuncPtr = hdfsFS (*)(hdfsBuilder *);

private:
    void *handler = nullptr;
    CreateDirFuncPtr createDirOp = nullptr;
    SetWorkingDirFuncPtr setWorkingDirOp = nullptr;
    DisconnectFuncPtr disconnectOp = nullptr;
    StreamBuilderAllocFuncPtr streamBuilderAllocOp = nullptr;
    StreamBuilderBuildFuncPtr streamBuilderBuildOp = nullptr;
    WriteFuncPtr writeOp = nullptr;
    FlushFuncPtr flushOp = nullptr;
    CloseFileFuncPtr closeFileOp = nullptr;
    PreadFuncPtr preadOp = nullptr;
    DeleteFuncPtr deleteOp = nullptr;
    ExistsFuncPtr existsOp = nullptr;
    GetPathInfoFuncPtr getPathInfoOp = nullptr;
    FreeFileInfoFuncPtr freeFileInfoOp = nullptr;
    ListDirectoryFuncPtr listDirectoryOp = nullptr;
    NewBuilderFuncPtr newBuilderOp = nullptr;
    BuilderSetNameNodeFuncPtr builderSetNameNodeOp = nullptr;
    BuilderSetNameNodePortFuncPtr builderSetNameNodePortOp = nullptr;
    BuilderConnectFuncPtr builderConnectOp = nullptr;

private:
    hdfsFS mHdfsFs = nullptr;
    std::string mNameNodeIp;
    uint16_t mNameNodePort = 0;
    std::string mHdfsWorkingPath;
};
}
}


#endif // BOOSTIO_HDFSSYSTEM_H
