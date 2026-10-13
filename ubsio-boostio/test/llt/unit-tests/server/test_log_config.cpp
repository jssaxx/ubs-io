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
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "gtest/gtest.h"
#include "bio_file_util.h"
#include "file_system_factory.h"

#define private public
#include "bio_config_instance.h"
#undef private
#include "underfs_config.h"

using namespace ock::bio;

namespace {
class LocalUnderFsFileConfig : public BioConfig {};

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

TEST(TestLogConfig, startup_semantic_error_is_written_to_stderr)
{
    auto conf = MakeLogConfiguration();
    auto *config = static_cast<BioConfig *>(conf.Get());
    const std::string underFsType = "ceph";
    const std::string invalidCephConfigPath = "/path/that/does/not/exist/ceph.conf";
    conf->Set(UNDERFS_FILE_SYSTEM_TYPE.first, underFsType);
    conf->Set(UNDERFS_CEPH_CFG_PATH.first, invalidCephConfigPath);

    testing::internal::CaptureStderr();
    auto result = config->AutoConfigUnderFs(conf);
    const std::string output = testing::internal::GetCapturedStderr();

    EXPECT_EQ(result, BIO_ERR);
    EXPECT_NE(output.find("Ceph config path not exist"), std::string::npos) << output;
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

TEST(TestLogConfig, local_and_shared_underfs_use_file_backend_and_validate_root)
{
    const std::pair<const char *, BioUnderFsType> cases[] = {
        {"local", BIO_UNDERFS_LOCAL}, {"shared", BIO_UNDERFS_SHARED}
    };
    for (const auto &item : cases) {
        SCOPED_TRACE(item.first);
        EXPECT_NE(std::dynamic_pointer_cast<LocalSystem>(FileSystemFactory::CreateFileSystem(item.second)), nullptr);
        auto conf = MakeLogConfiguration();
        auto *config = static_cast<BioConfig *>(conf.Get());
        conf->Set(UNDERFS_FILE_SYSTEM_TYPE.first, std::string(item.first));
        conf->Set(UNDERFS_LOCAL_ROOT_PATH.first, std::string("/ubsio-missing-local-root"));
        EXPECT_EQ(config->AutoConfigUnderFs(conf), BIO_ERR);
        char directoryTemplate[] = "/tmp/ubsio-local-config-XXXXXX";
        char *directory = mkdtemp(directoryTemplate);
        ASSERT_NE(directory, nullptr);
        conf->Set(UNDERFS_LOCAL_ROOT_PATH.first, std::string(directory));
        EXPECT_EQ(config->AutoConfigUnderFs(conf), BIO_OK);
        EXPECT_EQ(config->GetUnderFsConfig().underFsType, item.second);
        EXPECT_EQ(config->GetUnderFsConfig().localConfig.rootPath, directory);
        EXPECT_EQ(rmdir(directory), 0);
    }
}

TEST(TestLogConfig, underfs_config_strings_map_to_enum_before_cache_defaults)
{
    char directoryTemplate[] = "ubsio-underfs-enum-XXXXXX";
    char *directory = mkdtemp(directoryTemplate);
    ASSERT_NE(directory, nullptr);
    struct Cleanup {
        std::string directory;
        ~Cleanup()
        {
            FileUtil::RemoveDirRecursive(directory);
        }
    } cleanup{directory};
    const std::string cephPath = std::string(directory) + "/ceph.conf";
    {
        std::ofstream file(cephPath);
        ASSERT_TRUE(file.is_open());
    }
    auto conf = MakeLogConfiguration();
    auto *config = static_cast<BioConfig *>(conf.Get());
    EXPECT_EQ(config->GetUnderFsConfig().underFsType, BIO_UNDERFS_NONE);
    constexpr int32_t testMemCapacityGb = 1;
    conf->Set(MEM_CAPACITY_SIZE_GB.first, testMemCapacityGb);
    ASSERT_EQ(conf->GetInt(MEM_CAPACITY_SIZE_GB.first), testMemCapacityGb);
    conf->Set(DISK_CONF_PATH.first, std::string());
    conf->Set(WCACHE_EVICT_WATER_LEVEL.first, int32_t{0});
    conf->Set(UNDERFS_LOCAL_ROOT_PATH.first, std::string(directory));
    conf->Set(UNDERFS_CEPH_CFG_PATH.first, cephPath);
    const std::pair<const char *, BioUnderFsType> cases[] = {
        {"none", BIO_UNDERFS_NONE}, {"ceph", BIO_UNDERFS_CEPH}, {"hdfs", BIO_UNDERFS_HDFS},
        {"local", BIO_UNDERFS_LOCAL}, {"shared", BIO_UNDERFS_SHARED}
    };
    for (const auto &item : cases) {
        SCOPED_TRACE(item.first);
        conf->Set(UNDERFS_FILE_SYSTEM_TYPE.first, std::string(item.first));
        ASSERT_EQ(config->AutoConfAfterLoadFromFile(conf), BIO_OK);
        EXPECT_EQ(config->GetUnderFsConfig().underFsType, item.second);
        EXPECT_STREQ(UnderFsTypeName(item.second), item.first);
        EXPECT_EQ(config->GetDaemonConfig().wcacheMemEvictLevel, item.second == BIO_UNDERFS_NONE ? NO_90 : 0);
    }
    for (const char *type : {"invalid", "unknown"}) {
        conf->Set(UNDERFS_FILE_SYSTEM_TYPE.first, std::string(type));
        EXPECT_EQ(config->AutoConfigUnderFs(conf), BIO_ERR);
        EXPECT_EQ(ParseUnderFsType(type), BIO_UNDERFS_BUTT);
    }
    EXPECT_EQ(UnderFsTypeName(BIO_UNDERFS_BUTT), nullptr);
}

TEST(TestLogConfig, local_and_shared_underfs_are_loaded_and_validated_from_config_file)
{
    char directoryTemplate[] = "ubsio-local-conf-XXXXXX";
    char *directory = mkdtemp(directoryTemplate);
    ASSERT_NE(directory, nullptr);
    struct Cleanup {
        std::string directory;
        ~Cleanup()
        {
            FileUtil::RemoveDirRecursive(directory);
        }
    } cleanup{directory};
    const std::string path = std::string(directory) + "/ubsio.conf";
    const std::pair<const char *, BioUnderFsType> cases[] = {
        {"local", BIO_UNDERFS_LOCAL}, {"shared", BIO_UNDERFS_SHARED}
    };
    for (const auto &item : cases) {
        SCOPED_TRACE(item.first);
        {
            std::ofstream file(path);
            ASSERT_TRUE(file.is_open());
            file << UNDERFS_FILE_SYSTEM_TYPE.first << "=" << item.first << "\n"
                 << UNDERFS_LOCAL_ROOT_PATH.first << "=" << directory << "\n";
        }
        ASSERT_TRUE(Configuration::ReadConf<LocalUnderFsFileConfig>(path));
        auto conf = Configuration::GetInstance<LocalUnderFsFileConfig>();
        auto *config = static_cast<BioConfig *>(conf.Get());
        const auto errors = conf->Validate();
        EXPECT_TRUE(std::none_of(errors.begin(), errors.end(), [](const std::string &error) {
            return error.find(UNDERFS_FILE_SYSTEM_TYPE.first) != std::string::npos;
        }));
        ASSERT_EQ(config->AutoConfigUnderFs(conf), BIO_OK);
        EXPECT_EQ(config->GetUnderFsConfig().underFsType, item.second);
        std::string canonicalRoot(directory);
        ASSERT_TRUE(FileUtil::CanonicalPath(canonicalRoot));
        EXPECT_EQ(config->GetUnderFsConfig().localConfig.rootPath, canonicalRoot);
    }
    {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open());
        file << UNDERFS_FILE_SYSTEM_TYPE.first << "=unknown\n";
    }
    ASSERT_TRUE(Configuration::ReadConf<LocalUnderFsFileConfig>(path));
    const auto invalidErrors = Configuration::GetInstance<LocalUnderFsFileConfig>()->Validate();
    EXPECT_TRUE(std::any_of(invalidErrors.begin(), invalidErrors.end(), [](const std::string &error) {
        return error.find(UNDERFS_FILE_SYSTEM_TYPE.first) != std::string::npos;
    }));
}

TEST(TestLogConfig, shared_and_local_instances_share_file_namespace)
{
    char directoryTemplate[] = "ubsio-local-files-XXXXXX";
    char *directory = mkdtemp(directoryTemplate);
    ASSERT_NE(directory, nullptr);
    const auto originalConfig = UnderFsConfig::Instance()->GetUnderFsConfig();
    auto config = originalConfig;
    config.underFsType = BIO_UNDERFS_SHARED;
    config.localConfig.rootPath = directory;
    UnderFsConfig::Initialize(config);
    auto writer = FileSystemFactory::CreateFileSystem(BIO_UNDERFS_SHARED);
    auto reader = FileSystemFactory::CreateFileSystem(BIO_UNDERFS_LOCAL);
    struct Cleanup {
        BioConfig::UnderFsConfig original;
        std::string directory;
        std::shared_ptr<FileSystem> first;
        std::shared_ptr<FileSystem> second;
        ~Cleanup()
        {
            first->Stop();
            second->Stop();
            UnderFsConfig::Initialize(original);
            FileUtil::RemoveDirRecursive(directory);
        }
    } cleanup{originalConfig, directory, writer, reader};
    ASSERT_EQ(writer->Init(), BIO_OK);
    ASSERT_EQ(reader->Init(), BIO_OK);
    const char key[] = "local-layout-key";
    const char payload[] = "shared file payload";
    constexpr size_t readBufferSize = 64;
    char buffer[readBufferSize]{};
    size_t realLength = 0;
    ASSERT_EQ(writer->Put(key, payload, sizeof(payload)), BIO_OK);
    ASSERT_EQ(reader->GetWithRealLen(key, buffer, sizeof(buffer), 0, realLength), BIO_OK);
    EXPECT_EQ(realLength, sizeof(payload));
    EXPECT_EQ(std::string(buffer), payload);
    FileSystem::ObjStat stat{};
    ASSERT_EQ(reader->Stat(key, stat), BIO_OK);
    EXPECT_EQ(stat.size, sizeof(payload));
    EXPECT_EQ(reader->Exist(key), BIO_OK);
    const char replacement[] = "replacement";
    ASSERT_EQ(reader->Put(key, replacement, sizeof(replacement)), BIO_OK);
    ASSERT_EQ(writer->Get(key, buffer, sizeof(replacement), 0), BIO_OK);
    EXPECT_EQ(std::string(buffer), replacement);
    ASSERT_EQ(writer->Delete(key), BIO_OK);
    EXPECT_EQ(reader->Exist(key), BIO_NOT_EXISTS);
}
