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
} // namespace bio
} // namespace ock

#endif // BOOSTIO_UNDERFS_CONFIG_H
