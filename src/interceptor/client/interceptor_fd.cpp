/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#include "interceptor_fd.h"

using namespace ock::bio;

OpenFileMap::~OpenFileMap()
{
    filesMtx.LockWrite();
    files.clear();
    filesMtx.UnLock();
}

bool OpenFileMap::Exist(int fd)
{
    filesMtx.LockRead();
    auto iter = files.find(fd);
    if (iter != files.end()) {
        filesMtx.UnLock();
        return true;
    }
    filesMtx.UnLock();
    return false;
}

bool OpenFileMap::Add(int fd, std::shared_ptr<OpenFile> &&file)
{
    filesMtx.LockWrite();
    auto &&ret = files.emplace(fd, std::move(file));
    filesMtx.UnLock();
    return ret.second;
}

std::shared_ptr<OpenFile> &OpenFileMap::At(int fd)
{
    static std::shared_ptr<OpenFile> nullRef;
    filesMtx.LockRead();
    auto iter = files.find(fd);
    if (iter != files.end()) {
        filesMtx.UnLock();
        return iter->second;
    }
    filesMtx.UnLock();
    return nullRef;
}

void OpenFileMap::Erase(int fd)
{
    filesMtx.LockWrite();
    files.erase(fd);
    filesMtx.UnLock();
}
