/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BOOSTIO_FILESYSTEMFACTORY_H
#define BOOSTIO_FILESYSTEMFACTORY_H

#include "file_system.h"
#include <functional>
#include "ceph_system.h"
#include "hdfs_system.h"
#include "local_system.h"
#include "bio_ref.h"
#include <memory>

namespace ock {
namespace bio {
using FileSystemCreator = std::function<std::shared_ptr<FileSystem>()>;
const std::string CEPH_SYSTEM = "ceph";
const std::string HDFS_SYSTEM = "hdfs";
const std::string LOCAL_SYSTEM = "local";

class FileSystemFactory {
public:
    static std::shared_ptr<FileSystem> CreateFileSystem(const std::string &type)
    {
        const auto &fileSystemMap = GetFileSystemMap();
        auto it = fileSystemMap.find(type);
        if (it != fileSystemMap.end()) {
            return it->second();
        } else {
            return std::make_shared<LocalSystem>(); // use local file system by default
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
