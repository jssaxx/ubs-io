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

#include <algorithm>

#ifndef BOOSTIO_LOCAL_SYSTEM_TEST_FIXTURE_H
#define BOOSTIO_LOCAL_SYSTEM_TEST_FIXTURE_H

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <thread>
#include <type_traits>
#include <vector>
#include <fcntl.h>
#include <ftw.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include "mockcpp/mockcpp.hpp"
#include "underfs_config.h"
#include "underfs_c.h"
#include "bio_log.h"
#include "bio_lock.h"

#define private public
#define protected public
#include "local_system.h"
#include "shared_system.h"
#include "ufs_helper.h"
#undef protected
#undef private
#include "file_system_factory.h"

namespace local_system_test {
using namespace ock::bio;

inline uint64_t FileBytes(const std::string &key, uint64_t valueSize = 4097) noexcept
{
    return valueSize + key.size() + LOCAL_FILE_FOOTER_SIZE;
}

struct FileUsage {
    uint64_t usedBytes;
    uint64_t reservedBytes;
    size_t files;
};

struct ReadGate {
    std::mutex mutex;
    std::condition_variable wakeup;
    bool entered{false};
    bool released{false};
};

struct TraceEnableGuard {
    bool previous{ock::htracer::HtracerManager::IsEnable()};

    TraceEnableGuard()
    {
        ock::htracer::HTracerSetEnable(true);
    }

    ~TraceEnableGuard()
    {
        ock::htracer::HTracerSetEnable(previous);
    }
};

extern LocalSystem *spaceOwner;
extern uint32_t writeAttempts;
extern uint32_t unlinkAttempts;
extern uint64_t mockFileSystemTotalBytes;
extern uint64_t mockFileSystemAvailableBytes;
extern uint32_t fileSystemSpaceQueries;
extern std::thread::id writeThread;
extern ReadGate *readGate;
extern ReadGate *spaceGate;

ssize_t ObserveWrite(int fd, const struct iovec *iov, int iovcnt, off_t offset) noexcept;

ssize_t BlockingRead(int fd, void *value, size_t len, off_t offset);

ssize_t OutOfSpaceVectorWrite(int, const struct iovec *, int, off_t) noexcept;

int ProbeFileSystemCapacity(int, struct statvfs *space) noexcept;

int FailEvictionUnlink(int, const char *, int);

int ObserveTemporaryCleanup(const char *path) noexcept;

template <typename System>
class TestPosixSystem : public testing::Test {
protected:
    void SetUp() override
    {
        original = UnderFsConfig::Instance()->GetUnderFsConfig();
        char path[] = "/tmp/ubsio-local-eviction-XXXXXX";
        ASSERT_NE(mkdtemp(path), nullptr);
        root = path;
        config.underFsType = std::is_same<System, SharedSystem>::value ? BIO_UNDERFS_SHARED : BIO_UNDERFS_LOCAL;
        config.localConfig.rootPath = root;
        config.localConfig.checkIntervalSeconds = 3600;
        system.SetMetaEventCallback([](void *context, UfsLocalMetaEventType type, const char *key) {
            auto *self = static_cast<TestPosixSystem *>(context);
            self->events.emplace_back(type, key);
        }, this);
    }

    void TearDown() override
    {
        system.Stop();
        system.SetMetaEventCallback(nullptr, nullptr);
        GlobalMockObject::verify();
        spaceOwner = nullptr;
        readGate = nullptr;
        spaceGate = nullptr;
        UnderFsConfig::Initialize(original);
        if (!root.empty()) {
            nftw(root.c_str(), [](const char *path, const struct stat *, int, struct FTW *) {
                return remove(path);
            }, 16, FTW_DEPTH | FTW_PHYS);
        }
    }

    void Start()
    {
        UnderFsConfig::Initialize(config);
        ASSERT_EQ(system.Init(), BIO_OK);
    }

    UnderFsConfigInfo MakeCConfig() const noexcept
    {
        const auto &local = config.localConfig;
        return {UnderFsTypeName(config.underFsType), {"", "", "", ""}, {"", ""},
            {local.rootPath.c_str(), local.slotIndex, local.deviceCount,
                FileEvictionPolicyName(local.evictionPolicy), local.checkIntervalSeconds}};
    }

    void Write(const char *key, char value, time_t age)
    {
        std::vector<char> data(4097, value);
        ASSERT_EQ(system.Put(key, data.data(), data.size()), BIO_OK);
        std::string path;
        ASSERT_EQ(system.BuildFilePath(key, path), BIO_OK);
        int fd = open(path.c_str(), O_RDWR);
        ASSERT_GE(fd, 0);
        EXPECT_EQ(fsync(fd), 0);
        close(fd);
        struct timespec times[2]{{age, 0}, {age, 0}};
        ASSERT_EQ(utimensat(AT_FDCWD, path.c_str(), times, 0), 0);
    }

    void MockFileSystemCapacity(uint64_t total) noexcept
    {
        std::lock_guard<std::mutex> lock(system.mEvictionMutex);
        mockFileSystemTotalBytes = total;
        mockFileSystemAvailableBytes = total;
        if (!capacityMocked) {
            MOCKER(fstatvfs).stubs().will(invoke(ProbeFileSystemCapacity));
            capacityMocked = true;
        }
    }

    void MockManagedCapacity(uint64_t capacity) noexcept
    {
        const uint64_t total = capacity * config.localConfig.deviceCount;
        MockFileSystemCapacity((total * 10 + 8) / 9);
    }

    void SetEvictionTarget(uint64_t capacity) noexcept
    {
        MockManagedCapacity(capacity);
        std::lock_guard<std::mutex> lock(system.mEvictionMutex);
        system.mCapacityBytes = capacity;
    }

    BResult ScanKeys(std::vector<std::string> &keys, uint64_t &used)
    {
        FileIndex index;
        auto policy = CreateFileEvictionPolicy(config.localConfig.evictionPolicy);
        BResult ret = system.CollectRecoveryFiles(index, used);
        keys.clear();
        if (ret == BIO_OK) {
            if (policy == nullptr) {
                return BIO_ALLOC_FAIL;
            }
            ret = policy->Recover(index);
            if (ret != BIO_OK) {
                return ret;
            }
            std::vector<EvictionCandidate> candidates;
            ret = policy->TakeCandidates(policy->Size(),
                [](const FileRecord &) noexcept { return true; }, candidates);
            if (ret != BIO_OK) {
                return ret;
            }
            for (const auto &file : candidates) {
                keys.push_back(file.key);
            }
        }
        return ret;
    }

    BResult SnapshotKeys(std::unordered_map<std::string, uint64_t> &keys) noexcept
    {
        UfsLocalKeyInfo *items = nullptr;
        uint64_t count = 0;
        BResult ret = system.ScanKeys(&items, &count);
        std::unique_ptr<UfsLocalKeyInfo, decltype(&std::free)> result(items, &std::free);
        if (ret != BIO_OK) {
            return ret;
        }
        keys.clear();
        for (uint64_t i = 0; i < count; ++i) {
            keys.emplace(items[i].key, items[i].valueLen);
        }
        return BIO_OK;
    }

    std::vector<std::string> CandidateKeys()
    {
        std::lock_guard<std::mutex> capacity(system.mEvictionMutex);
        std::vector<std::string> keys;
        std::vector<EvictionCandidate> candidates;
        EXPECT_EQ(system.mEvictionPolicy->TakeCandidates(system.mEvictionPolicy->Size(),
            [](const FileRecord &) noexcept { return true; }, candidates), BIO_OK);
        for (const auto &file : candidates) {
            keys.push_back(file.key);
        }
        for (auto file = candidates.rbegin(); file != candidates.rend(); ++file) {
            EXPECT_EQ(system.mEvictionPolicy->ColdInsert(*file), BIO_OK);
        }
        return keys;
    }

    FileUsage Usage()
    {
        std::lock_guard<std::mutex> capacity(system.mEvictionMutex);
        return {system.mUsedBytes, system.mReservedBytes,
            system.mEvictionPolicy == nullptr ? 0 : system.mEvictionPolicy->Size()};
    }

    bool Contains(const std::string &key) const
    {
        FileRecord file;
        FileReference reference;
        return system.mEvictionPolicy->Lookup(key, file, reference) == BIO_OK;
    }

    BResult Reserve(const std::string &key, uint64_t bytes)
    {
        return system.ReserveWriteSpace(key, bytes);
    }

    bool capacityMocked{false};
    BioConfig::UnderFsConfig original;
    BioConfig::UnderFsConfig config;
    std::string root;
    System system;
    std::vector<std::pair<UfsLocalMetaEventType, std::string>> events;
};

using TestLocalEviction = TestPosixSystem<LocalSystem>;
using TestSharedSystem = TestPosixSystem<SharedSystem>;

}

#endif
