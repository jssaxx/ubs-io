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

#ifndef BOOST_IO_INTERCEPTOR_FD_H
#define BOOST_IO_INTERCEPTOR_FD_H

#include <bitset>
#include <string>
#include <unistd.h>
#include <memory>
#include <unordered_map>
#include <functional>
#include <vector>
#include <utility>
#include <list>
#include <atomic>
#include "bio_lock.h"

namespace ock {
namespace bio {
class OpenFile {
public:
    OpenFile(int fd, uint64_t inode) : m_fd(fd), m_inode(inode) {}

    virtual ~OpenFile() {}

    inline uint64_t GetActualFd() const
    {
        return m_fd;
    }

    inline uint64_t GetInode() const
    {
        return m_inode;
    }

private:
    int m_fd;
    uint64_t m_inode;
};

class OpenFileMap {
public:
    OpenFileMap() = default;

    virtual ~OpenFileMap();

    OpenFileMap(const OpenFileMap &) = delete;

    OpenFileMap &operator = (const OpenFileMap &) = delete;

    bool Exist(int fd);

    bool Add(int fd, std::shared_ptr<OpenFile> &&file);

    std::shared_ptr<OpenFile> &At(int fd);

    void Erase(int fd);

private:
    ReadWriteLock filesMtx;
    std::unordered_map<int, std::shared_ptr<OpenFile>> files;
};
}
}
#endif // BOOST_IO_INTERCEPTOR_FD_H
