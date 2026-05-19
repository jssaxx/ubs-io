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

std::shared_ptr<OpenFile> OpenFileMap::At(int fd)
{
    filesMtx.LockRead();
    auto iter = files.find(fd);
    if (iter != files.end()) {
        auto file = iter->second;
        filesMtx.UnLock();
        return file;
    }
    filesMtx.UnLock();
    return nullptr;
}

const std::shared_ptr<OpenFile> &OpenFileMap::AtCached(int fd)
{
    struct CachedOpenFile {
        int fd = -1;
        std::shared_ptr<OpenFile> file = nullptr;
    };
    static thread_local CachedOpenFile cachedFile;

    if (cachedFile.fd == fd && cachedFile.file != nullptr && cachedFile.file->IsActive()) {
        return cachedFile.file;
    }

    auto file = At(fd);
    cachedFile.fd = fd;
    cachedFile.file = std::move(file);
    return cachedFile.file;
}

std::vector<std::pair<int, std::shared_ptr<OpenFile>>> OpenFileMap::Snapshot()
{
    std::vector<std::pair<int, std::shared_ptr<OpenFile>>> snapshot;
    filesMtx.LockRead();
    snapshot.reserve(files.size());
    for (auto &item : files) {
        snapshot.emplace_back(item.first, item.second);
    }
    filesMtx.UnLock();
    return snapshot;
}

void OpenFileMap::Erase(int fd)
{
    filesMtx.LockWrite();
    auto iter = files.find(fd);
    if (iter != files.end()) {
        iter->second->Deactivate();
        files.erase(iter);
    }
    filesMtx.UnLock();
}
