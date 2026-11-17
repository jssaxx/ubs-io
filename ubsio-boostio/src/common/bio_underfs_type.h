/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef BOOSTIO_BIO_UNDERFS_TYPE_H
#define BOOSTIO_BIO_UNDERFS_TYPE_H

#include <string>
#include <unordered_map>
#include "bio_c.h"

namespace ock {
namespace bio {
inline BioUnderFsType ParseUnderFsType(const std::string &name)
{
    static const std::unordered_map<std::string, BioUnderFsType> types = {
        {"none", BIO_UNDERFS_NONE}, {"ceph", BIO_UNDERFS_CEPH}, {"hdfs", BIO_UNDERFS_HDFS},
        {"local", BIO_UNDERFS_LOCAL}, {"shared", BIO_UNDERFS_SHARED}
    };
    const auto type = types.find(name);
    return type == types.end() ? BIO_UNDERFS_BUTT : type->second;
}

inline const char *UnderFsTypeName(BioUnderFsType type)
{
    switch (type) {
        case BIO_UNDERFS_NONE:
            return "none";
        case BIO_UNDERFS_CEPH:
            return "ceph";
        case BIO_UNDERFS_HDFS:
            return "hdfs";
        case BIO_UNDERFS_LOCAL:
            return "local";
        case BIO_UNDERFS_SHARED:
            return "shared";
        default:
            return nullptr;
    }
}
}
}

#endif // BOOSTIO_BIO_UNDERFS_TYPE_H
