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

#include <algorithm>
#include <cerrno>
#include <string>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gtest/gtest.h"

#define private public
#include "standalone_device_id_gather.h"
#undef private

using namespace ock::bio;

namespace {

struct GatherChildResult {
    uint32_t inputIndex;
    int32_t result;
    uint32_t virtualDeviceIndex;
};

std::string GetGatherShmName()
{
    std::string shmName;
    EXPECT_EQ(StandaloneDeviceIdGather::BuildShmName(shmName), BIO_OK);
    return shmName;
}

void CleanupGatherShm()
{
    std::string shmName = GetGatherShmName();
    if (!shmName.empty()) {
        (void)shm_unlink(shmName.c_str());
    }
}

std::vector<GatherChildResult> RunGatherProcesses(const std::vector<uint32_t> &logicDeviceIds,
    uint32_t deviceCount, uint64_t timeoutMs)
{
    int32_t pipeFds[2] = { -1, -1 };
    if (pipe(pipeFds) != 0) {
        ADD_FAILURE() << "Create result pipe failed, errno:" << errno;
        return {};
    }

    std::vector<pid_t> children;
    for (uint32_t index = 0; index < logicDeviceIds.size(); ++index) {
        pid_t child = fork();
        if (child < 0) {
            ADD_FAILURE() << "Fork gather child failed, errno:" << errno;
            break;
        }
        if (child == 0) {
            (void)close(pipeFds[0]);
            GatherChildResult childResult = { index, BIO_ERR, 0 };
            childResult.result = StandaloneDeviceIdGather::Gather(logicDeviceIds[index], deviceCount,
                childResult.virtualDeviceIndex, timeoutMs);
            ssize_t written = write(pipeFds[1], &childResult, sizeof(childResult));
            (void)close(pipeFds[1]);
            _exit(written == static_cast<ssize_t>(sizeof(childResult)) ? 0 : 1);
        }
        children.emplace_back(child);
    }
    (void)close(pipeFds[1]);

    std::vector<GatherChildResult> results;
    while (results.size() < children.size()) {
        GatherChildResult childResult = {};
        ssize_t readSize;
        do {
            readSize = read(pipeFds[0], &childResult, sizeof(childResult));
        } while (readSize < 0 && errno == EINTR);
        if (readSize != static_cast<ssize_t>(sizeof(childResult))) {
            break;
        }
        results.emplace_back(childResult);
    }
    (void)close(pipeFds[0]);

    for (pid_t child : children) {
        int32_t status = 0;
        pid_t waited = waitpid(child, &status, 0);
        if (waited != child) {
            ADD_FAILURE() << "Wait gather child failed, child:" << child << ", errno:" << errno;
            continue;
        }
        EXPECT_TRUE(WIFEXITED(status));
        if (WIFEXITED(status)) {
            EXPECT_EQ(WEXITSTATUS(status), 0);
        }
    }
    std::sort(results.begin(), results.end(), [](const GatherChildResult &left, const GatherChildResult &right) {
        return left.inputIndex < right.inputIndex;
    });
    return results;
}

}

TEST(TestStandaloneDeviceIdGather, single_device_maps_to_zero_without_shared_memory)
{
    uint32_t virtualDeviceIndex = UINT32_MAX;
    EXPECT_EQ(StandaloneDeviceIdGather::Gather(37, 1, virtualDeviceIndex, 100), BIO_OK);
    EXPECT_EQ(virtualDeviceIndex, 0U);
}

TEST(TestStandaloneDeviceIdGather, processes_map_unsorted_logic_ids_to_sorted_indices)
{
    CleanupGatherShm();
    std::vector<uint32_t> logicDeviceIds = { 11, 2, 7, 4 };
    auto results = RunGatherProcesses(logicDeviceIds, logicDeviceIds.size(), 2000);

    ASSERT_EQ(results.size(), logicDeviceIds.size());
    std::vector<uint32_t> expectedIndices = { 3, 0, 2, 1 };
    for (size_t index = 0; index < results.size(); ++index) {
        EXPECT_EQ(results[index].result, BIO_OK);
        EXPECT_EQ(results[index].virtualDeviceIndex, expectedIndices[index]);
    }

    std::string shmName = GetGatherShmName();
    int32_t fd = shm_open(shmName.c_str(), O_RDWR | O_CLOEXEC, 0);
    EXPECT_EQ(fd, -1);
    EXPECT_EQ(errno, ENOENT);
    if (fd >= 0) {
        (void)close(fd);
    }
}

TEST(TestStandaloneDeviceIdGather, duplicate_logic_id_fails_the_round)
{
    CleanupGatherShm();
    auto results = RunGatherProcesses({ 5, 5 }, 2, 1000);

    ASSERT_EQ(results.size(), static_cast<size_t>(2));
    EXPECT_EQ(results[0].result, BIO_INVALID_PARAM);
    EXPECT_EQ(results[1].result, BIO_INVALID_PARAM);
    CleanupGatherShm();
}

TEST(TestStandaloneDeviceIdGather, missing_participant_times_out)
{
    CleanupGatherShm();
    uint32_t virtualDeviceIndex = UINT32_MAX;
    EXPECT_EQ(StandaloneDeviceIdGather::Gather(5, 2, virtualDeviceIndex, 50), BIO_NOT_READY);
    CleanupGatherShm();
}

TEST(TestStandaloneDeviceIdGather, dead_process_slot_is_reset_for_the_next_round)
{
    CleanupGatherShm();
    pid_t staleChild = fork();
    ASSERT_GE(staleChild, 0);
    if (staleChild == 0) {
        uint32_t virtualDeviceIndex = 0;
        (void)StandaloneDeviceIdGather::Gather(9, 2, virtualDeviceIndex, 5000);
        _exit(0);
    }
    (void)usleep(100000);
    ASSERT_EQ(kill(staleChild, SIGKILL), 0);
    ASSERT_EQ(waitpid(staleChild, nullptr, 0), staleChild);

    auto results = RunGatherProcesses({ 9, 3 }, 2, 2000);
    ASSERT_EQ(results.size(), static_cast<size_t>(2));
    EXPECT_EQ(results[0].result, BIO_OK);
    EXPECT_EQ(results[0].virtualDeviceIndex, 1U);
    EXPECT_EQ(results[1].result, BIO_OK);
    EXPECT_EQ(results[1].virtualDeviceIndex, 0U);
    CleanupGatherShm();
}
