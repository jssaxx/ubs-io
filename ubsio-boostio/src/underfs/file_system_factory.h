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

#ifndef BOOSTIO_FILESYSTEMFACTORY_H
#define BOOSTIO_FILESYSTEMFACTORY_H

#include <functional>
#include <memory>
#include "file_system.h"
#include "ceph_system.h"
#include "hdfs_system.h"
#include "local_system.h"
#include "bio_ref.h"

namespace ock {
namespace bio {
using FileSystemCreator = std::function<std::shared_ptr<FileSystem>()>;
const std::string CEPH_SYSTEM = "ceph";
const std::string HDFS_SYSTEM = "hdfs";
const std::string LOCAL_SYSTEM = "local";
const std::string NONE_SYSTEM = "none";

class FileSystemFactory {
public:
    static std::shared_ptr<FileSystem> CreateFileSystem(const std::string &type)
    {
        const auto &fileSystemMap = GetFileSystemMap();
        auto it = fileSystemMap.find(type);
        if (it != fileSystemMap.end()) {
            return it->second();
        } else if (type == NONE_SYSTEM) {
            return nullptr;
        } else {
            return std::make_shared<LocalSystem>();
        }
    }

private:
    static const std::unordered_map<std::string, FileSystemCreator> &GetFileSystemMap()
    {
        static const std::unordered_map<std::string, FileSystemCreator> fileSystemMap = {
            {CEPH_SYSTEM, []() { return std::make_shared<CephSystem>(); }},
            {HDFS_SYSTEM, []() { return std::make_shared<HdfsSystem>(); }},
            {LOCAL_SYSTEM, []() { return std::make_shared<LocalSystem>(); }}
        };
        return fileSystemMap;
    }
};
}
}


#endif // BOOSTIO_FILESYSTEMFACTORY_H
