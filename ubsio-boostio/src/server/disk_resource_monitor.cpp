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

#include "disk_resource_monitor.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "bdm_core.h"
#include "bio_config_instance.h"
#include "bio_file_util.h"
#include "bio_log.h"

namespace {
constexpr uint64_t SECTOR_SIZE_BYTES = 512;
constexpr uint32_t BANDWIDTH_SAMPLE_INTERVAL_MS = 200;

struct DiskIoSnapshot {
    bool valid = false;
    uint64_t sectorsRead = 0;
    uint64_t sectorsWritten = 0;
};

bool GetBandwidthSysPath(const std::string &diskPath, std::string &sysPath)
{
    errno = 0;
    if (!ock::bio::FileUtil::GetBlockDeviceSysPath(diskPath, sysPath)) {
        int error = errno;
        LOG_WARN("Resolve bandwidth sysfs path failed, diskPath:" << diskPath << ", errno:" << error <<
            ", error:" << std::strerror(error) << ".");
        return false;
    }
    if (sysPath.compare(0, 5, "/sys/") != 0) {
        LOG_WARN("Disk bandwidth is unavailable because path is not a block device, diskPath:" << diskPath <<
            ", resolvedPath:" << sysPath << ".");
        return false;
    }
    return true;
}

DiskIoSnapshot ReadDiskIoSnapshot(const std::string &diskPath, const std::string &sysPath)
{
    errno = 0;
    std::ifstream statFile(sysPath + "/stat");
    if (!statFile.is_open()) {
        int error = errno;
        LOG_WARN("Open disk statistics failed, diskPath:" << diskPath << ", sysPath:" << sysPath <<
            ", errno:" << error << ", error:" << std::strerror(error) << ".");
        return {};
    }

    uint64_t readsCompleted = 0;
    uint64_t readsMerged = 0;
    uint64_t readMilliseconds = 0;
    uint64_t writesCompleted = 0;
    uint64_t writesMerged = 0;
    DiskIoSnapshot result;
    statFile >> readsCompleted >> readsMerged >> result.sectorsRead >> readMilliseconds >>
        writesCompleted >> writesMerged >> result.sectorsWritten;
    result.valid = !statFile.fail();
    if (!result.valid) {
        LOG_WARN("Parse disk statistics failed, diskPath:" << diskPath << ", sysPath:" << sysPath <<
            ", expectedFields:7, streamState:" << static_cast<uint32_t>(statFile.rdstate()) <<
            ", eof:" << statFile.eof() << ", fail:" << statFile.fail() << ", bad:" << statFile.bad() << ".");
    }
    return result;
}

uint64_t CalculateBandwidth(uint64_t before, uint64_t after, uint64_t elapsedNanoseconds)
{
    if (after < before || elapsedNanoseconds == 0) {
        return 0;
    }
    long double bytes = static_cast<long double>(after - before) * SECTOR_SIZE_BYTES;
    long double bytesPerSecond = bytes * 1000000000.0L / elapsedNanoseconds;
    return static_cast<uint64_t>(bytesPerSecond);
}

}

namespace ock {
namespace bio {

void DiskResourceMonitor::Query(DiskResourcesDesc *disks, uint16_t &diskNum)
{
    diskNum = 0;
    if (disks == nullptr) {
        return;
    }

    const auto &diskPaths = BioConfig::Instance()->GetDaemonConfig().diskList;
    diskNum = static_cast<uint16_t>(std::min<size_t>(diskPaths.size(), DISK_RESOURCE_MAX_NUM));
    if (diskNum == 0) {
        return;
    }

    std::vector<std::string> bandwidthSysPaths(diskNum);
    std::vector<bool> bandwidthSysPathValid(diskNum, false);
    std::vector<DiskIoSnapshot> before(diskNum);
    bool hasValidSnapshot = false;
    for (uint16_t index = 0; index < diskNum; ++index) {
        disks[index] = {};
        disks[index].diskId = index;
        disks[index].status = static_cast<uint16_t>(BdmGetDiskStatus(index));
        std::snprintf(disks[index].path, sizeof(disks[index].path), "%s", diskPaths[index].c_str());
        bandwidthSysPathValid[index] = GetBandwidthSysPath(diskPaths[index], bandwidthSysPaths[index]);
        if (bandwidthSysPathValid[index]) {
            before[index] = ReadDiskIoSnapshot(diskPaths[index], bandwidthSysPaths[index]);
            hasValidSnapshot = hasValidSnapshot || before[index].valid;
        }
    }

    auto sampleStart = std::chrono::steady_clock::now();
    if (hasValidSnapshot) {
        std::this_thread::sleep_for(std::chrono::milliseconds(BANDWIDTH_SAMPLE_INTERVAL_MS));
    }
    auto sampleEnd = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(sampleEnd - sampleStart).count();

    for (uint16_t index = 0; index < diskNum; ++index) {
        if (before[index].valid) {
            auto after = ReadDiskIoSnapshot(diskPaths[index], bandwidthSysPaths[index]);
            if (after.valid) {
                if (elapsed <= 0 || after.sectorsRead < before[index].sectorsRead ||
                    after.sectorsWritten < before[index].sectorsWritten) {
                    LOG_WARN("Invalid disk statistics delta, diskPath:" << diskPaths[index] <<
                        ", elapsedNanoseconds:" << elapsed << ", beforeReadSectors:" << before[index].sectorsRead <<
                        ", afterReadSectors:" << after.sectorsRead << ", beforeWriteSectors:" <<
                        before[index].sectorsWritten << ", afterWriteSectors:" << after.sectorsWritten << ".");
                } else {
                    disks[index].readBandwidth = CalculateBandwidth(
                        before[index].sectorsRead, after.sectorsRead, static_cast<uint64_t>(elapsed));
                    disks[index].writeBandwidth = CalculateBandwidth(
                        before[index].sectorsWritten, after.sectorsWritten, static_cast<uint64_t>(elapsed));
                    disks[index].totalBandwidth = disks[index].readBandwidth + disks[index].writeBandwidth;
                    disks[index].bandwidthValid = 1;
                }
            }
        }
    }
}

} // namespace bio
} // namespace ock
