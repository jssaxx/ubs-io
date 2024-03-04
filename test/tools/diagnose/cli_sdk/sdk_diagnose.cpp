//
// Created by j30026471 on 2024/2/28.
//
#include <chrono>
#include <iostream>
#include <memory>
#include <csignal>
#include <sys/resource.h>
#include <regex>
#include "htracer.h"
#include "bio_client.h"
#include "sdk_diagnose.h"
#include "cli.h"

#ifdef __cplusplus
extern "C" {
#endif

int SdkDiagnoseInit()
{
    return ock::bio::diagnose::BioSdkCommand::Initialize();
}

#ifdef __cplusplus
}
#endif

using namespace ock::bio;
std::shared_ptr<Bio> gCurrentCache = nullptr;
std::regex pattern("[0-9]+");
struct PerfTestParam {
    bool done;
    uint32_t tid;
    int32_t result;
    sem_t sem;
    uint32_t length;
    uint32_t count;
};

typedef void *(*perfTestRunner)(void *param);

static std::unordered_map<std::string, Bio::ObjLocation> gLocation;

static void BioSdkDebugProcess(int argc, char *argv[]) noexcept;
static void BioSdkDebugHelp(char *command, int detail) noexcept;
static void HandleList();
static void HandleCreate(std::vector<std::string> cmds);
static void HandleOpen(std::vector<std::string> cmds);
static void HandleDestroy(std::vector<std::string> cmds);
static void HandlePut(std::vector<std::string> cmds);
static void HandleGet(std::vector<std::string> cmds);
static void HandleStat(std::vector<std::string> cmds);
static void HandleLoad(std::vector<std::string> cmds);
static void HandleDelete(std::vector<std::string> cmds);
static void HandleShow(std::vector<std::string> cmds);
static void *PerfTestPutImpl(void *param);
static void *PerfTestGetImpl(void *param);
static void HandlePerf(std::vector<std::string> cmds);

int diagnose::BioSdkCommand::Initialize() noexcept
{
    CLI_CMD_S command;
    strncpy(command.szCommand, "bioSdk", CLI_MAX_COMMAND_LEN);
    strncpy(command.szDescription, "bioSdk commands.", CLI_MAX_CMD_DESC_LEN);
    command.fnCmdDo = BioSdkDebugProcess;
    command.fnPrintCmdHelp = BioSdkDebugHelp;
    auto result = CLI_RegCmd(&command);
    if (result != 0) {
        printf("register bioSdk diagnose failed.");
        return -1;
    } else {
        printf("register bioSdk diagnose succeeded\n");
    }

    return 0;
}

void diagnose::BioSdkCommand::Destroy() noexcept
{
    CLI_UnRegCmd((char *)"bioSdk");
}

static void HandleList()
{
    auto caches = BioService::ListCache();
    if (caches.empty()) {
        CLI_PrintBuf("\tNo cache is available:\n");
        return;
    }
    uint32_t i = 0;
    for (const auto &cache : caches) {
        CLI_PrintBuf("Cache#%d\n", i++);
        CLI_PrintBuf("\tTenantId:%d\n", cache.second->GetTenantId());
        CLI_PrintBuf("\tAffinity:%d\n", cache.second->GetAffinityPolicy());
        CLI_PrintBuf("\tStrategy:%d\n", cache.second->GetWriteStrategy());
    }
}

static void HandleCreate(std::vector<std::string> cmds)
{
    for (int i = 1; i <= 3; i++) {
        if (!std::regex_match(cmds[i], pattern)) {
            CLI_PrintBuf("invalid input.\n");
            return;
        }
    }
    auto tenantId = std::stoul(cmds[1]);
    auto affinity = std::stoul(cmds[2]);
    auto strategy = std::stoul(cmds[3]);
    BioService::Descriptor desc = { tenantId, static_cast<AffinityStrategy>(affinity),
                                    static_cast<WriteStrategy>(strategy)};
    auto cache = BioService::CreateCache(desc);
    if (cache == nullptr) {
        CLI_PrintBuf("Create cache failed.\n");
    } else {
        CLI_PrintBuf("Create and open cache success, tenantId:%d.\n", tenantId);
        gCurrentCache = cache;
    }
}

static void HandleOpen(std::vector<std::string> cmds)
{
    if (!std::regex_match(cmds[1], pattern)) {
        CLI_PrintBuf("invalid input.\n");
        return;
    }
    auto tenantId = std::stoul(cmds[1]);
    auto cache = BioService::GetCache(tenantId);
    if (cache == nullptr) {
        CLI_PrintBuf("The cache does not exist, tenantId:%d\n", tenantId);
    } else {
        CLI_PrintBuf("Open cache success, tenantId:%d\n", tenantId);
        gCurrentCache = cache;
    }
}

static void HandleDestroy(std::vector<std::string> cmds)
{
    if (!std::regex_match(cmds[1], pattern)) {
        CLI_PrintBuf("invalid input.\n");
        return;
    }
    auto tenantId = std::stoul(cmds[1]);
    BioService::DestroyCache(tenantId);
    CLI_PrintBuf("Destroy cache success, tenantId:%d.\n", tenantId);
}

static void HandlePut(std::vector<std::string> cmds)
{
    for (int i = 3; i <= 4; i++) {
        if (!std::regex_match(cmds[i], pattern)) {
            CLI_PrintBuf("invalid input.\n");
            return;
        }
    }
    auto key = cmds[1].c_str();
    auto filePath = cmds[2].c_str();
    auto length = std::stoull(cmds[3]);
    auto sliceId = std::stoul(cmds[4]);

    Bio::ObjLocation location{};
    auto ret = gCurrentCache->CalculateLocation(sliceId, location);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Calculate location failed. result:%d\n", ret);
        return;
    }
    CLI_PrintBuf("Location info: %u\n", BioClient::Instance()->ParseLocation(location));

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        CLI_PrintBuf("open file failed, file: %s\n", filePath);
        return;
    }
    char *value = new char[length];
    if (fread(value, sizeof(char), length, fp) != length) {
        CLI_PrintBuf("Read value from file failed, errno:%d.\n", errno);
        delete[] value;
        fclose(fp);
        return;
    }

    ret = gCurrentCache->Put(key, value, length, location);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Failed to put a value. result:%d.\n", ret);
    } else {
        CLI_PrintBuf("Put value success, key:%s, length:%d\n", key, length);
    }
    delete[] value;
    fclose(fp);
}

static void HandleGet(std::vector<std::string> cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!std::regex_match(cmds[i], pattern)) {
            CLI_PrintBuf("invalid input.\n");
            return;
        }
    }
    auto key = cmds[1].c_str();
    auto offset = std::stoull(cmds[2]);
    auto length = std::stoull(cmds[3]);
    auto location = std::stoull(cmds[4]);
    auto filePath = cmds[5].c_str();
    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "w")) == nullptr) {
        CLI_PrintBuf("open file failed, file:%s.\n", filePath);
        return;
    }
    char *value = new char[length];
    Bio::ObjLocation locationInfo{ location, 0 };
    uint64_t realLen = length;
    auto ret = gCurrentCache->Get(key, offset, length, locationInfo, value, realLen);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Failed to get a value. result:%d.\n", ret);
    } else {
        CLI_PrintBuf("Get value success, key:%s, offset:%d, length:%d, realLen:%d, location:%llu\n",
            key, offset, length, realLen, locationInfo.location[0]);
        if (fwrite(value, sizeof(char), realLen, fp) != realLen) {
            CLI_PrintBuf("Write value to file failed, errno:%d\n", errno);
        }
    }
    delete[] value;
    fclose(fp);
}

static void HandleStat(std::vector<std::string> cmds)
{
    if (!std::regex_match(cmds[2], pattern)) {
        CLI_PrintBuf("invalid input.\n");
        return;
    }
    auto key = cmds[1].c_str();
    auto location = std::stoull(cmds[2]);
    Bio::ObjLocation locationInfo{ location, 0 };
    Bio::ObjStat keyStat = gCurrentCache->Stat(key, locationInfo);
    if (keyStat.time == 0) {
        CLI_PrintBuf("Failed to get key stat.\n");
    } else {
        CLI_PrintBuf("Get key stat success.\n");
        CLI_PrintBuf("key:%s, location:%llu\n", key, locationInfo.location[0]);
        CLI_PrintBuf("size:%d\n", keyStat.size);
        CLI_PrintBuf("time:%s\n", ctime(&keyStat.time));
    }
}

void TestCallback(void *context, CResult result)
{
    int* loadFlag = reinterpret_cast<int*>(context);
    if (result == RET_CACHE_OK) {
        CLI_PrintBuf("load cache success.\n");
    } else {
        CLI_PrintBuf("load cache fail.\n");
    }
    *loadFlag = 1;
}

static void HandleLoad(std::vector<std::string> cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!std::regex_match(cmds[i], pattern)) {
            CLI_PrintBuf("invalid input.\n");
            return;
        }
    }
    auto key = cmds[1].c_str();
    auto offset = std::stoull(cmds[2]);
    auto length = std::stoull(cmds[3]);
    auto location = std::stoull(cmds[4]);
    int loadFlag = 0;

    Bio::ObjLocation locationInfo{ location, 0 };
    auto ret = gCurrentCache->Load(key, offset, length, locationInfo, TestCallback, &loadFlag);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Send to load request fail.\n");
    } else {
        CLI_PrintBuf("Send to load request success.\n");
    }
    while (loadFlag == 0) {};
}

static void HandleDelete(std::vector<std::string> cmds)
{
    if (!std::regex_match(cmds[2], pattern)) {
        CLI_PrintBuf("invalid input.\n");
        return;
    }
    auto key = cmds[1].c_str();
    auto location = std::stoull(cmds[2]);
    Bio::ObjLocation locationInfo{ location, 0 };
    auto ret = gCurrentCache->Delete(key, locationInfo);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Failed to delete a key. result:%d.\n",ret);
    } else {
        CLI_PrintBuf("Delete key success, key:%s, location:%llu\n", key, locationInfo.location[0]);
    }
}

static void HandleShow(std::vector<std::string> cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "pt") {
        if (cmds.size() != 3) {
            CLI_PrintBuf("Input parameters failed!, num:%d.\n", cmds.size());
            return;
        }
        cType = cmds[2].c_str();
        std::string type(cType);
        if (type == "all") {
            std::map<uint16_t, CmPtInfo> ptView = BioClient::Instance()->GetMirror()->GetPtView();
            CLI_PrintBuf("Pt view:\n");
            for (auto &ptEntry : ptView) {
                CLI_PrintBuf("%s\n", ptEntry.second.ToString());
            }
        } else if (type == "affinity") {
            std::vector<uint16_t> ptList = BioClient::Instance()->GetMirror()->ListLocalAffinityPt();
            CLI_PrintBuf("Local affinity pt list:\n");
            for (auto &entry : ptList) {
                CLI_PrintBuf(" %u\n", entry);
            }
        }
    } else if (viewType == "node") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d.\n", cmds.size());
            return;
        }
        std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioClient::Instance()->GetMirror()->GetNodeView();
        CLI_PrintBuf("Node view:\n");
        for (auto &nodeEntry : nodeView) {
            CLI_PrintBuf("%s\n", nodeEntry.second.ToString());
        }
        CLI_PrintBuf("Local Node:");
        CmNodeId localNode = BioClient::Instance()->GetMirror()->GetLocalNodeInfo();
        CLI_PrintBuf("%s\n", localNode.ToString());
    } else if (viewType == "trace") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d.\n", cmds.size());
            return;
        }
        auto info = ock::htracer::GetTraceInfo();
        CLI_PrintBuf("%s\n", info);
    }
}

static void *PerfTestPutImpl(void *param)
{
    auto *getParam = (PerfTestParam *)param;
    static std::atomic<uint32_t> sliceId(0);
    static std::atomic<int32_t> keyIndex(1);

    Bio::ObjLocation location{};
    auto ret = gCurrentCache->CalculateLocation((++sliceId), location);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Calculate location failed. result:%d.\n", ret);
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
    for (int i = 2; i <= 4; i++) {
        if (!std::regex_match(cmds[i], pattern)) {
            CLI_PrintBuf("invalid input.\n");
            return;
        }
    }
    if (gCurrentCache == nullptr) {
        CLI_PrintBuf("Create and open a cache first!\n");
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
        CLI_PrintBuf("Invalid rw type:%s.\n", rw);
        return;
    }

    CLI_PrintBuf("Perf test start, rw:%s, bs:%d, ioDepth:%d, size:%d, count:%d.\n", rw, bs, ioDepth, size, count);
    auto *th = (pthread_t *)malloc(sizeof(pthread_t) * ioDepth);
    auto *param = (PerfTestParam *)malloc(sizeof(PerfTestParam) * ioDepth);
    if (th == nullptr || param == nullptr) {
        CLI_PrintBuf("Malloc memory failed.\n");
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
            CLI_PrintBuf("Perf test create pthread failed, ret:%d.\n", ret);
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
            CLI_PrintBuf("Perf test return failed, tid:%d, ret:%d.\n", k, param[k].result);
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
    CLI_PrintBuf("Perf Test Result: @ %s\n", asctime(timeinfo));
    CLI_PrintBuf("  IO depth                   : %lu\n", ioDepth);
    CLI_PrintBuf("  IO size                    : %lu\n", bs);
    CLI_PrintBuf("  total IO count             : %d\n", (int)totalCount);
    CLI_PrintBuf("  total spent                : %.2f ms\n", time_use / 1000U);
    CLI_PrintBuf("  throughput                 : %.4f MB/s\n", dataPerf * bwFactor);
    CLI_PrintBuf("  IOPS                       : %.2f /s\n", iops);
    CLI_PrintBuf("  latency                    : %.2f (us)\n", time_use / totalCount);
    CLI_PrintBuf("Perf Test End.\n");

    free(param);
    free(th);
}

static void BioSdkDebugHelp(char *command, int detail) noexcept
{
    CLI_PrintBuf("list: bioSdk list caches\n");
    CLI_PrintBuf("create cache: bioSdk create [tenantId] [affinity] [strategy]\n");
    CLI_PrintBuf("open cache: bioSdk open [tenantId]\n");
    CLI_PrintBuf("destroy cache: bioSdk destroy [tenantId]\n");
    CLI_PrintBuf("put value to cache: bioSdk put [key] [filePath] [length] [sliceId]\n");
    CLI_PrintBuf("get value from cache: bioSdk get [key] [offset] [length] [location] [filePath]\n");
    CLI_PrintBuf("get key state from cache: bioSdk stat [key] [location]\n");
    CLI_PrintBuf("load key to cache: bioSdk load [key] [offset] [length] [location]\n");
    CLI_PrintBuf("delete key: bioSdk delete [key] [location]\n");
    CLI_PrintBuf("show view: bioSdk show [pt/node/trace] [all/affinity]\n");
    CLI_PrintBuf("perf test: bioSdk perf [rw] [bs(Kb)] [ioDepth] [size(Mb)]\n");
    CLI_PrintBuf("exit: exit console\n");
}

static void BioSdkDebugProcess(int argc, char *argv[]) noexcept
{
    if (argc <= 1) {
        BioSdkDebugHelp(argv[0], 1);
        return;
    }

    std::vector<std::string> cmds;
    for (int i = 1; i < argc; i++) {
        std::string str(argv[i]);
        cmds.emplace_back(str);
    }

    std::string cmdType = cmds[0];
    if (cmdType == "list") {
        HandleList();
    }  else if (cmdType == "create") {
        if (cmds.size() != 4) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleCreate(cmds);
    } else if (cmdType == "open") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleOpen(cmds);
    } else if (cmdType == "destroy") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleDestroy(cmds);
    } else if (cmdType == "put") {
        if (gCurrentCache == nullptr) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 5) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandlePut(cmds);
    } else if (cmdType == "get") {
        if (gCurrentCache == nullptr) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 6) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleGet(cmds);
    } else if (cmdType == "stat") {
        if (gCurrentCache == nullptr) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 3) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleStat(cmds);
    } else if (cmdType == "load") {
        if (gCurrentCache == nullptr) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 5) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleLoad(cmds);
    } else if (cmdType == "delete") {
        if (gCurrentCache == nullptr) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 3) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleDelete(cmds);
    } else if (cmdType == "show") {
        if (cmds.size() < 2) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleShow(cmds);
    } else if (cmdType == "perf") {
        if (cmds.size() != 5) {
            CLI_PrintBuf("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandlePerf(cmds);
    } else if (cmdType == "exit") {
        return;
    } else {
        BioSdkDebugHelp(argv[0], 1);
    }
}
