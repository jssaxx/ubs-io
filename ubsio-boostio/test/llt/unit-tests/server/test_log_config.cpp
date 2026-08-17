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

#include <algorithm>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "bio_file_util.h"

#define private public
#include "bio_config_instance.h"
#undef private

using namespace ock::bio;

namespace {
ConfigurationPtr MakeLogConfiguration()
{
    ConfigurationPtr conf = new BioConfig();
    static_cast<BioConfig *>(conf.Get())->LoadDefaultConf();
    return conf;
}
}

TEST(TestLogConfig, default_log_path_is_backward_compatible)
{
    auto conf = MakeLogConfiguration();
    auto *config = static_cast<BioConfig *>(conf.Get());

    ASSERT_EQ(config->AutoConfigDaemonLogAndOther(conf), BIO_OK);
    EXPECT_EQ(config->GetDaemonConfig().logPath, "/var/log/ubsio");
}

TEST(TestLogConfig, custom_log_path_is_loaded)
{
    auto conf = MakeLogConfiguration();
    auto *config = static_cast<BioConfig *>(conf.Get());
    const std::string customLogPath = "/tmp/ubsio-custom-log";
    config->Set(LOG_PATH.first, customLogPath);

    ASSERT_EQ(config->AutoConfigDaemonLogAndOther(conf), BIO_OK);
    EXPECT_EQ(config->GetDaemonConfig().logPath, customLogPath);
}

TEST(TestLogConfig, empty_log_path_is_rejected)
{
    auto conf = MakeLogConfiguration();
    conf->Set(LOG_PATH.first, std::string());

    auto errors = conf->Validate();
    auto logPathError = std::find_if(errors.begin(), errors.end(), [](const std::string &error) {
        return error.find(LOG_PATH.first) != std::string::npos;
    });
    EXPECT_NE(logPathError, errors.end());
}

TEST(TestLogConfig, log_directories_are_prepared_by_config)
{
    char directoryTemplate[] = "/tmp/ubsio-log-config-XXXXXX";
    char *temporaryDirectory = mkdtemp(directoryTemplate);
    ASSERT_NE(temporaryDirectory, nullptr);
    ASSERT_EQ(rmdir(temporaryDirectory), 0);

    auto conf = MakeLogConfiguration();
    auto *config = static_cast<BioConfig *>(conf.Get());
    config->Set(LOG_PATH.first, std::string(temporaryDirectory));
    ASSERT_EQ(config->AutoConfigDaemonLogAndOther(conf), BIO_OK);

    auto result = config->PrepareLogDirectories();
    EXPECT_EQ(result, BIO_OK);
    if (result == BIO_OK) {
        struct stat pathStat {};
        EXPECT_EQ(stat(config->GetDaemonConfig().logPath.c_str(), &pathStat), 0);
        EXPECT_TRUE(S_ISDIR(pathStat.st_mode));

        std::string traceDirectory = config->GetDaemonConfig().logPath + "/trace";
        EXPECT_EQ(stat(traceDirectory.c_str(), &pathStat), 0);
        EXPECT_TRUE(S_ISDIR(pathStat.st_mode));
    }

    if (FileUtil::Exist(temporaryDirectory)) {
        EXPECT_TRUE(FileUtil::RemoveDirRecursive(temporaryDirectory));
    }
}
