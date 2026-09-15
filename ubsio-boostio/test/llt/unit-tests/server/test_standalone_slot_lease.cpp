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
#include <sys/wait.h>
#include <unistd.h>

#include "gtest/gtest.h"

#include "bio_types.h"

#define private public
#include "standalone_slot_lease.h"
#undef private

using namespace ock::bio;

namespace {

const std::vector<std::string> DISK_PATHS = { "/dev/test0" };
const std::vector<int64_t> DISK_CAPS = { 1024 * 1024 };
constexpr uint64_t SEGMENT_SIZE = 1024;
constexpr uint32_t SLOT_LEASE_MAGIC = 0x5542534CU;
constexpr uint32_t SLOT_LEASE_VERSION = 1U;
constexpr uint32_t LAYOUT_STATE_FAILED = 3U;
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
    uint64_t layoutFingerprint;
    uint32_t layoutState;
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
            result.result = lease.Acquire(slotCount, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, result.slotIndex);
            if (result.result == BIO_OK) {
                result.result = lease.PublishLayoutReady();
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
    EXPECT_EQ(lease.Acquire(4, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, slotIndex), BIO_OK);
    EXPECT_EQ(slotIndex, 0U);
    EXPECT_EQ(lease.PublishLayoutReady(), BIO_OK);
    lease.Release();
    int32_t shmFd = shm_open(GetLeaseShmName().c_str(), O_RDWR, 0);
    EXPECT_EQ(shmFd, -1);
    if (shmFd >= 0) {
        (void)close(shmFd);
    }
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
        int32_t ret = lease.Acquire(1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, slotIndex);
        if (ret == BIO_OK) {
            ret = lease.PublishLayoutReady();
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
    EXPECT_EQ(lease.Acquire(1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, slotIndex), BIO_OK);
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
        int32_t ret = lease.Acquire(1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, slotIndex);
        if (ret == BIO_OK) {
            ret = lease.PublishLayoutReady();
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
    EXPECT_EQ(lease.Acquire(1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, slotIndex), BIO_OK);
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
        int32_t ret = lease->Acquire(1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, slotIndex);
        if (ret == BIO_OK) {
            ret = lease->PublishLayoutReady();
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
        // Transfer the lease to the worker before the thread-group leader exits.
        pthread_exit(nullptr);
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
        EXPECT_EQ(contender.Acquire(1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, contenderSlot), BIO_NOT_READY);
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

TEST(TestStandaloneSlotLease, returns_not_ready_when_layout_initializer_dies)
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
        int32_t ret = lease.Acquire(2, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, slotIndex);
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
    EXPECT_EQ(observerLease.Acquire(2, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, observerSlot), BIO_NOT_READY);

    StandaloneSlotLease nextGenerationLease;
    uint32_t nextGenerationSlot = UINT32_MAX;
    EXPECT_EQ(nextGenerationLease.Acquire(2, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, nextGenerationSlot), BIO_OK);
    EXPECT_EQ(nextGenerationLease.PublishLayoutReady(), BIO_OK);
    nextGenerationLease.Release();
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, cleans_up_failed_layout_without_live_owner)
{
    CleanupLeaseShm();
    const std::string shmName = GetLeaseShmName();
    int32_t fd = shm_open(shmName.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    ASSERT_GE(fd, 0);
    ASSERT_EQ(ftruncate(fd, sizeof(TestSlotLeaseHeader)), 0);
    void *mapping = mmap(nullptr, sizeof(TestSlotLeaseHeader), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_NE(mapping, MAP_FAILED);
    auto *header = static_cast<TestSlotLeaseHeader *>(mapping);
    *header = {};
    header->magic = SLOT_LEASE_MAGIC;
    header->version = SLOT_LEASE_VERSION;
    header->structSize = sizeof(TestSlotLeaseHeader);
    header->slotCount = 1;
    header->layoutFingerprint = StandaloneSlotLease::BuildLayoutFingerprint(
        1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE);
    header->layoutState = LAYOUT_STATE_FAILED;
    ASSERT_EQ(munmap(mapping, sizeof(TestSlotLeaseHeader)), 0);
    ASSERT_EQ(close(fd), 0);

    StandaloneSlotLease observerLease;
    uint32_t observerSlot = UINT32_MAX;
    EXPECT_EQ(observerLease.Acquire(1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, observerSlot), BIO_NOT_READY);
    int32_t staleFd = shm_open(shmName.c_str(), O_RDWR, 0);
    EXPECT_EQ(staleFd, -1);
    if (staleFd >= 0) {
        (void)close(staleFd);
    }

    StandaloneSlotLease nextGenerationLease;
    uint32_t nextGenerationSlot = UINT32_MAX;
    EXPECT_EQ(nextGenerationLease.Acquire(1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, nextGenerationSlot), BIO_OK);
    EXPECT_EQ(nextGenerationLease.PublishLayoutReady(), BIO_OK);
    nextGenerationLease.Release();
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, rejects_layout_change_while_a_slot_is_owned)
{
    CleanupLeaseShm();
    StandaloneSlotLease firstLease;
    uint32_t slotIndex = UINT32_MAX;
    ASSERT_EQ(firstLease.Acquire(2, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, slotIndex), BIO_OK);
    ASSERT_EQ(firstLease.PublishLayoutReady(), BIO_OK);

    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        StandaloneSlotLease changedLease;
        uint32_t changedSlot = UINT32_MAX;
        int32_t ret = changedLease.Acquire(3, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, changedSlot);
        _exit(ret == BIO_INVALID_PARAM ? 0 : 1);
    }
    int32_t status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    firstLease.Release();
    CleanupLeaseShm();
}

TEST(TestStandaloneSlotLease, forked_child_cannot_release_parent_slot)
{
    CleanupLeaseShm();
    StandaloneSlotLease parentLease;
    uint32_t parentSlot = UINT32_MAX;
    ASSERT_EQ(parentLease.Acquire(1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, parentSlot), BIO_OK);
    ASSERT_EQ(parentLease.PublishLayoutReady(), BIO_OK);

    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        parentLease.Release();
        uint32_t childSlot = UINT32_MAX;
        int32_t ret = parentLease.Acquire(1, DISK_PATHS, DISK_CAPS, SEGMENT_SIZE, childSlot);
        _exit(ret == BIO_NOT_READY ? 0 : 1);
    }
    int32_t status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);

    parentLease.Release();
    CleanupLeaseShm();
}
