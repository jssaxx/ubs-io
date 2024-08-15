/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */

#ifndef BOOSTIO_UNDERFS_CONFIG_H
#define BOOSTIO_UNDERFS_CONFIG_H

#include "bio_config_instance.h"

namespace ock {
namespace bio {
class UnderFsConfig {
public:
    static const std::shared_ptr<UnderFsConfig> &Instance()
    {
        static auto instance = std::make_shared<UnderFsConfig>();
        return instance;
    }

    static void Initialize(const BioConfig::UnderFsConfig &config)
    {
        Instance()->InitializeImpl(config);
    }

    const BioConfig::UnderFsConfig &GetUnderFsConfig() const noexcept
    {
        return mConfig;
    }

private:
    void InitializeImpl(const BioConfig::UnderFsConfig &config)
    {
        mConfig = config;
    }

private:
    BioConfig::UnderFsConfig mConfig;
};
}
}

#endif // BOOSTIO_UNDERFS_CONFIG_H
