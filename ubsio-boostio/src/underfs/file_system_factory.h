/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
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

#include <memory>
#include "bio_underfs_type.h"
#include "file_system.h"
#include "dl_ceph_system.h"
#include "hdfs_system.h"
#include "local_system.h"
#include "shared_system.h"
#include "bio_ref.h"

namespace ock {
namespace bio {
class FileSystemFactory {
public:
    static std::shared_ptr<FileSystem> CreateFileSystem(BioUnderFsType type)
    {
        switch (type) {
            case BIO_UNDERFS_CEPH:
                return std::make_shared<DlCephSystem>();
            case BIO_UNDERFS_HDFS:
                return std::make_shared<HdfsSystem>();
            case BIO_UNDERFS_LOCAL:
                return std::make_shared<LocalSystem>();
            case BIO_UNDERFS_SHARED:
                return std::make_shared<SharedSystem>();
            default:
                return std::make_shared<LocalSystem>();
        }
    }
};
}
}


#endif // BOOSTIO_FILESYSTEMFACTORY_H
