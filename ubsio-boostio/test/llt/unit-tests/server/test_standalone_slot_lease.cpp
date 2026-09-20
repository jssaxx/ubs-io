/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gtest/gtest.h"

#include "bio_types.h"

#define private public
#include "standalone_slot_lease.h"
#undef private

using namespace ock::bio;

namespace {

constexpr uint32_t SLOT_LEASE_MAGIC = 0x5542534CU;
constexpr uint32_t SLOT_LEASE_VERSION = 1U;
constexpr uint32_t INITIALIZATION_STATE_FAILED = 3U;
constexpr size_t PROC_STAT_STATE_OFFSET_FROM_COMM_END = 2U;

struct TestSlotOwner {
    uint32_t occupied;
    int32_t pid;
    uint64_t pidStartTime;
    uint64_t generation;
};

struct TestSlotLeaseHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t structSize;
    uint32_t slotCount;
    uint32_t initializationState;
    int32_t initializerPid;
    uint64_t initializerStartTime;
    uint64_t nextGeneration;
    TestSlotOwner slots[DEVICE_SIZE];
};

struct LeaseChildResult {
    uint32_t inputIndex;
    int32_t result;
    uint32_t slotIndex;
};

struct LeaseWorkerContext {
    int32_t releaseFd;
    StandaloneSlotLease *lease;
};

const std::string &GetLeaseShmName()
{
    static const std::string shmName = "/ubsio_standalone_slot_lease_test_" + std::to_string(getpid()) + "_" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    StandaloneSlotLease::SetShmNameForTest(shmName);
    return shmName;
}

void CleanupLeaseShm()
{
    std::string shmName = GetLeaseShmName();
    if (!shmName.empty()) {
        (void)shm_unlink(shmName.c_str());
    }
}

bool WaitForProcessState(pid_t pid, char expectedState)
{
    constexpr uint32_t RETRY_TIMES = 200U;
    constexpr useconds_t RETRY_INTERVAL_US = 10000U;
    for (uint32_t retry = 0; retry < RETRY_TIMES; ++retry) {
        std::ifstream statFile("/proc/" + std::to_string(pid) + "/stat");
        std::string line;
        if (statFile.is_open() && std::getline(statFile, line)) {
            auto commandEnd = line.rfind(')');
            if (commandEnd != std::string::npos &&
                commandEnd + PROC_STAT_STATE_OFFSET_FROM_COMM_END < line.size() &&
                line[commandEnd + PROC_STAT_STATE_OFFSET_FROM_COMM_END] == expectedState) {
                return true;
            }
        }
        (void)usleep(RETRY_INTERVAL_US);
    }
    return false;
}

void *WaitForPipeClose(void *argument)
{
    auto *context = static_cast<LeaseWorkerContext *>(argument);
    char signal = 0;
    ssize_t readResult = 0;
    do {
        readResult = read(context->releaseFd, &signal, sizeof(signal));
    } while (readResult < 0 && errno == EINTR);
    (void)close(context->releaseFd);
    delete context->lease;
    delete context;
    return nullptr;
}

std::vector<LeaseChildResult> RunLeaseProcesses(uint32_t processCount, uint32_t slotCount)
{
    int32_t resultPipe[2] = { -1, -1 };
    int32_t releasePipe[2] = { -1, -1 };
    EXPECT_EQ(pipe(resultPipe), 0);
    EXPECT_EQ(pipe(releasePipe), 0);
    std::vector<pid_t> children;
    for (uint32_t index = 0; index < processCount; ++index) {
        pid_t child = fork();
        EXPECT_GE(child, 0);
        if (child == 0) {
            (void)close(resultPipe[0]);
            (void)close(releasePipe[1]);
            StandaloneSlotLease lease;
            LeaseChildResult result = { index, BIO_ERR, UINT32_MAX };
            result.result = lease.Acquire(slotCount, result.slotIndex);
            if (result.result == BIO_OK) {
                result.result = lease.PublishReady();
            }
            ssize_t written = write(resultPipe[1], &result, sizeof(result));
            char release = 0;
            (void)read(releasePipe[0], &release, sizeof(release));
            lease.Release();
            _exit(written == static_cast<ssize_t>(sizeof(result)) ? 0 : 1);
        }
        children.emplace_back(child);
    }
    (void)close(resultPipe[1]);
    (void)close(releasePipe[0]);

    std::vector<LeaseChildResult> results;
    while (results.size() < children.size()) {
        LeaseChildResult result = {};
        ssize_t readSize = read(resultPipe[0], &result, sizeof(result));
        if (readSize != static_cast<ssize_t>(sizeof(result))) {
            break;
        }
        results.emplace_back(result);
    }
    (void)close(resultPipe[0]);
    (void)close(releasePipe[1]);
    for (pid_t child : children) {
        int32_t status = 0;
        EXPECT_EQ(waitpid(child, &status, 0), child);
        EXPECT_TRUE(WIFEXITED(status));
        if (WIFEXITED(status)) {
            EXPECT_EQ(WEXITSTATUS(status), 0);
        }
    }
    std::sort(results.begin(), results.end(), [](const LeaseChildResult &left, const LeaseChildResult &right) {
        return left.inputIndex < right.inputIndex;
    });
    return results;
}

}

TEST(TestStandaloneSlotLease, single_lazy_process_does_not_wait_for_slot_count)
{
    CleanupLeaseShm();
    StandaloneSlotLease lease;
    uint32_t slotIndex = UINT32_MAX;
    EXPECT_EQ(lease.Acquire(4, slotIndex), BIO_OK);
    EXPECT_EQ(slotIndex, 0U);
    EXPECT_EQ(lease.PublishReady(), BIO_OK);
    lease.Release();
    int32_t shmFd = shm_open(GetLeaseShmName().c_str(), O_RDWR, 0);
    EXPECT_EQ(shmFd, -1);
    if (shmFd >= 0) {
        (void)close(shmFd);
    }
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, truncated_header_returns_error_without_crashing)
{
    CleanupLeaseShm();
    StandaloneSlotLease lease;
    uint32_t slotIndex = UINT32_MAX;
    ASSERT_EQ(lease.Acquire(1, slotIndex), BIO_OK);
    ASSERT_EQ(ftruncate(lease.mFd, 0), 0);
    EXPECT_EQ(lease.PublishReady(), BIO_INNER_ERR);
    lease.Release();
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, first_process_resets_unexpected_sized_storage)
{
    CleanupLeaseShm();
    const std::string shmName = GetLeaseShmName();
    int32_t fd = shm_open(shmName.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    constexpr size_t incompatibleSize = sizeof(TestSlotLeaseHeader) + sizeof(uint64_t);
    ASSERT_EQ(ftruncate(fd, incompatibleSize), 0);
    ASSERT_EQ(close(fd), 0);

    auto results = RunLeaseProcesses(4, 4);
    ASSERT_EQ(results.size(), 4U);
    std::set<uint32_t> slots;
    for (const auto &result : results) {
        EXPECT_EQ(result.result, BIO_OK);
        slots.insert(result.slotIndex);
    }
    EXPECT_EQ(slots, (std::set<uint32_t>{ 0, 1, 2, 3 }));
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, concurrent_processes_claim_unique_slots)
{
    CleanupLeaseShm();
    auto results = RunLeaseProcesses(4, 4);
    ASSERT_EQ(results.size(), 4U);
    std::set<uint32_t> slots;
    for (const auto &result : results) {
        EXPECT_EQ(result.result, BIO_OK);
        slots.insert(result.slotIndex);
    }
    EXPECT_EQ(slots, (std::set<uint32_t>{ 0, 1, 2, 3 }));
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, reports_not_ready_when_all_slots_are_owned)
{
    CleanupLeaseShm();
    auto results = RunLeaseProcesses(3, 2);
    ASSERT_EQ(results.size(), 3U);
    uint32_t successCount = 0;
    uint32_t fullCount = 0;
    for (const auto &result : results) {
        successCount += result.result == BIO_OK ? 1U : 0U;
        fullCount += result.result == BIO_NOT_READY ? 1U : 0U;
    }
    EXPECT_EQ(successCount, 2U);
    EXPECT_EQ(fullCount, 1U);
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, reclaims_slot_after_owner_process_dies)
{
    CleanupLeaseShm();
    int32_t readyPipe[2] = { -1, -1 };
    ASSERT_EQ(pipe(readyPipe), 0);
    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        (void)close(readyPipe[0]);
        StandaloneSlotLease lease;
        uint32_t slotIndex = UINT32_MAX;
        int32_t ret = lease.Acquire(1, slotIndex);
        if (ret == BIO_OK) {
            ret = lease.PublishReady();
        }
        (void)write(readyPipe[1], &ret, sizeof(ret));
        pause();
        _exit(1);
    }
    (void)close(readyPipe[1]);
    int32_t childResult = BIO_ERR;
    ASSERT_EQ(read(readyPipe[0], &childResult, sizeof(childResult)), static_cast<ssize_t>(sizeof(childResult)));
    (void)close(readyPipe[0]);
    ASSERT_EQ(childResult, BIO_OK);
    ASSERT_EQ(kill(child, SIGKILL), 0);
    ASSERT_EQ(waitpid(child, nullptr, 0), child);

    StandaloneSlotLease lease;
    uint32_t slotIndex = UINT32_MAX;
    EXPECT_EQ(lease.Acquire(1, slotIndex), BIO_OK);
    EXPECT_EQ(slotIndex, 0U);
    lease.Release();
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, reclaims_slot_from_zombie_owner)
{
    CleanupLeaseShm();
    int32_t readyPipe[2] = { -1, -1 };
    ASSERT_EQ(pipe(readyPipe), 0);
    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        (void)close(readyPipe[0]);
        StandaloneSlotLease lease;
        uint32_t slotIndex = UINT32_MAX;
        int32_t ret = lease.Acquire(1, slotIndex);
        if (ret == BIO_OK) {
            ret = lease.PublishReady();
        }
        (void)write(readyPipe[1], &ret, sizeof(ret));
        _exit(0);
    }
    (void)close(readyPipe[1]);
    int32_t childResult = BIO_ERR;
    ASSERT_EQ(read(readyPipe[0], &childResult, sizeof(childResult)), static_cast<ssize_t>(sizeof(childResult)));
    (void)close(readyPipe[0]);
    ASSERT_EQ(childResult, BIO_OK);
    siginfo_t childInfo = {};
    ASSERT_EQ(waitid(P_PID, child, &childInfo, WEXITED | WNOWAIT), 0);
    ASSERT_EQ(childInfo.si_pid, child);

    StandaloneSlotLease lease;
    uint32_t slotIndex = UINT32_MAX;
    EXPECT_EQ(lease.Acquire(1, slotIndex), BIO_OK);
    EXPECT_EQ(slotIndex, 0U);
    lease.Release();
    ASSERT_EQ(waitpid(child, nullptr, 0), child);
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, keeps_slot_while_other_threads_remain_alive)
{
    CleanupLeaseShm();
    int32_t readyPipe[2] = { -1, -1 };
    int32_t releasePipe[2] = { -1, -1 };
    ASSERT_EQ(pipe(readyPipe), 0);
    ASSERT_EQ(pipe(releasePipe), 0);
    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        (void)close(readyPipe[0]);
        (void)close(releasePipe[1]);
        auto *lease = new StandaloneSlotLease;
        uint32_t slotIndex = UINT32_MAX;
        int32_t ret = lease->Acquire(1, slotIndex);
        if (ret == BIO_OK) {
            ret = lease->PublishReady();
        }
        pthread_t worker = {};
        auto *workerContext = new LeaseWorkerContext{ releasePipe[0], lease };
        bool workerStarted = ret == BIO_OK && pthread_create(&worker, nullptr, WaitForPipeClose, workerContext) == 0;
        if (ret == BIO_OK && !workerStarted) {
            ret = BIO_ERR;
        }
        ssize_t written = write(readyPipe[1], &ret, sizeof(ret));
        (void)close(readyPipe[1]);
        if (ret != BIO_OK || written != static_cast<ssize_t>(sizeof(ret))) {
            if (!workerStarted) {
                delete workerContext;
                delete lease;
            }
            _exit(1);
        }
        // Exit only the thread-group leader. The raw syscall avoids pthread_exit's forced C++ stack unwind through
        // GTest's exception handler while the worker retains and later releases the lease.
        (void)syscall(SYS_exit, 0);
        __builtin_unreachable();
    }
    (void)close(readyPipe[1]);
    (void)close(releasePipe[0]);
    int32_t childResult = BIO_ERR;
    ssize_t readSize = read(readyPipe[0], &childResult, sizeof(childResult));
    (void)close(readyPipe[0]);
    EXPECT_EQ(readSize, static_cast<ssize_t>(sizeof(childResult)));
    EXPECT_EQ(childResult, BIO_OK);

    bool leaderIsZombie = childResult == BIO_OK && WaitForProcessState(child, 'Z');
    EXPECT_TRUE(leaderIsZombie);
    if (leaderIsZombie) {
        StandaloneSlotLease contender;
        uint32_t contenderSlot = UINT32_MAX;
        EXPECT_EQ(contender.Acquire(1, contenderSlot), BIO_NOT_READY);
    }

    (void)close(releasePipe[1]);
    int32_t status = 0;
    EXPECT_EQ(waitpid(child, &status, 0), child);
    EXPECT_TRUE(WIFEXITED(status));
    if (WIFEXITED(status)) {
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, returns_not_ready_when_initializer_dies)
{
    CleanupLeaseShm();
    int32_t readyPipe[2] = { -1, -1 };
    ASSERT_EQ(pipe(readyPipe), 0);
    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        (void)close(readyPipe[0]);
        StandaloneSlotLease lease;
        uint32_t slotIndex = UINT32_MAX;
        int32_t ret = lease.Acquire(2, slotIndex);
        (void)write(readyPipe[1], &ret, sizeof(ret));
        pause();
        _exit(1);
    }
    (void)close(readyPipe[1]);
    int32_t childResult = BIO_ERR;
    ASSERT_EQ(read(readyPipe[0], &childResult, sizeof(childResult)), static_cast<ssize_t>(sizeof(childResult)));
    (void)close(readyPipe[0]);
    ASSERT_EQ(childResult, BIO_OK);
    ASSERT_EQ(kill(child, SIGKILL), 0);
    ASSERT_EQ(waitpid(child, nullptr, 0), child);

    StandaloneSlotLease observerLease;
    uint32_t observerSlot = UINT32_MAX;
    EXPECT_EQ(observerLease.Acquire(2, observerSlot), BIO_NOT_READY);

    StandaloneSlotLease nextGenerationLease;
    uint32_t nextGenerationSlot = UINT32_MAX;
    EXPECT_EQ(nextGenerationLease.Acquire(2, nextGenerationSlot), BIO_OK);
    EXPECT_EQ(nextGenerationLease.PublishReady(), BIO_OK);
    nextGenerationLease.Release();
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, cleans_up_failed_initialization_without_live_owner)
{
    CleanupLeaseShm();
    const std::string shmName = GetLeaseShmName();
    int32_t fd = shm_open(shmName.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(ftruncate(fd, sizeof(TestSlotLeaseHeader)), 0);
    TestSlotLeaseHeader header = {};
    header.magic = SLOT_LEASE_MAGIC;
    header.version = SLOT_LEASE_VERSION;
    header.structSize = sizeof(TestSlotLeaseHeader);
    header.slotCount = 4;
    header.initializationState = INITIALIZATION_STATE_FAILED;
    ASSERT_EQ(pwrite(fd, &header, sizeof(header), 0), static_cast<ssize_t>(sizeof(header)));
    ASSERT_EQ(close(fd), 0);

    auto results = RunLeaseProcesses(4, 4);
    ASSERT_EQ(results.size(), 4U);
    std::set<uint32_t> slots;
    for (const auto &result : results) {
        EXPECT_EQ(result.result, BIO_OK);
        slots.insert(result.slotIndex);
    }
    EXPECT_EQ(slots, (std::set<uint32_t>{ 0, 1, 2, 3 }));
    int32_t staleFd = shm_open(shmName.c_str(), O_RDWR, 0);
    EXPECT_EQ(staleFd, -1);
    if (staleFd >= 0) {
        (void)close(staleFd);
    }

    StandaloneSlotLease nextGenerationLease;
    uint32_t nextGenerationSlot = UINT32_MAX;
    EXPECT_EQ(nextGenerationLease.Acquire(1, nextGenerationSlot), BIO_OK);
    EXPECT_EQ(nextGenerationLease.PublishReady(), BIO_OK);
    nextGenerationLease.Release();
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, failed_initialization_with_live_owner_is_not_retried)
{
    CleanupLeaseShm();
    StandaloneSlotLease initializer;
    uint32_t slot = UINT32_MAX;
    ASSERT_EQ(initializer.Acquire(2, slot), BIO_OK);
    TestSlotLeaseHeader header = {};
    ASSERT_EQ(pread(initializer.mFd, &header, sizeof(header), 0), static_cast<ssize_t>(sizeof(header)));
    header.initializationState = INITIALIZATION_STATE_FAILED;
    ASSERT_EQ(pwrite(initializer.mFd, &header, sizeof(header), 0), static_cast<ssize_t>(sizeof(header)));
    StandaloneSlotLease contender;
    EXPECT_EQ(contender.Acquire(2, slot), BIO_NOT_READY);
    EXPECT_FALSE(contender.IsAcquired());
    initializer.Release();
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, waiting_process_does_not_retry_failed_generation)
{
    CleanupLeaseShm();
    StandaloneSlotLease initializer;
    uint32_t slot = UINT32_MAX;
    ASSERT_EQ(initializer.Acquire(2, slot), BIO_OK);
    int32_t readyPipe[2] = { -1, -1 };
    ASSERT_EQ(pipe(readyPipe), 0);
    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        (void)close(readyPipe[0]);
        char ready = 1;
        (void)write(readyPipe[1], &ready, sizeof(ready));
        (void)close(readyPipe[1]);
        StandaloneSlotLease waiter;
        uint32_t childSlot = UINT32_MAX;
        auto ret = waiter.Acquire(2, childSlot);
        _exit(ret == BIO_NOT_READY ? 0 : 1);
    }
    (void)close(readyPipe[1]);
    char ready = 0;
    EXPECT_EQ(read(readyPipe[0], &ready, sizeof(ready)), static_cast<ssize_t>(sizeof(ready)));
    (void)close(readyPipe[0]);
    // The child has no other blocking operation after announcing that it is ready.
    EXPECT_TRUE(WaitForProcessState(child, 'S'));
    initializer.Release();
    int32_t status = 0;
    EXPECT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, rejects_slot_count_change_while_a_slot_is_owned)
{
    CleanupLeaseShm();
    StandaloneSlotLease firstLease;
    uint32_t slotIndex = UINT32_MAX;
    ASSERT_EQ(firstLease.Acquire(2, slotIndex), BIO_OK);
    ASSERT_EQ(firstLease.PublishReady(), BIO_OK);

    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        StandaloneSlotLease changedLease;
        uint32_t changedSlot = UINT32_MAX;
        int32_t ret = changedLease.Acquire(3, changedSlot);
        _exit(ret == BIO_INVALID_PARAM ? 0 : 1);
    }
    int32_t status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    firstLease.Release();
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, replacement_process_reuses_free_slot_with_surviving_owner)
{
    CleanupLeaseShm();
    StandaloneSlotLease survivor;
    uint32_t survivorSlot = UINT32_MAX;
    ASSERT_EQ(survivor.Acquire(2, survivorSlot), BIO_OK);
    ASSERT_EQ(survivor.PublishReady(), BIO_OK);
    constexpr uint32_t JOIN_ATTEMPTS = 2U;
    for (uint32_t attempt = 0; attempt < JOIN_ATTEMPTS; ++attempt) {
        pid_t child = fork();
        ASSERT_GE(child, 0);
        if (child == 0) {
            StandaloneSlotLease joiningLease;
            uint32_t joiningSlot = UINT32_MAX;
            auto ret = joiningLease.Acquire(2, joiningSlot);
            // Exit without Release: the next process must reclaim only this slot.
            _exit(ret == BIO_OK && joiningSlot != survivorSlot ? 0 : 1);
        }
        int32_t status = 0;
        ASSERT_EQ(waitpid(child, &status, 0), child);
        ASSERT_TRUE(WIFEXITED(status));
        EXPECT_EQ(WEXITSTATUS(status), 0);
    }
    survivor.Release();
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, forked_child_cannot_release_parent_slot)
{
    CleanupLeaseShm();
    StandaloneSlotLease parentLease;
    uint32_t parentSlot = UINT32_MAX;
    ASSERT_EQ(parentLease.Acquire(1, parentSlot), BIO_OK);
    ASSERT_EQ(parentLease.PublishReady(), BIO_OK);

    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        parentLease.Release();
        uint32_t childSlot = UINT32_MAX;
        int32_t ret = parentLease.Acquire(1, childSlot);
        _exit(ret == BIO_NOT_READY ? 0 : 1);
    }
    int32_t status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);

    parentLease.Release();
    CleanupLeaseShm();
}
