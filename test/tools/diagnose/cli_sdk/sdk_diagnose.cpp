//
// Created by j30026471 on 2024/2/28.
//
#include <chrono>
#include <iostream>
#include <memory>
#include <csignal>
#include <sys/resource.h>
#include <regex>
#include <condition_variable>
#include <semaphore.h>
#include "cli.h"
#include "htracer.h"
#include "bio_client.h"
#include "bio_lock.h"
#include "sdk_diagnose.h"

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
uint64_t gTenantId = UINT64_MAX;
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

static std::unordered_map<std::string, ObjLocation> gLocation;
static ReadWriteLock gLocationLock;

static void BioSdkDebugProcess(int argc, char *argv[]) noexcept;
static void BioSdkDebugHelp(char *command, int detail) noexcept;
static void HandleListCache();
static void HandleCreate(std::vector<std::string> cmds);
static void HandleOpen(std::vector<std::string> cmds);
static void HandleDestroy(std::vector<std::string> cmds);
static void HandlePut(std::vector<std::string> cmds);
static void HandleGet(std::vector<std::string> cmds);
static void HandleList(std::vector<std::string> cmds);
static void HandleStat(std::vector<std::string> cmds);
static void HandleLoad(std::vector<std::string> cmds);
static void HandleDelete(std::vector<std::string> cmds);
static void HandleQos(std::vector<std::string> cmds);
static void HandleShow(std::vector<std::string> cmds);
static void *PerfTestPutImpl(void *param);
static void *PerfTestGetImpl(void *param);
static void HandlePerf(std::vector<std::string> cmds);

static bool mInited = false;
int diagnose::BioSdkCommand::Initialize() noexcept
{
    if (mInited) {
        return 0;
    }
    CLI_CMD_S command;
    strncpy(command.szCommand, "sdk", CLI_MAX_COMMAND_LEN);
    strncpy(command.szDescription, "sdk commands.", CLI_MAX_CMD_DESC_LEN);
    command.fnCmdDo = BioSdkDebugProcess;
    command.fnPrintCmdHelp = BioSdkDebugHelp;
    mInited = true;
    return CLI_RegCmd(&command);
}

void diagnose::BioSdkCommand::Destroy() noexcept
{
    CLI_UnRegCmd((char *)"sdk");
}

static void HandleListCache()
{
    auto caches = BioService::ListCache();
    if (caches.empty()) {
        CLI_PrintBuf("No cache is available.\n");
        return;
    }
    uint32_t i = 0;
    for (const auto &cache : caches) {
        CLI_PrintBuf("Cache#%u\n", i++);
        CLI_PrintBuf("\tTenantId:%llu\n", cache.tenantId);
        CLI_PrintBuf("\tAffinity:%u\n", cache.affinity);
        CLI_PrintBuf("\tStrategy:%u\n", cache.strategy);
    }
}

static void HandleCreate(std::vector<std::string> cmds)
{
    for (int i = 1; i <= 3; i++) {
        if (!std::regex_match(cmds[i], pattern)) {
            CLI_PrintBuf("Invalid input.\n");
            return;
        }
    }
    uint32_t tenantId = 0;
    uint32_t affinity = 0;
    uint32_t strategy = 0;
    try {
        tenantId = std::stoul(cmds[1]);
        affinity = std::stoul(cmds[2]);
        strategy = std::stoul(cmds[3]);
    } catch (std::exception e) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }
    CacheDescriptor desc = { tenantId, static_cast<AffinityStrategy>(affinity),
                             static_cast<WriteStrategy>(strategy)};
    auto ret = BioCreateCache(desc);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Create cache failed, result:%d.\n", ret);
    } else {
        CLI_PrintBuf("Create cache success, tenantId:%u.\n", tenantId);
        gTenantId = tenantId;
    }
}

static void HandleOpen(std::vector<std::string> cmds)
{
    if (!std::regex_match(cmds[1], pattern)) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }
    uint32_t tenantId = 0;
    try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }
    CacheDescriptor desc;
    auto ret = BioGetCache(tenantId, &desc);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("The cache does not exist, tenantId:%u\n", tenantId);
    } else {
        CLI_PrintBuf("Open cache success, tenantId:%u\n", tenantId);
        gTenantId = desc.tenantId;
    }
}

static void HandleDestroy(std::vector<std::string> cmds)
{
    if (!std::regex_match(cmds[1], pattern)) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }
    uint32_t tenantId = 0;
    try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }
    auto ret = BioDestroyCache(tenantId);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Destroy cache failed, result:%d, tenantId:%u.\n", ret, tenantId);
    } else {
        CLI_PrintBuf("Destroy cache success, tenantId:%u\n", tenantId);
    }
}

static void HandlePut(std::vector<std::string> cmds)
{
    for (int i = 3; i <= 4; i++) {
        if (!std::regex_match(cmds[i], pattern)) {
            CLI_PrintBuf("Invalid input.\n");
            return;
        }
    }
    auto key = cmds[1].c_str();
    auto filePath = cmds[2].c_str();
    uint64_t length = 0;
    uint32_t sliceId = 0;
    try {
        length = std::stoull(cmds[3]);
        sliceId = std::stoul(cmds[4]);
    } catch (std::exception e) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }

    ObjLocation location{};
    auto ret = BioCalcLocation(gTenantId, sliceId, &location);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Calculate location failed, result:%d.\n", ret);
        return;
    }
    CLI_PrintBuf("Location info: %u.\n", BioClient::Instance()->ParseLocation(location));

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        CLI_PrintBuf("fopen file failed, file: %s.\n", filePath);
        return;
    }
    char *value = new char[length];
    if (fread(value, sizeof(char), length, fp) != length) {
        CLI_PrintBuf("Read value from file failed, errno:%d.\n", errno);
        delete[] value;
        fclose(fp);
        return;
    }

    ret = BioPut(gTenantId, key, value, length, location);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Failed to put a value, result:%d.\n", ret);
    } else {
        CLI_PrintBuf("Put value success, key:%s, length:%llu.\n", key, length);
    }
    delete[] value;
    fclose(fp);
}

static void HandleGet(std::vector<std::string> cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!std::regex_match(cmds[i], pattern)) {
            CLI_PrintBuf("Invalid input.\n");
            return;
        }
    }
    uint64_t offset = 0;
    uint64_t length = 0;
    uint64_t location = 0;

    auto key = cmds[1].c_str();
    try {
        offset = std::stoull(cmds[2]);
        length = std::stoull(cmds[3]);
        location = std::stoull(cmds[4]);
    } catch (std::exception e) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }

    auto filePath = cmds[5].c_str();
    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "w")) == nullptr) {
        CLI_PrintBuf("fopen file failed, file:%s.\n", filePath);
        return;
    }
    char *value = new char[length];
    ObjLocation locationInfo{ location, 0 };
    uint64_t realLen = length;
    auto ret = BioGet(gTenantId, key, offset, length, locationInfo, value, &realLen);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Failed to get a value, result:%d.\n", ret);
    } else {
        CLI_PrintBuf("Get value success, key:%s, offset:%llu, length:%llu, realLen:%llu, location:%llu.\n",
            key, offset, length, realLen, locationInfo.location[0]);
        if (fwrite(value, sizeof(char), realLen, fp) != realLen) {
            CLI_PrintBuf("fwrite value to file failed, errno:%d.\n", errno);
        }
    }
    delete[] value;
    fclose(fp);
}

static void HandleList(std::vector<std::string> cmds)
{
    uint64_t location = 0;
    auto prefix = cmds[1].c_str();
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    auto ret = BioListAll(gTenantId, prefix, &objs, &objNum);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Failed to list all, result:%d.\n", ret);
    } else {
        CLI_PrintBuf("List all success, obj num: %lu.\n", objNum);
        for (uint32_t idx = 0; idx < objNum; idx++) {
            CLI_PrintBuf("Object#%u: key:%s, size:%u, time:%s",
                         idx, objs[idx].key, objs[idx].size, ctime(&objs[idx].time));
        }
        BioFreeListResources(objs, objNum);
    }
}

static void HandleStat(std::vector<std::string> cmds)
{
    if (!std::regex_match(cmds[2], pattern)) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }
    uint64_t location = 0;
    auto key = cmds[1].c_str();
    try {
        location = std::stoull(cmds[2]);
    } catch (std::exception e) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }
    ObjLocation locationInfo{ location, 0 };
    ObjStat keyStat;
    auto ret = BioStat(gTenantId, key, locationInfo, &keyStat);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Failed to get key stat, result:%d.\n", ret);
    } else {
        CLI_PrintBuf("Get key stat success.\n");
        CLI_PrintBuf("key:%s, location:%lu, size:%u, time:%s\n", key, locationInfo.location[0],
                     keyStat.size, ctime(&keyStat.time));
    }
}

typedef struct {
    sem_t sem;
    CResult result;
} LoadContext;

static void TestCallback(void *context, int32_t result)
{
    LoadContext* loadCtx = reinterpret_cast<LoadContext*>(context);
    loadCtx->result = static_cast<CResult>(result);
    sem_post(&(loadCtx->sem));
}

static void HandleLoad(std::vector<std::string> cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!std::regex_match(cmds[i], pattern)) {
            CLI_PrintBuf("Invalid input.\n");
            return;
        }
    }
    uint64_t offset = 0;
    uint64_t length = 0;
    uint64_t location = 0;
    auto key = cmds[1].c_str();
    try {
        offset = std::stoull(cmds[2]);
        length = std::stoull(cmds[3]);
        location = std::stoull(cmds[4]);
    } catch (std::exception e) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }

    ObjLocation locationInfo{ location, 0 };
    LoadContext loadCtx;
    sem_init(&(loadCtx.sem), 0, 0);
    loadCtx.result = RET_CACHE_OK;
    auto ret = BioLoad(gTenantId, key, offset, length, locationInfo, TestCallback, &loadCtx);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Load failed, key:%s, result:%d.\n", key, ret);
        return;
    } else {
        sem_wait(&(loadCtx.sem));
        sem_destroy(&(loadCtx.sem));
        if (loadCtx.result == RET_CACHE_OK) {
            CLI_PrintBuf("Load success, key:%s.\n", key);
        } else {
            CLI_PrintBuf("Load failed; key:%s, result:%d.\n", key, loadCtx.result);
        }
    }
}

static void HandleDelete(std::vector<std::string> cmds)
{
    if (!std::regex_match(cmds[2], pattern)) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }
    uint64_t location = 0;
    auto key = cmds[1].c_str();
    try {
        location = std::stoull(cmds[2]);
    } catch (std::exception e) {
        CLI_PrintBuf("Invalid input.\n");
        return;
    }
    ObjLocation locationInfo{ location, 0 };
    auto ret = BioDelete(gTenantId, key, locationInfo);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Failed to delete, key%s, result:%d.\n", key, ret);
    } else {
        CLI_PrintBuf("Delete key success, key:%s, location:%lu.\n", key, locationInfo.location[0]);
    }
}

static void HandleQos(std::vector<std::string> cmds)
{
    auto cType = cmds[1].c_str();
    std::string op(cType);
    if (op == "show") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        BioQosPtr qosP = BioClient::Instance()->GetMirror()->GetQosPtr();
        std::vector<uint64_t> oriQuota;
        std::vector<uint64_t> baseQuota;
        std::vector<uint64_t> quota;
        std::vector<uint32_t> concur;
        qosP->Show(oriQuota, baseQuota, quota, concur);
        const std::string typeStr[QUOTA_BUTT] = { "Write", "Read" };
        CLI_PrintBuf("  Quota info, switch:%s \n", qosP->Switch() ? "on" : "off");
        for (uint32_t idx = 0; idx < baseQuota.size(); idx++) {
            CLI_PrintBuf("  %s: original quota:%lu, adjust quota:%lu, remain quota:%lu, concur:%u.\n",
                typeStr[idx], baseQuota[idx], quota[idx], concur[idx]);
        }
    } else if (op == "switch") {
        if (cmds.size() != 3) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        cType = cmds[2].c_str();
        std::string qosSwitch(cType);
        BioQosPtr qosP = BioClient::Instance()->GetMirror()->GetQosPtr();
        auto ret = qosP->Switch(qosSwitch);
    } else {
        CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
    }
}

static void HandleShow(std::vector<std::string> cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "pt") {
        if (cmds.size() != 3) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        cType = cmds[2].c_str();
        std::string type(cType);
        if (type == "all") {
            std::map<uint16_t, CmPtInfo> ptView = BioClient::Instance()->GetMirror()->GetPtView();
            CLI_PrintBuf("Pt view:\n");
            for (auto &ptEntry : ptView) {
                CLI_PrintBuf("%s\n", ptEntry.second.ToString().c_str());
            }
        } else if (type == "affinity") {
            std::vector<uint16_t> ptList = BioClient::Instance()->GetMirror()->ListLocalAffinityPt();
            CLI_PrintBuf("Local affinity pt list:\n");
            for (auto &entry : ptList) {
                CLI_PrintBuf(" %u\n", entry);
            }
        } else if (type == "hit") {
            auto hitRet = BioClient::Instance()->GetMirror()->ShowPtHit();
            CLI_PrintBuf("PT Hit info:\n");
            for (uint32_t idx = 0; idx < hitRet.size(); idx++) {
                CLI_PrintBuf("  PtId:%u : %lu\n", idx, hitRet[idx]);
            }
        }
    } else if (viewType == "node") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioClient::Instance()->GetMirror()->GetNodeView();
        CLI_PrintBuf("Node view:\n");
        for (auto &nodeEntry : nodeView) {
            CLI_PrintBuf("%s\n", nodeEntry.second.ToString().c_str());
        }
        CLI_PrintBuf("Local Node:");
        CmNodeId localNode = BioClient::Instance()->GetMirror()->GetLocalNodeInfo();
        CLI_PrintBuf("%s\n", localNode.ToString().c_str());
    } else {
        CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
    }
}

static void HandleSdkTrace(std::vector<std::string> cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "show") {
        auto info = ock::htracer::GetTraceInfo();
        CLI_PrintBuf(info.c_str());
    } else if (viewType == "clear") {
        ock::htracer::ClearTraceInfo();
        CLI_PrintBuf("clearing statistics sdk records succeeded.\n");
    }
}

static void *PerfTestPutImpl(void *param)
{
    auto *getParam = (PerfTestParam *)param;
    static std::atomic<uint32_t> sliceId(0);
    static std::atomic<int32_t> keyIndex(1);

    ObjLocation location{};
    auto ret = BioCalcLocation(gTenantId, (++sliceId), &location);
    if (ret != RET_CACHE_OK) {
        CLI_PrintBuf("Calculate location failed, result:%d.\n", ret);
        getParam->result = ret;
        getParam->done = true;
        return nullptr;
    }

    char *value = new char[getParam->length];
    memset(value, 66, getParam->length);
    char key[128];

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        sprintf(key, "file_%u_%d", getParam->tid, keyIndex.load());
        ret = BioPut(gTenantId, key, value, getParam->length, location);
        if (ret != RET_CACHE_OK) {
            getParam->result = ret;
            break;
        }
        keyIndex++;
        gLocationLock.LockWrite();
        gLocation.emplace(key, location);
        gLocationLock.UnLock();
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
        sprintf(key, "file_%u_%d", getParam->tid, keyIndex.load());
        auto iter = gLocation.find(key);
        if (iter == gLocation.end()) {
            getParam->result = BIO_ERR;
            break;
        }
        uint64_t realLen = 0;
        auto ret = BioGet(gTenantId, key, 0, getParam->length, iter->second, value, &realLen);
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
    if (gTenantId == UINT64_MAX) {
        CLI_PrintBuf("Create and open a cache first!\n");
        return;
    }

    uint32_t bs = 0;
    uint32_t ioDepth = 0;
    uint32_t size = 0;
    auto rw = cmds[1].c_str();
    try {
        bs = (std::stoul(cmds[2]) * 1024);
        ioDepth = std::stoul(cmds[3]);
        size = (std::stoul(cmds[4]) * 1024 * 1024);
    } catch (std::exception e) {
        CLI_PrintBuf("invalid input.\n");
        return;
    }
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

    CLI_PrintBuf("Perf test start, rw:%s, bs:%u, ioDepth:%u, size:%u, count:%u.\n", rw, bs, ioDepth, size, count);
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
            CLI_PrintBuf("Perf test return failed, tid:%u, ret:%d.\n", k, param[k].result);
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
    double dataPerf = static_cast<double>(((totalSize / 1048576U) * 1000000U / time_use) * ioDepth);
    double iops = static_cast<double>(totalCount * 1000000U) / time_use;
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
    CLI_PrintBuf("  latency                    : %.2f (us)\n", time_use / count);
    CLI_PrintBuf("Perf Test End.\n");

    free(param);
    free(th);
}

static void BioSdkDebugHelp(char *command, int detail) noexcept
{
    CLI_PrintBuf("\tlist caches: sdk list\n");
    CLI_PrintBuf("\tcreate cache: sdk create [tenantId] [affinity] [strategy]\n");
    CLI_PrintBuf("\topen cache: sdk open [tenantId]\n");
    CLI_PrintBuf("\tdestroy cache: sdk destroy [tenantId]\n");
    CLI_PrintBuf("\tput value: sdk put [key] [filePath] [length] [sliceId]\n");
    CLI_PrintBuf("\tget value: sdk get [key] [offset] [length] [location] [filePath]\n");
    CLI_PrintBuf("\tstate object: sdk stat [key] [location]\n");
    CLI_PrintBuf("\tlist all object: sdk listall [prefix]\n");
    CLI_PrintBuf("\tload object: sdk load [key] [offset] [length] [location]\n");
    CLI_PrintBuf("\tdelete object: sdk delete [key] [location]\n");
    CLI_PrintBuf("\tqos operation: sdk qos [show/switch] [on/off]\n");
    CLI_PrintBuf("\tshow view: sdk show [pt/node] [all/affinity/hit]\n");
    CLI_PrintBuf("\ttrace: sdk trace [show/clear]\n");
    CLI_PrintBuf("\tperf test: sdk perf [rw] [bs(Kb)] [ioDepth] [size(Mb)]\n");
    CLI_PrintBuf("\texit: exit console\n");
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
        HandleListCache();
    }  else if (cmdType == "create") {
        if (cmds.size() != 4) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleCreate(cmds);
    } else if (cmdType == "open") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleOpen(cmds);
    } else if (cmdType == "destroy") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleDestroy(cmds);
    } else if (cmdType == "put") {
        if (gTenantId == UINT64_MAX) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 5) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandlePut(cmds);
    } else if (cmdType == "get") {
        if (gTenantId == UINT64_MAX) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 6) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleGet(cmds);
    } else if (cmdType == "stat") {
        if (gTenantId == UINT64_MAX) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 3) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleStat(cmds);
    }  else if (cmdType == "listall") {
        if (gTenantId == UINT64_MAX) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleList(cmds);
    } else if (cmdType == "load") {
        if (gTenantId == UINT64_MAX) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 5) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleLoad(cmds);
    } else if (cmdType == "delete") {
        if (gTenantId == UINT64_MAX) {
            CLI_PrintBuf("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 3) {
            CLI_PrintBuf("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleDelete(cmds);
    } else if (cmdType == "qos") {
        if (cmds.size() < 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleQos(cmds);
    } else if (cmdType == "show") {
        if (cmds.size() < 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleShow(cmds);
    } else if (cmdType == "trace") {
        if (cmds.size() != 2) {
            CLI_PrintBuf("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleSdkTrace(cmds);
    } else if (cmdType == "perf") {
        if (cmds.size() != 5) {
            CLI_PrintBuf("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandlePerf(cmds);
    } else if (cmdType == "exit") {
        return;
    } else {
        BioSdkDebugHelp(argv[0], 1);
    }
}
