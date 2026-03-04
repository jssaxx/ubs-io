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

#ifndef BOOSTIO_UNDERFS_H
#define BOOSTIO_UNDERFS_H

#include <memory>
#include "bio_ref.h"
#include "file_system.h"
#include "file_system_factory.h"
#include "underfs_config.h"

namespace ock {
namespace bio {
using UnderFsPtr = std::shared_ptr<FileSystem>;
class UnderFs {
public:
    using ObjStat = FileSystem::ObjStat;

    static void InitUnderFsConfig(BioConfig::UnderFsConfig config)
    {
        UnderFsConfig::Initialize(config);
    }

    static std::shared_ptr<FileSystem> &Instance()
    {
        static std::shared_ptr<FileSystem> instance = FileSystemFactory::CreateFileSystem(GetUnderFsType());
        return instance;
    }

private:
    static std::string GetUnderFsType()
    {
#ifdef DEBUG_UT
        return LOCAL_SYSTEM;
#else
        auto instance = UnderFsConfig::Instance();
        if (instance == nullptr) {
            return "";
        }
        return instance->GetUnderFsConfig().underFsType;
#endif
    }
};
}
}

#endif // BOOSTIO_UNDERFS_H
