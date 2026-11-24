/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-io is licensed under the Mulan PSL v2. */
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>
#include <dlfcn.h>
#include <unistd.h>
#include "test_common.h"
#include "ubsio_kvc.h"

extern "C" void FakeBioSetUnderFsType(BioUnderFsType type);

using namespace ock::ubsio;
using namespace ock::ubsio::nds;

namespace {
constexpr size_t SECTOR_SIZE = 512;
struct alignas(SECTOR_SIZE) Sector {
    std::array<char, SECTOR_SIZE> bytes{};
};

class NdsLocalReadTest : public KvTest {
protected:
    void SetUp() override
    {
        KvTest::SetUp();
        unsetenv("UBSIO_NDS_SKIP_MEM_REGISTER");
        char name[] = "./ubsio-nds-ut-XXXXXX";
        file = mkstemp(name);
        ASSERT_GE(file, 0);
        path = name;
        for (size_t i = 0; i < source.size(); ++i) {
            source[i] = static_cast<char>(i % 251);
        }
        ASSERT_EQ(write(file, source.data(), source.size()), static_cast<ssize_t>(source.size()));

        FakeBioSetUnderFsType(BIO_UNDERFS_LOCAL);
        FakeBioSetResult("ubsio.nds.enabled", 1);
        FakeBioSetResult("localFileSlot", 1);
        underfs = dlopen("libbio_underfs.so", RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(underfs, nullptr);
        ASSERT_EQ(DlBioSdkApi::LoadLibrary(), UBSIO_KVC_OK);
        ASSERT_EQ(DlBioSdkApi::KvBioInit(), UBSIO_KVC_OK);
    }

    void TearDown() override
    {
        KvTest::TearDown();
        unsetenv("UBSIO_NDS_SKIP_MEM_REGISTER");
        if (underfs != nullptr) {
            dlclose(underfs);
        }
        if (file >= 0) {
            close(file);
            unlink(path.c_str());
        }
    }

    void Read(const LocalFileRead *items, size_t count, int *results)
    {
        const std::vector<int> fds(count, file);
        const std::vector<uint64_t> devices(count, 0);
        std::fill_n(results, count, UBSIO_KVC_OK);
        NdsManager::Instance().ReadLocalFiles(items, fds.data(), devices.data(), count, results);
    }

    int file{-1};
    void *underfs{nullptr};
    std::string path;
    std::array<char, 2 * SECTOR_SIZE> source{};
};

TEST_F(NdsLocalReadTest, RequiresNdsEnabledOnLocalStorage)
{
    FakeBioSetResult("ubsio.nds.enabled", 0);
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_NO_NDS);
    FakeBioSetResult("ubsio.nds.enabled", 1);
    FakeBioSetUnderFsType(BIO_UNDERFS_SHARED);
    EXPECT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_NO_NDS);
    EXPECT_EQ(FakeNdsGetCallCount("nds_init"), 0);
}

TEST_F(NdsLocalReadTest, BatchReadsRegisteredAndScatteredBuffers)
{
    auto &manager = NdsManager::Instance();
    ASSERT_EQ(manager.Initialize(0), UBSIO_KVC_OK);
    std::array<Sector, 3> outputs{};
    for (auto &output : outputs) {
        ASSERT_EQ(manager.RegisterMemory(output.bytes.data(), output.bytes.size()), UBSIO_KVC_OK);
    }
    void *const single[] = {outputs[0].bytes.data()};
    void *const scatter[] = {outputs[1].bytes.data(), outputs[2].bytes.data()};
    const size_t lengths[] = {SECTOR_SIZE, SECTOR_SIZE};
    const LocalFileRead items[]{{single, lengths, 1, 0}, {scatter, lengths, 2, 0}};
    int results[2]{};
    Read(items, 2, results);

    EXPECT_EQ(results[0], UBSIO_KVC_OK);
    EXPECT_EQ(results[1], UBSIO_KVC_OK);
    EXPECT_TRUE(std::equal(outputs[0].bytes.begin(), outputs[0].bytes.end(), source.begin()));
    EXPECT_TRUE(std::equal(outputs[1].bytes.begin(), outputs[1].bytes.end(), source.begin()));
    EXPECT_TRUE(std::equal(outputs[2].bytes.begin(), outputs[2].bytes.end(), source.begin() + SECTOR_SIZE));
    EXPECT_EQ(FakeNdsGetCallCount("nds_io_submit"), 1);
    EXPECT_EQ(FakeNdsGetCallCount("nds_submit_registered"), 1);
    EXPECT_EQ(FakeNdsGetCallCount("nds_submit_unregistered"), 1);
    EXPECT_EQ(manager.UnRegisterMemory(outputs[0].bytes.data(), SECTOR_SIZE), UBSIO_KVC_OK);
}

TEST_F(NdsLocalReadTest, SplitsLargeBatchesAndReusesTheContextWithoutMemoryRegistration)
{
    ASSERT_EQ(setenv("UBSIO_NDS_SKIP_MEM_REGISTER", "1", 1), 0);
    auto &manager = NdsManager::Instance();
    ASSERT_EQ(manager.Initialize(0), UBSIO_KVC_OK);
    constexpr size_t count = NDS_MAX_IO_DEPTH + 1;
    std::vector<Sector> outputs(count);
    std::vector<void *> addresses(count);
    std::vector<LocalFileRead> items(count);
    std::vector<int> results(count);
    const size_t length = SECTOR_SIZE;
    ASSERT_EQ(manager.RegisterMemory(outputs.data(), sizeof(Sector) * outputs.size()), UBSIO_KVC_OK);
    for (size_t i = 0; i < count; ++i) {
        addresses[i] = outputs[i].bytes.data();
        items[i] = {&addresses[i], &length, 1, 0};
    }
    Read(items.data(), items.size(), results.data());
    EXPECT_TRUE(std::all_of(results.begin(), results.end(), [](int result) { return result == UBSIO_KVC_OK; }));
    EXPECT_TRUE(std::all_of(outputs.begin(), outputs.end(), [this](const Sector &output) {
        return std::equal(output.bytes.begin(), output.bytes.end(), source.begin());
    }));
    EXPECT_EQ(FakeNdsGetCallCount("nds_io_submit"), 2);

    int result = UBSIO_KVC_ERR;
    Read(items.data(), 1, &result);
    EXPECT_EQ(result, UBSIO_KVC_OK);
    EXPECT_EQ(FakeNdsGetCallCount("nds_io_new_ctx"), 1);
    EXPECT_EQ(FakeNdsGetCallCount("nds_register_fs"), 1);
    EXPECT_EQ(FakeNdsGetCallCount("nds_register_mem"), 0);
    EXPECT_EQ(FakeNdsGetCallCount("nds_submit_registered"), 0);
    EXPECT_EQ(manager.UnInitialize(), UBSIO_KVC_OK);
    EXPECT_EQ(FakeNdsGetCallCount("nds_io_destroy_ctx"), 1);
}

TEST_F(NdsLocalReadTest, ReadsTheOwnersFileThroughHostWhenDirectIoIsUnavailable)
{
    ASSERT_EQ(ACLApi::LoadLibrary(), UBSIO_KVC_OK);
    ASSERT_EQ(NdsManager::Instance().Initialize(0), UBSIO_KVC_OK);
    constexpr uint8_t ownerSlot = 3;
    const std::string missing = path + ".missing";
    const char *keys[] = {path.c_str(), missing.c_str()};
    const uint8_t slots[] = {ownerSlot, ownerSlot};
    Sector present, absent;
    void *const presentAddress[] = {present.bytes.data()};
    void *const absentAddress[] = {absent.bytes.data()};
    void *const *buffers[] = {presentAddress, absentAddress};
    const size_t length = SECTOR_SIZE;
    const size_t *lengths[] = {&length, &length};
    const uint32_t iovCounts[] = {1, 1};
    int results[2]{};
    FakeBioSetResult("UfsResolveFilePath", ownerSlot - 1);
    FakeNdsSetResult("nds_register_fs", -EOPNOTSUPP);
    ASSERT_EQ(UbsioKvCacheBatchGetDirect(keys, 2, slots, buffers, lengths, iovCounts, results), UBSIO_KVC_OK);
    EXPECT_EQ(results[0], RET_CACHE_OK);
    EXPECT_EQ(results[1], RET_CACHE_NOT_FOUND);
    EXPECT_TRUE(std::equal(present.bytes.begin(), present.bytes.end(), source.begin()));
    EXPECT_EQ(FakeAclGetCallCount("aclrtMemcpy"), 1);
    EXPECT_EQ(FakeNdsGetCallCount("nds_io_submit"), 0);
}
} // namespace
