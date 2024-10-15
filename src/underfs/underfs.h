/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#ifndef BOOSTIO_UNDERFS_H
#define BOOSTIO_UNDERFS_H

#include <memory>
#include "file_system_factory.h"
#include "file_system.h"
#include "bio_ref.h"
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
        return UnderFsConfig::Instance()->GetUnderFsConfig().underFsType;
#endif
    }
};
}
}

#endif // BOOSTIO_UNDERFS_H
