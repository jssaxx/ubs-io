/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <cstdlib>
#include <cstring>
#include <string>

#include "gtest/gtest.h"
#include "file_system_factory.h"
#include "local_system.h"
#include "underfs_config.h"

using namespace ock::bio;

namespace {
constexpr size_t TEST_IO_SIZE = 4096;
constexpr size_t TEST_IO_OFFSET = 512;

class UnderFsConfigGuard {
public:
    UnderFsConfigGuard() : mOriginal(UnderFsConfig::Instance()->GetUnderFsConfig()) {}

    ~UnderFsConfigGuard()
    {
        UnderFsConfig::Initialize(mOriginal);
    }

private:
    BioConfig::UnderFsConfig mOriginal;
};

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char pathTemplate[] = "/tmp/ubsio-local-test-XXXXXX";
        char *path = mkdtemp(pathTemplate);
        if (path != nullptr) {
            mPath = path;
        }
    }

    ~TemporaryDirectory()
    {
        if (!mPath.empty()) {
            std::string command = "rm -rf -- '" + mPath + "'";
            (void)system(command.c_str());
        }
    }

    const std::string &Path() const
    {
        return mPath;
    }

private:
    std::string mPath;
};

void InitializeLocalConfig(const std::string &rootPath)
{
    BioConfig::UnderFsConfig config;
    config.underFsType = BIO_UNDERFS_LOCAL;
    config.localConfig.rootPath = rootPath;
    UnderFsConfig::Initialize(config);
}

std::shared_ptr<FileSystem> CreateLocal()
{
    return FileSystemFactory::CreateFileSystem(BIO_UNDERFS_LOCAL);
}
}

TEST(TestUnderFsLocal, factory_uses_local_posix_backend)
{
    TemporaryDirectory root;
    ASSERT_FALSE(root.Path().empty());
    UnderFsConfigGuard guard;
    InitializeLocalConfig(root.Path());

    auto fileSystem = CreateLocal();
    ASSERT_NE(fileSystem, nullptr);
    EXPECT_NE(std::dynamic_pointer_cast<LocalSystem>(fileSystem), nullptr);
    EXPECT_EQ(fileSystem->Init(), BIO_OK);
    fileSystem->Stop();
}

TEST(TestUnderFsLocal, crud_with_aligned_and_unaligned_io)
{
    TemporaryDirectory root;
    ASSERT_FALSE(root.Path().empty());
    UnderFsConfigGuard guard;
    InitializeLocalConfig(root.Path());

    auto fileSystem = CreateLocal();
    ASSERT_NE(fileSystem, nullptr);
    ASSERT_EQ(fileSystem->Init(), BIO_OK);

    const char *alignedKey = "local-aligned-key";
    char *alignedValue = nullptr;
    ASSERT_EQ(posix_memalign(reinterpret_cast<void **>(&alignedValue), TEST_IO_SIZE, TEST_IO_SIZE), 0);
    for (size_t index = 0; index < TEST_IO_SIZE; ++index) {
        alignedValue[index] = static_cast<char>(index % 251);
    }

    ASSERT_EQ(fileSystem->Put(alignedKey, alignedValue, TEST_IO_SIZE), BIO_OK);
    EXPECT_EQ(fileSystem->Exist(alignedKey), BIO_OK);

    FileSystem::ObjStat stat{};
    ASSERT_EQ(fileSystem->Stat(alignedKey, stat), BIO_OK);
    EXPECT_EQ(stat.size, TEST_IO_SIZE);

    char *readBuffer = nullptr;
    ASSERT_EQ(posix_memalign(reinterpret_cast<void **>(&readBuffer), TEST_IO_SIZE, TEST_IO_SIZE), 0);
    ASSERT_EQ(fileSystem->Get(alignedKey, readBuffer, TEST_IO_SIZE, 0), BIO_OK);
    EXPECT_EQ(std::memcmp(alignedValue, readBuffer, TEST_IO_SIZE), 0);

    size_t realLen = 0;
    ASSERT_EQ(fileSystem->GetWithRealLen(alignedKey, readBuffer, TEST_IO_OFFSET, TEST_IO_OFFSET, realLen), BIO_OK);
    EXPECT_EQ(realLen, TEST_IO_OFFSET);
    EXPECT_EQ(std::memcmp(alignedValue + TEST_IO_OFFSET, readBuffer, TEST_IO_OFFSET), 0);

    const char *unalignedKey = "local-unaligned-key";
    const char *unalignedValue = "unaligned-value";
    const size_t unalignedLength = std::strlen(unalignedValue);
    ASSERT_EQ(fileSystem->Put(unalignedKey, unalignedValue, unalignedLength), BIO_OK);
    char unalignedRead[64] = { 0 };
    ASSERT_EQ(fileSystem->Get(unalignedKey, unalignedRead, unalignedLength, 0), BIO_OK);
    EXPECT_STREQ(unalignedRead, unalignedValue);

    const char *replacement = "replacement-value";
    ASSERT_EQ(fileSystem->Put(unalignedKey, replacement, std::strlen(replacement)), BIO_OK);
    char existingRead[64] = { 0 };
    ASSERT_EQ(fileSystem->Get(unalignedKey, existingRead, unalignedLength, 0), BIO_OK);
    EXPECT_STREQ(existingRead, unalignedValue);

    EXPECT_EQ(fileSystem->Delete(alignedKey), BIO_OK);
    EXPECT_EQ(fileSystem->Exist(alignedKey), BIO_NOT_EXISTS);
    EXPECT_EQ(fileSystem->Delete(unalignedKey), BIO_OK);

    std::free(alignedValue);
    std::free(readBuffer);
    fileSystem->Stop();
}

TEST(TestUnderFsLocal, data_is_visible_after_restart)
{
    TemporaryDirectory root;
    ASSERT_FALSE(root.Path().empty());
    UnderFsConfigGuard guard;
    InitializeLocalConfig(root.Path());

    const char *key = "local-restart-key";
    const char *value = "persistent-value";
    {
        auto fileSystem = CreateLocal();
        ASSERT_NE(fileSystem, nullptr);
        ASSERT_EQ(fileSystem->Init(), BIO_OK);
        ASSERT_EQ(fileSystem->Put(key, value, std::strlen(value)), BIO_OK);
        fileSystem->Stop();
    }

    {
        auto fileSystem = CreateLocal();
        ASSERT_NE(fileSystem, nullptr);
        ASSERT_EQ(fileSystem->Init(), BIO_OK);
        char readBuffer[64] = { 0 };
        ASSERT_EQ(fileSystem->Get(key, readBuffer, std::strlen(value), 0), BIO_OK);
        EXPECT_STREQ(readBuffer, value);
        EXPECT_EQ(fileSystem->Delete(key), BIO_OK);
        fileSystem->Stop();
    }
}
