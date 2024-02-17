/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <csignal>
#include <sys/resource.h>
#include "htracer.h"
#include "bio_client.h"

using namespace ock::bio;
std::atomic<bool> gDaemonRunning = { false };
std::shared_ptr<Bio> gCurrentCache = nullptr;

struct PerfTestParam {
    bool done;
    uint32_t tid;
    int32_t result;
    sem_t sem;
    uint32_t length;
    uint32_t count;
};

typedef void *(*perfTestRunner)(void *param);

static void usage()
{
    std::cout << "Invalid command! \n"
        "\tlist: list caches\n"
        "\tcreate cache: create [tenantId] [affinity] [strategy]\n"
        "\topen cache: open [tenantId]\n"
        "\tdestroy cache: destroy [tenantId]\n"
        "\tput value to cache: put [key] [filePath] [length] [sliceId]\n"
        "\tget value from cache: get [key] [offset] [length] [location] [filePath]\n"
        "\tdelete key: delete [key] [location]\n"
        "\tshow view: show [pt/node/trace] [all/affinity]\n"
        "\tperf test: perf [rw] [bs(Kb)] [ioDepth] [size(Mb)]\n"
        "\texit: exit console\n";
}

static void HandleList()
{
    auto caches = BioService::ListCache();
    if (caches.empty()) {
        std::cout << "\tNo cache is available:" << std::endl;
        return;
    }
    uint32_t i = 0;
    for (const auto &cache : caches) {
        std::cout << "Cache#" << i++ << std::endl;
        std::cout << "\tTenantId:" << cache.second->GetTenantId() << std::endl;
        std::cout << "\tAffinity:" << cache.second->GetAffinityPolicy() << std::endl;
        std::cout << "\tStrategy:" << cache.second->GetWriteStrategy() << std::endl;
    }
}

static void HandleCreate(std::vector<std::string> cmds)
{
    auto tenantId = std::stoul(cmds[1]);
    auto affinity = std::stoul(cmds[2]);
    auto strategy = std::stoul(cmds[3]);
    BioService::Descriptor desc = { tenantId, static_cast<AffinityStrategy>(affinity),
                                    static_cast<WriteStrategy>(strategy)};
    auto cache = BioService::CreateCache(desc);
    if (cache == nullptr) {
        std::cout << "Create cache failed." << std::endl;
    } else {
        std::cout << "Create and open cache success, tenantId:" << tenantId << std::endl;
        gCurrentCache = cache;
    }
}

static void HandleOpen(std::vector<std::string> cmds)
{
    auto tenantId = std::stoul(cmds[1]);
    auto cache = BioService::GetCache(tenantId);
    if (cache == nullptr) {
        std::cout << "The cache does not exist, tenantId:" << tenantId << std::endl;
    } else {
        std::cout << "Open cache success, tenantId:" << tenantId << std::endl;
        gCurrentCache = cache;
    }
}

static void HandleDestroy(std::vector<std::string> cmds)
{
    auto tenantId = std::stoul(cmds[1]);
    BioService::DestroyCache(tenantId);
    std::cout << "Destroy cache success, tenantId:" << tenantId << std::endl;
}

static void HandlePut(std::vector<std::string> cmds)
{
    auto key = cmds[1].c_str();
    auto filePath = cmds[2].c_str();
    auto length = std::stoull(cmds[3]);
    auto sliceId = std::stoul(cmds[4]);

    Bio::ObjLocation location{};
    auto ret = gCurrentCache->CalculateLocation(sliceId, location);
    if (ret != RET_CACHE_OK) {
        std::cout << "Calculate location failed. result:" << ret << std::endl;
        return;
    }
    std::cout << "Location info: " << BioClient::Instance()->ParseLocation(location) << std::endl;

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        std::cout << "open file failed, file:" << filePath << std::endl;
        return;
    }
    char *value = new char[length];
    if (fread(value, sizeof(char), length, fp) != length) {
        std::cout << "Read value from file failed, errno:" << errno << std::endl;
        delete[] value;
        fclose(fp);
        return;
    }

    ret = gCurrentCache->Put(key, value, length, location);
    if (ret != RET_CACHE_OK) {
        std::cout << "Failed to put a value. result:" << ret << std::endl;
    } else {
        std::cout << "Put value success, key:" << key << ", length:" << length << std::endl;
    }
    delete[] value;
    fclose(fp);
}

static void HandleGet(std::vector<std::string> cmds)
{
    auto key = cmds[1].c_str();
    auto offset = std::stoull(cmds[2]);
    auto length = std::stoull(cmds[3]);
    auto location = std::stoull(cmds[4]);
    auto filePath = cmds[5].c_str();
    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "w")) == nullptr) {
        std::cout << "open file failed, file:" << filePath << std::endl;
        return;
    }
    char *value = new char[length];
    Bio::ObjLocation locationInfo{ location, 0 };
    uint64_t realLen = length;
    auto ret = gCurrentCache->Get(key, offset, length, locationInfo, value, realLen);
    if (ret != RET_CACHE_OK) {
        std::cout << "Failed to get a value. result:" << ret << std::endl;
    } else {
        std::cout << "Get value success, key:" << key << ", offset:" << offset << ", length:" << length <<
            ", realLen:" << realLen <<", location:" << locationInfo.location[0] << std::endl;
        if (fwrite(value, sizeof(char), realLen, fp) != realLen) {
            std::cout << "Write value to file failed, errno:" << errno << std::endl;
        }
    }
    delete[] value;
    fclose(fp);
}

static void HandleDelete(std::vector<std::string> cmds)
{
    auto key = cmds[1].c_str();
    auto location = std::stoull(cmds[2]);
    Bio::ObjLocation locationInfo{ location, 0 };
    auto ret = gCurrentCache->Delete(key, locationInfo);
    if (ret != RET_CACHE_OK) {
        std::cout << "Failed to delete a key. result:" << ret << std::endl;
    } else {
        std::cout << "Delete key success, key:" << key << ", location:" << locationInfo.location[0] << std::endl;
    }
}

static void HandleShow(std::vector<std::string> cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "pt") {
        if (cmds.size() != 3) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return;
        }
        cType = cmds[2].c_str();
        std::string type(cType);
        if (type == "all") {
            std::map<uint16_t, CmPtInfo> ptView = BioClient::Instance()->GetMirror()->GetPtView();
            std::cout << "Pt view:" << std::endl;
            for (auto &ptEntry : ptView) {
                std::cout << ptEntry.second.ToString() << std::endl;
            }
        } else if (type == "affinity") {
            std::vector<uint16_t> ptList = BioClient::Instance()->GetMirror()->ListLocalAffinityPt();
            std::cout << "Local affinity pt list:" << std::endl;
            for (auto &entry : ptList) {
                std::cout << " " << entry;
            }
            std::cout << std::endl;
        }
    } else if (viewType == "node") {
        if (cmds.size() != 2) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return;
        }
        std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioClient::Instance()->GetMirror()->GetNodeView();
        std::cout << "Node view:" << std::endl;
        for (auto &nodeEntry : nodeView) {
            std::cout << nodeEntry.second.ToString() << std::endl;
        }
        std::cout << "Local Node:" << std::endl;
        CmNodeId localNode = BioClient::Instance()->GetMirror()->GetLocalNodeInfo();
        std::cout << localNode.ToString() << std::endl;
    } else if (viewType == "trace") {
        if (cmds.size() != 2) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return;
        }
        auto info = ock::htracer::GetTraceInfo();
        std::cout << info << std::endl;
    }
}

static std::unordered_map<std::string, Bio::ObjLocation> gLocation;

static void *PerfTestPutImpl(void *param)
{
    auto *getParam = (PerfTestParam *)param;
    static std::atomic<uint32_t> sliceId(0);
    static std::atomic<int32_t> keyIndex(1);

    Bio::ObjLocation location{};
    auto ret = gCurrentCache->CalculateLocation((++sliceId), location);
    if (ret != RET_CACHE_OK) {
        std::cout << "Calculate location failed. result:" << ret << std::endl;
        getParam->result = ret;
        getParam->done = true;
        return nullptr;
    }

    char *value = new char[getParam->length];
    memset(value, 66, getParam->length);
    char key[128];

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        sprintf(key, "file_%d_%d", getParam->tid, keyIndex.load());
        ret = gCurrentCache->Put(key, value, getParam->length, location);
        if (ret != RET_CACHE_OK) {
            getParam->result = ret;
            break;
        }
        keyIndex++;
        gLocation.emplace(key, location);
    }

    delete[] value;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

static void *PerfTestGetImpl(void *param)
{
    auto *getParam = (PerfTestParam *)param;
    char *value = new char[getParam->length];
    char key[128];
    static std::atomic<int32_t> keyIndex(1);

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        sprintf(key, "file_%d_%d", getParam->tid, keyIndex.load());
        auto iter = gLocation.find(key);
        if (iter == gLocation.end()) {
            getParam->result = BIO_ERR;
            break;
        }
        uint64_t realLen = 0;
        auto ret = gCurrentCache->Get(key, 0, getParam->length, iter->second, value, realLen);
        if (ret != RET_CACHE_OK) {
            getParam->result = ret;
            break;
        }
        keyIndex++;
    }

    delete[] value;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

static void HandlePerf(std::vector<std::string> cmds)
{
    if (gCurrentCache == nullptr) {
        std::cout << "Create and open a cache first!" << std::endl;
        return;
    }
    auto rw = cmds[1].c_str();
    auto bs = (std::stoul(cmds[2]) * 1024);
    auto ioDepth = std::stoul(cmds[3]);
    auto size = (std::stoul(cmds[4]) * 1024 * 1024);
    auto count = size / bs;

    perfTestRunner runner = nullptr;
    if (memcmp(rw, "read", sizeof("read")) == 0) {
        runner = PerfTestGetImpl;
    } else if (memcmp(rw, "write", sizeof("write")) == 0) {
        runner = PerfTestPutImpl;
    } else {
        std::cout <<"Invalid rw type:" << rw << std::endl;
        return;
    }

    std::cout << "Perf test start, rw:" << rw << ", bs:" << bs << ", ioDepth:" << ioDepth << ", size:" << size <<
        ", count:" << count << "." << std::endl;
    auto *th = (pthread_t *)malloc(sizeof(pthread_t) * ioDepth);
    auto *param = (PerfTestParam *)malloc(sizeof(PerfTestParam) * ioDepth);
    if (th == nullptr || param == nullptr) {
        std::cout <<"Malloc memory failed." << std::endl;
        return;
    }
    for (uint32_t i = 0; i < ioDepth; i++) {
        param[i].done = false;
        param[i].tid = i;
        param[i].result = RET_CACHE_OK;
        sem_init(&param[i].sem, 0, 0);
        param[i].length = bs;
        param[i].count = count;
    }

    struct timeval startT, stopT;
    gettimeofday(&startT, nullptr);
    for (uint32_t i = 0; i < ioDepth; i++) {
        int ret = pthread_create(&th[i], nullptr, runner, &param[i]);
        if (ret != 0) {
            std::cout << "Perf test create pthread failed, ret:" << ret <<std::endl;
            free(param);
            free(th);
            return;
        }
    }

    for (uint32_t j = 0; j < ioDepth; j++) {
        while (!param[j].done) {
            sem_wait(&param[j].sem);
            j = 0;
        }
    }

    gettimeofday(&stopT, nullptr);
    for (uint32_t k = 0; k < ioDepth; k++) {
        if (param[k].result != 0) {
            std::cout << "Perf test return failed, tid:" << k << ", ret:" << param[k].result << std::endl;
            free(param);
            free(th);
            return;
        }
    }

    float cost_sec = stopT.tv_sec - startT.tv_sec;
    float cost_usec = stopT.tv_usec - startT.tv_usec;
    float time_use = cost_sec * 1000000U + cost_usec;
    auto totalCount = static_cast<double>(count * ioDepth) ;
    auto totalSize = static_cast<double>(count * bs);
    double dataPerf = static_cast<double>((totalSize * 1000000U) / 1048576U) / time_use;
    double iops = static_cast<double>(totalSize * 1000000U) / time_use;
    int bwFactor = 1;

    time_t rawtime;
    struct tm *timeinfo = nullptr;
    struct tm timebuf{};
    rawtime = time(nullptr);
    timeinfo = localtime_r(&rawtime, &timebuf);
    printf("Perf Test Result: @ %s\n", asctime(timeinfo));
    printf("  IO depth                   : %lu\n", ioDepth);
    printf("  IO size                    : %lu\n", bs);
    printf("  total IO count             : %d\n", (int)totalCount);
    printf("  total spent                : %.2f ms\n", time_use / 1000U);
    printf("  throughput                 : %.4f MB/s\n", dataPerf * bwFactor);
    printf("  IOPS                       : %.2f /s\n", iops);
    printf("  latency                    : %.2f (us)\n", time_use / totalCount);
    printf("Perf Test End.\n");

    free(param);
    free(th);
}

int32_t HandleCmd(const std::string &cmd)
{
    std::vector<std::string> cmds;
    StrUtil::Split(cmd, " ", cmds);
    if (cmds.empty()) {
        usage();
        return 0;
    }

    std::string cmdType = cmds[0];
    if (cmdType == "list") {
        HandleList();
        return 0;
    }  else if (cmdType == "create") {
        if (cmds.size() != 4) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return 0;
        }
        HandleCreate(cmds);
        return 0;
    } else if (cmdType == "open") {
        if (cmds.size() != 2) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return 0;
        }
        HandleOpen(cmds);
        return 0;
    } else if (cmdType == "destroy") {
        if (cmds.size() != 2) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return 0;
        }
        HandleDestroy(cmds);
        return 0;
    } else if (cmdType == "put") {
        if (gCurrentCache == nullptr) {
            std::cout << "Create and open a cache first!" << std::endl;
            return 0;
        }
        if (cmds.size() != 5) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return 0;
        }
        HandlePut(cmds);
        return 0;
    } else if (cmdType == "get") {
        if (gCurrentCache == nullptr) {
            std::cout << "Create and open a cache first!" << std::endl;
            return 0;
        }
        if (cmds.size() != 6) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return 0;
        }
        HandleGet(cmds);
        return 0;
    } else if (cmdType == "delete") {
        if (gCurrentCache == nullptr) {
            std::cout << "Create and open a cache first!" << std::endl;
            return 0;
        }
        if (cmds.size() != 3) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return 0;
        }
        HandleDelete(cmds);
        return 0;
    } else if (cmdType == "show") {
        if (cmds.size() < 2) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return 0;
        }
        HandleShow(cmds);
        return 0;
    } else if (cmdType == "perf") {
        if (cmds.size() != 5) {
            std::cout << "Input parameters failed!, num:" << cmds.size() << std::endl;
            return 0;
        }
        HandlePerf(cmds);
        return 0;
    } else if (cmdType == "exit") {
        return 1;
    } else {
        usage();
    }
    return 0;
}

void HandleSigterm(int signum)
{
    (void)signum;

    // already existed, do nothing.
    if (!gDaemonRunning) {
        std::cout << "Already exited!" << std::endl;
        return;
    }

    // disable core dump when stopping
    struct rlimit coreLimiter = {
            .rlim_cur = 0,
            .rlim_max = 0
    };

    int result = setrlimit(RLIMIT_CORE, &coreLimiter);
    if (UNLIKELY(result != 0)) {
        std::cout << "Failed to disable core dump, errno " << errno << std::endl;
    }

    gDaemonRunning = false;
}

int main(int argc, char **argv)
{
    struct sigaction termSa {};
    termSa.sa_handler = &HandleSigterm;
    sigaction(SIGTERM, &termSa, nullptr);

    auto ret = BioService::Initialize();
    if (ret != RET_CACHE_OK) {
        std::cout << "Initialize bio service failed, ret " << ret << std::endl;
        return -1;
    }
    gDaemonRunning = true;
    std::cout << "BoostIO service running." << std::endl;

    while (true) {
        std::string cmd;
        std::cout << "> ";
        getline(std::cin, cmd);
        if (HandleCmd(cmd) != 0) {
            break;
        }
    }
    return 0;
}