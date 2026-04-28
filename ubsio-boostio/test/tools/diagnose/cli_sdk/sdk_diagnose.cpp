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

#include <chrono>
#include <iostream>
#include <csignal>
#include <sys/resource.h>
#include <regex>
#include <semaphore.h>
#include "htracer.h"
#include "bio_client.h"
#include "bio_lock.h"
#include "bio_crc_util.h"
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
typedef void *(*perfTestRunner)(void *param);
uint64_t gTenantId = UINT64_MAX;

const std::regex& GetPattern()
{
    static const std::regex pattern("[0-9]+"); // 首次调用时初始化
    return pattern;
}

struct PerfTestParam {
    bool done;
    uint32_t tid;
    int32_t result;
    sem_t sem;
    uint32_t length;
    uint32_t count;
};
static std::unordered_map<std::string, ObjLocation> gLocation;
static ReadWriteLock gLocationLock;

bool ock::bio::diagnose::BioSdkCommand::mInited = false;
void* ock::bio::diagnose::BioSdkCommand::mHandler = nullptr;
CliRegCmdFuncPtr ock::bio::diagnose::BioSdkCommand::mRegOp = nullptr;
CliUnRegCmdFuncPtr ock::bio::diagnose::BioSdkCommand::mUnRegOp = nullptr;
CliPrintBufFuncPtr ock::bio::diagnose::BioSdkCommand::mPrintOp = nullptr;

int32_t diagnose::BioSdkCommand::LoadSymbols()
{
    const char* soFileName = "libcli_agent.so";
    mHandler = dlopen(soFileName, RTLD_NOW);
    if (mHandler == nullptr) {
        CLIENT_LOG_ERROR("Failed to open library() " << soFileName << " dlopen, error " << dlerror());
        return BIO_INNER_ERR;
    }

    mRegOp = reinterpret_cast<CliRegCmdFuncPtr>(dlsym(mHandler, "cli_register_command"));
    mUnRegOp = reinterpret_cast<CliUnRegCmdFuncPtr>(dlsym(mHandler, "cli_unregister_command"));
    mPrintOp = reinterpret_cast<CliPrintBufFuncPtr>(dlsym(mHandler, "cli_print_buffer"));
    if (mRegOp == nullptr || mUnRegOp == nullptr || mPrintOp == nullptr) {
        CLIENT_LOG_ERROR("Failed to load function.");
        dlclose(mHandler);
        return BIO_INNER_ERR;
    }

    return BIO_OK;
}

int diagnose::BioSdkCommand::Initialize() noexcept
{
    if (mInited) {
        return 0;
    }

    auto ret = LoadSymbols();
    if (ret != BIO_OK) {
        CLIENT_LOG_ERROR("Failed to load symbols.");
        return ret;
    }

    CliCommand command;
    strncpy(command.command, "sdk", CLI_MAX_COMMAND_LEN);
    strncpy(command.description, "sdk commands.", CLI_MAX_CMD_DESC_LEN);
    command.handler = BioSdkDebugProcess;
    command.help_handler = BioSdkDebugHelp;
    auto result = mRegOp(&command);
    if (result == 0) {
        mInited = true;
    }
    return result;
}

void diagnose::BioSdkCommand::Destroy() noexcept
{
    if (mInited && mUnRegOp) {
        mUnRegOp((char *)"sdk");
        mInited = false;
    }

    if (mHandler) {
        dlclose(mHandler);
        mHandler = nullptr;
    }
}

void diagnose::BioSdkCommand::HandleListCache()
{
    auto caches = BioService::ListCache();
    if (caches.empty()) {
        mPrintOp("No cache is available.\n");
        return;
    }
    uint32_t i = 0;
    for (const auto &cache : caches) {
        mPrintOp("Cache#%u\n", i++);
        mPrintOp("\tTenantId:%llu\n", cache.tenantId);
        mPrintOp("\tAffinity:%u\n", cache.affinity);
        mPrintOp("\tStrategy:%u\n", cache.strategy);
    }
}

void diagnose::BioSdkCommand::HandleCreate(const std::vector<std::string> &cmds)
{
    for (int i = 1; i <= 3; i++) {
        if (!std::regex_match(cmds[i], GetPattern())) {
            mPrintOp("Invalid input.\n");
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
        mPrintOp("Invalid input.\n");
        return;
    }
    CacheDescriptor desc = { tenantId, static_cast<AffinityStrategy>(affinity),
                             static_cast<WriteStrategy>(strategy)};
    auto ret = BioCreateCache(desc);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Create cache failed, result:%d.\n", ret);
    } else {
        mPrintOp("Create cache success, tenantId:%u.\n", tenantId);
        gTenantId = tenantId;
    }
}

void diagnose::BioSdkCommand::HandleOpen(const std::vector<std::string> &cmds)
{
    if (!std::regex_match(cmds[1], GetPattern())) {
        mPrintOp("Invalid input.\n");
        return;
    }
    uint32_t tenantId = 0;
    try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    CacheDescriptor desc;
    auto ret = BioGetCache(tenantId, &desc);
    if (ret != RET_CACHE_OK) {
        mPrintOp("The cache does not exist, tenantId:%u\n", tenantId);
    } else {
        mPrintOp("Open cache success, tenantId:%u\n", tenantId);
        gTenantId = desc.tenantId;
    }
}

void diagnose::BioSdkCommand::HandleDestroy(const std::vector<std::string> &cmds)
{
    if (!std::regex_match(cmds[1], GetPattern())) {
        mPrintOp("Invalid input.\n");
        return;
    }
    uint32_t tenantId = 0;
    try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    auto ret = BioDestroyCache(tenantId);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Destroy cache failed, result:%d, tenantId:%u.\n", ret, tenantId);
    } else {
        mPrintOp("Destroy cache success, tenantId:%u\n", tenantId);
    }
}

void diagnose::BioSdkCommand::HandlePut(const std::vector<std::string> &cmds)
{
    for (int i = 3; i <= 4; i++) {
        if (!std::regex_match(cmds[i], GetPattern())) {
            mPrintOp("Invalid input.\n");
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
        mPrintOp("Invalid input.\n");
        return;
    }

    ObjLocation location{};
    auto ret = BioCalcLocation(gTenantId, sliceId, &location);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Calculate location failed, result:%d.\n", ret);
        return;
    }
    mPrintOp("Location info: %u.\n", BioClient::Instance()->ParseLocation(location));

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        mPrintOp("fopen file failed, file: %s.\n", filePath);
        return;
    }
    char *value = new char[length];
    if (fread(value, sizeof(char), length, fp) != length) {
        mPrintOp("Read value from file failed, errno:%d.\n", errno);
        delete[] value;
        fclose(fp);
        return;
    }

    ret = BioPut(gTenantId, key, value, length, location);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to put a value, result:%d.\n", ret);
    } else {
        mPrintOp("Put value success, key:%s, length:%llu.\n", key, length);
    }
    delete[] value;
    fclose(fp);
}

void diagnose::BioSdkCommand::HandleGet(const std::vector<std::string> &cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!std::regex_match(cmds[i], GetPattern())) {
            mPrintOp("Invalid input.\n");
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
        mPrintOp("Invalid input.\n");
        return;
    }

    auto filePath = cmds[5].c_str();
    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "w")) == nullptr) {
        mPrintOp("fopen file failed, file:%s.\n", filePath);
        return;
    }
    char *value = new char[length];
    ObjLocation locationInfo{ location, 0 };
    uint64_t realLen = length;
    auto ret = BioGet(gTenantId, key, offset, length, locationInfo, value, &realLen);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to get a value, result:%d.\n", ret);
    } else {
        mPrintOp("Get value success, key:%s, offset:%llu, length:%llu, realLen:%llu, location:%llu.\n",
            key, offset, length, realLen, locationInfo.location[0]);
        if (fwrite(value, sizeof(char), realLen, fp) != realLen) {
            mPrintOp("fwrite value to file failed, errno:%d.\n", errno);
        }
    }
    delete[] value;
    fclose(fp);
}

void diagnose::BioSdkCommand::HandleList(const std::vector<std::string> &cmds)
{
    uint64_t location = 0;
    auto prefix = cmds[1].c_str();
    ObjStat *objs = nullptr;
    uint64_t objNum = 0;
    auto ret = BioListAll(gTenantId, prefix, &objs, &objNum);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to list all, result:%d.\n", ret);
    } else {
        mPrintOp("List all success, obj num: %lu.\n", objNum);
        for (uint32_t idx = 0; idx < objNum; idx++) {
            mPrintOp("Object#%u: key:%s, size:%u, time:%s",
                         idx, objs[idx].key, objs[idx].size, ctime(&objs[idx].time));
        }
        BioFreeListResources(&objs, objNum);
    }
}

void diagnose::BioSdkCommand::HandleNotifyUpdatePrepare(const std::vector<std::string> &cmds)
{
    uint32_t tenantId = 0;
        try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    auto ret = BioNotifyUpgradePrepare(tenantId);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to notify update, result:%d.\n", ret);
    } else {
        mPrintOp("Notify update prepare success, result:%d.\n", ret);
    }
}

void diagnose::BioSdkCommand::HandleNotifyUpdateFinish(const std::vector<std::string> &cmds)
{
    uint32_t tenantId = 0;
        try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    auto ret = BioNotifyUpgradeFinish(tenantId);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to notify update, result:%d.\n", ret);
    } else {
        mPrintOp("Notify update finish success, result:%d.\n", ret);
    }
}

void diagnose::BioSdkCommand::HandleCheckUpdateReady(const std::vector<std::string> &cmds)
{
    uint32_t tenantId = 0;
    try {
        tenantId = std::stoul(cmds[1]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    auto ret = BioCheckUpgradeReady(tenantId);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to check update, result:%d.\n", ret);
    } else {
        mPrintOp("Check update ready success, result:%d.\n");
    }
}

void diagnose::BioSdkCommand::HandleStat(const std::vector<std::string> &cmds)
{
    if (!std::regex_match(cmds[NO_2], GetPattern())) {
        mPrintOp("Invalid input.\n");
        return;
    }
    uint64_t location = 0;
    auto key = cmds[1].c_str();
    try {
        location = std::stoull(cmds[NO_2]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    ObjLocation locationInfo{ location, 0 };
    ObjStat keyStat;
    auto ret = BioStat(gTenantId, key, locationInfo, &keyStat);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to get key stat, result:%d.\n", ret);
    } else {
        mPrintOp("Get key stat success.\n");
        mPrintOp("key:%s, location:%lu, size:%u, time:%s\n", key, locationInfo.location[0],
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

void diagnose::BioSdkCommand::HandleLoad(const std::vector<std::string> &cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!std::regex_match(cmds[i], GetPattern())) {
            mPrintOp("Invalid input.\n");
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
        mPrintOp("Invalid input.\n");
        return;
    }

    ObjLocation locationInfo{ location, 0 };
    LoadContext loadCtx;
    sem_init(&(loadCtx.sem), 0, 0);
    loadCtx.result = RET_CACHE_OK;
    auto ret = BioLoad(gTenantId, key, offset, length, locationInfo, TestCallback, &loadCtx);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Load failed, key:%s, result:%d.\n", key, ret);
        return;
    } else {
        sem_wait(&(loadCtx.sem));
        sem_destroy(&(loadCtx.sem));
        if (loadCtx.result == RET_CACHE_OK) {
            mPrintOp("Load success, key:%s.\n", key);
        } else {
            mPrintOp("Load failed; key:%s, result:%d.\n", key, loadCtx.result);
        }
    }
}

void diagnose::BioSdkCommand::HandleDelete(const std::vector<std::string> &cmds)
{
    if (!std::regex_match(cmds[2], GetPattern())) {
        mPrintOp("Invalid input.\n");
        return;
    }
    uint64_t location = 0;
    auto key = cmds[1].c_str();
    try {
        location = std::stoull(cmds[2]);
    } catch (std::exception e) {
        mPrintOp("Invalid input.\n");
        return;
    }
    ObjLocation locationInfo{ location, 0 };
    auto ret = BioDelete(gTenantId, key, locationInfo);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to delete, key%s, result:%d.\n", key, ret);
    } else {
        mPrintOp("Delete key success, key:%s, location:%lu.\n", key, locationInfo.location[0]);
    }
}

void diagnose::BioSdkCommand::HandleAddDisk(const std::vector<std::string> &cmds)
{
    auto diskPath = cmds[1].c_str();
    auto ret = BioAddDisk(diskPath);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Failed to add a disk, result:%d.\n", ret);
    } else {
        mPrintOp("Add disk success, diskPath:%s, tenantId:%u\n", diskPath);
    }
}

void diagnose::BioSdkCommand::HandleShow(const std::vector<std::string> &cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "pt") {
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        cType = cmds[2].c_str();
        std::string type(cType);
        if (type == "all") {
            std::map<uint16_t, CmPtInfo> ptView = BioClient::Instance()->GetMirror()->GetPtView();
            mPrintOp("Pt view:\n");
            for (auto &ptEntry : ptView) {
                mPrintOp("%s\n", ptEntry.second.ToString().c_str());
            }
        } else if (type == "affinity") {
            std::vector<uint16_t> ptList = BioClient::Instance()->GetMirror()->ListLocalAffinityPt();
            mPrintOp("Local affinity pt list:\n");
            for (auto &entry : ptList) {
                mPrintOp(" %u\n", entry);
            }
        }
    } else if (viewType == "node") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        std::map<CmNodeId, CmNodeInfo, CmNodeIdCmp> nodeView = BioClient::Instance()->GetMirror()->GetNodeView();
        mPrintOp("Node view:\n");
        for (auto &nodeEntry : nodeView) {
            mPrintOp("%s\n", nodeEntry.second.ToString().c_str());
        }
        mPrintOp("Local Node:");
        CmNodeId localNode = BioClient::Instance()->GetMirror()->GetLocalNodeInfo();
        mPrintOp("%s\n", localNode.ToString().c_str());
    } else if (viewType == "flow") {
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        uint16_t ptId = std::stoull(cmds[2]);
        auto flowInst = BioClient::Instance()->GetMirror()->Query(ptId);
        if (UNLIKELY(flowInst == nullptr)) {
            mPrintOp("flow is null pt:%u", ptId);
            return;
        }
        mPrintOp("flow id: %lu.\n", flowInst->FlowId());
        mPrintOp("flow status: %d.\n", flowInst->IsNormal() ? 1 : 0);
        mPrintOp("flow offset: %lu.\n", flowInst->GetOffset());
        mPrintOp("flow index: %lu.\n", flowInst->GetIndex());
    } else {
        mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
    }
}

void diagnose::BioSdkCommand::HandleShowCacheHit(const std::vector<std::string> &cmds)
{
    CacheHitFinalDesc desc;
    CacheHitFinalDesc *nodeDesc = NULL;
    uint64_t nodeNum = 0;
    auto ret = BioShowCacheHitRatio(&desc, &nodeDesc, &nodeNum);
    if (ret != RET_CACHE_OK) {
        BioFreeCacheHitPtr(&nodeDesc, nodeNum);
        mPrintOp("Show Cache Hit failed, result:%d.\n", ret);
        return;
    }

    double rCacheHitMemRatio = desc.wCacheTotalCount != 0 ?
                               (double)desc.rCacheHitMemCount / (double)desc.wCacheTotalCount : 0;
    double rCacheHitDiskRatio = desc.wCacheTotalCount != 0 ?
                               (double)desc.rCacheHitDiskCount / (double)desc.wCacheTotalCount : 0;
    double rCacheHitRatio = desc.wCacheTotalCount != 0 ?
                            (double)desc.rCacheHitCount / (double)desc.wCacheTotalCount : 0;
    double wCacheHitMemRatio = desc.wCacheHitMemCount != 0 ?
                               (double)desc.wCacheHitMemCount / (double)desc.wCacheTotalCount : 0;
    double wCacheHitDiskRatio = desc.wCacheHitDiskCount != 0 ?
                               (double)desc.wCacheHitDiskCount / (double)desc.wCacheTotalCount : 0;
    double wCacheHitRatio = desc.wCacheTotalCount != 0 ?
                            (double)desc.wCacheHitCount / (double)desc.wCacheTotalCount : 0;
    double backendHitRatio = desc.wCacheTotalCount != 0 ?
                          (double)desc.backendHitCount / (double)desc.wCacheTotalCount : 0;
    double totalCacheHitRatio = rCacheHitRatio + wCacheHitRatio;
    mPrintOp("--------------------------------\n");
    mPrintOp("all node totalCacheHitRatio :%.2f%%.\n", totalCacheHitRatio * 100);
    mPrintOp("all node rCacheHitMemRatio :%.2f%%.\n", rCacheHitMemRatio * 100);
    mPrintOp("all node rCacheHitDiskRatio :%.2f%%.\n", rCacheHitDiskRatio * 100);
    mPrintOp("all node rCacheHitRatio :%.2f%%.\n", rCacheHitRatio * 100);
    mPrintOp("all node wCacheHitMemRatio :%.2f%%.\n", wCacheHitMemRatio * 100);
    mPrintOp("all node wCacheHitDiskRatio :%.2f%%.\n", wCacheHitDiskRatio * 100);
    mPrintOp("all node wCacheHitRatio :%.2f%%.\n", wCacheHitRatio * 100);
    mPrintOp("all node backendHitRatio :%.2f%%.\n", backendHitRatio * 100);
    mPrintOp("----------------------------------\n");
    for (int i = 0; i < nodeNum; i++) {
        uint16_t nodeId = nodeDesc[i].nodeId;
        double nodeRCacheHitMemRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                   (double)nodeDesc[i].rCacheHitMemCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeRCacheHitDiskRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                       (double)nodeDesc[i].rCacheHitDiskCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeRCacheHitRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                    (double)nodeDesc[i].rCacheHitCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeWCacheHitMemRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                       (double)nodeDesc[i].wCacheHitMemCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeWCacheHitDiskRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                        (double)nodeDesc[i].wCacheHitDiskCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeWCacheHitRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                    (double)nodeDesc[i].wCacheHitCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeBackendHitRatio = nodeDesc[i].wCacheTotalCount != 0 ?
                                    (double)nodeDesc[i].backendHitCount / (double)nodeDesc[i].wCacheTotalCount : 0;
        double nodeTotalCacheHitRatio = nodeRCacheHitRatio + nodeWCacheHitRatio;
        mPrintOp("node: %d totalHitCacheRatio :%.2f%%.\n", nodeId, nodeTotalCacheHitRatio * 100);
        mPrintOp("node: %d rCacheHitMemRatio :%.2f%%.\n", nodeId, nodeRCacheHitMemRatio * 100);
        mPrintOp("node: %d rCacheHitDiskRatio :%.2f%%.\n", nodeId, nodeRCacheHitDiskRatio * 100);
        mPrintOp("node: %d rCacheHitRatio :%.2f%%.\n", nodeId, nodeRCacheHitRatio * 100);
        mPrintOp("node: %d wCacheHitMemRatio :%.2f%%.\n", nodeId, nodeWCacheHitMemRatio * 100);
        mPrintOp("node: %d wCacheHitDiskRatio :%.2f%%.\n", nodeId, nodeWCacheHitDiskRatio * 100);
        mPrintOp("node: %d wCacheHitRatio :%.2f%%.\n", nodeId, nodeWCacheHitRatio * 100);
        mPrintOp("node: %d backendHitRatio :%.2f%%.\n", nodeId, nodeBackendHitRatio * 100);
        mPrintOp("--------------------------------\n");
    }
    BioFreeCacheHitPtr(&nodeDesc, nodeNum);
}

void diagnose::BioSdkCommand::HandleShowCacheResource(const std::vector<std::string> &cmds)
{
    CacheResourcesDesc *nodeDesc = NULL;
    uint64_t nodeNum = 0;
    auto ret = BioShowCacheResource(&nodeDesc, &nodeNum);
    if (ret != RET_CACHE_OK) {
        BioFreeCacheResourcePtr(&nodeDesc, nodeNum);
        mPrintOp("Show Cache Resource failed, result:%d \n", ret);
        return;
    }
    mPrintOp("--------------------------------\n");
    for (int i = 0; i < nodeNum; i++) {
        uint16_t nodeId = nodeDesc[i].nodeId;
        if (nodeDesc[i].rCacheMemCapacity == 0 || nodeDesc[i].rCacheDiskCapacity == 0
            || nodeDesc[i].wCacheMemCapacity == 0 || nodeDesc[i].wCacheDiskCapacity == 0) {
            mPrintOp("node Capacity is zero, nodeId:%d  rCacheMemCapacity(MB):%llu  rCacheDiskCapacity(MB):%llu \n",
                         nodeId, nodeDesc[i].rCacheMemCapacity / NO_1048576,
                         nodeDesc[i].rCacheDiskCapacity / NO_1048576);
            mPrintOp("wCacheMemCapacity(MB):%llu  wCacheDiskCapacity(MB):%llu \n",
                         nodeDesc[i].wCacheMemCapacity / NO_1048576, nodeDesc[i].wCacheDiskCapacity / NO_1048576);
            continue;
        }
        mPrintOp("node: %d cache resources information(MB): \n", nodeId);
        double wCacheMemWaterLever = (double)nodeDesc[i].wCacheMemUsedSize / (double)nodeDesc[i].wCacheMemCapacity;
        double rCacheMemWaterLever = (double)nodeDesc[i].rCacheMemUsedSize / (double)nodeDesc[i].rCacheMemCapacity;
        double wCacheDiskWaterLever = (double)nodeDesc[i].wCacheDiskUsedSize / (double)nodeDesc[i].wCacheDiskCapacity;
        double rCacheDiskWaterLever = (double)nodeDesc[i].rCacheDiskUsedSize / (double)nodeDesc[i].rCacheDiskCapacity;
        mPrintOp("wCacheMemCapacity %llu   wCacheDiskCapacity %llu \n",
                     nodeDesc[i].wCacheMemCapacity / NO_1048576, nodeDesc[i].wCacheDiskCapacity / NO_1048576);
        mPrintOp("rCacheMemCapacity %llu   rCacheDiskCapacity %llu \n",
                     nodeDesc[i].rCacheMemCapacity / NO_1048576, nodeDesc[i].rCacheDiskCapacity / NO_1048576);
        mPrintOp("wCacheMemUsedSize %llu   wCacheDiskUsedSize %llu \n",
                     nodeDesc[i].wCacheMemUsedSize / NO_1048576, nodeDesc[i].wCacheDiskUsedSize / NO_1048576);
        mPrintOp("wCacheMemWaterLever %.4f%%   wCacheDiskWaterLever %.4f%% \n",
                     wCacheMemWaterLever * 100, wCacheDiskWaterLever * 100);
        mPrintOp("rCacheMemUsedSize %llu   rCacheDiskUsedSize %llu \n",
                     nodeDesc[i].rCacheMemUsedSize / NO_1048576, nodeDesc[i].rCacheDiskUsedSize / NO_1048576);
        mPrintOp("rCacheMemWaterLever %.4f%%   rCacheDiskWaterLever %.4f%% \n",
                     rCacheMemWaterLever * 100, rCacheDiskWaterLever * 100);
        mPrintOp("--------------------------------\n");
    }
    BioFreeCacheResourcePtr(&nodeDesc, nodeNum);
}

void diagnose::BioSdkCommand::HandleSdkTrace(const std::vector<std::string> &cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "show") {
        auto info = ock::htracer::GetTraceInfo();
        mPrintOp(info.c_str());
    } else if (viewType == "clear") {
        ock::htracer::ClearTraceInfo();
        mPrintOp("clearing statistics sdk records succeeded.\n");
    } else if (viewType == "open") {
        ock::htracer::HTracerSetEnable(true);
        mPrintOp("open statistics sdk records succeeded.\n");
    } else if (viewType == "close") {
        ock::htracer::HTracerSetEnable(false);
        mPrintOp("close statistics sdk records succeeded.\n");
    }
}

void* diagnose::BioSdkCommand::PerfTestPutImpl(void *param)
{
    auto *getParam = (PerfTestParam *)param;
    static std::atomic<uint32_t> sliceId(0);
    std::atomic<int32_t> keyIndex(1);

    ObjLocation location{};
    auto ret = BioCalcLocation(gTenantId, (++sliceId), &location);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Calculate location failed, result:%d.\n", ret);
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

void* diagnose::BioSdkCommand::PerfTestGetImpl(void *param)
{
    auto *getParam = (PerfTestParam *)param;
    char *value = new char[getParam->length];
    char key[128];
    std::atomic<int32_t> keyIndex(1);

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

void diagnose::BioSdkCommand::HandleBatchGet(const std::vector<std::string> &cmds)
{
    uint32_t bs = (std::stoul(cmds[1]) * 1024);
    uint32_t batchNum = std::stoul(cmds[2]);
    char key[256];
    std::vector<std::string> prepareKeys;
    std::vector<std::shared_ptr<ObjLocation>> prepareLocations;
    uint32_t keyIndex = 0;
    char **values = reinterpret_cast<char**>(malloc(sizeof(char*) * batchNum));
    for (uint32_t idx = 0; idx < batchNum; idx++) {
        sprintf(key, "file_%u_%d", getpid(), keyIndex);

        std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
        auto ret = BioCalcLocation(gTenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), location.get());
        if (ret != RET_CACHE_OK) {
            mPrintOp("Calculate location failed, result:%d.\n", ret);
            return;
        }
        char *value = new char[bs];
        ret = BioPut(gTenantId, key, value, bs, *location);
        if (ret != RET_CACHE_OK) {
            mPrintOp("Put key(%s) fail, result:%d\n", key, ret);
            return;
        }
        values[idx] = value;
        keyIndex++;
        prepareKeys.emplace_back(key);
        prepareLocations.emplace_back(location);
    }
    char **keys = reinterpret_cast<char**>(malloc(sizeof(char*) * batchNum));
    uint64_t *offsets = reinterpret_cast<uint64_t*>(malloc(sizeof(uint64_t) * batchNum));
    uint64_t *lengths = reinterpret_cast<uint64_t*>(malloc(sizeof(uint64_t) * batchNum));
    ObjLocation *locations = reinterpret_cast<ObjLocation*>(malloc(sizeof(ObjLocation) * batchNum));
    uintptr_t *valueAddrs = reinterpret_cast<uintptr_t*>(malloc(sizeof(uintptr_t) * batchNum));
    uint64_t *realLengths = reinterpret_cast<uint64_t*>(malloc(sizeof(uint64_t) * batchNum));
    int32_t *results = reinterpret_cast<int32_t*>(malloc(sizeof(int32_t) * batchNum));
    bool* existFlags = reinterpret_cast<bool*>(malloc(sizeof(bool) * batchNum));
    KeyAddrInfo *infos = reinterpret_cast<KeyAddrInfo*>(malloc(sizeof(KeyAddrInfo) * batchNum));
    if (keys == nullptr || offsets == nullptr || lengths == nullptr || locations == nullptr ||
        valueAddrs == nullptr || realLengths == nullptr || results == nullptr || existFlags == nullptr) {
        mPrintOp("Malloc fail.");
        return;
    }

    for (uint32_t i = 0; i < batchNum; i++) {
        keys[i] = reinterpret_cast<char*>(malloc(sizeof(char) * 256));
        memcpy(reinterpret_cast<void*>(keys[i]), prepareKeys[i].c_str(), prepareKeys[i].size());
        reinterpret_cast<char*>(keys[i])[prepareKeys[i].size()] = '\0';
//        keys[i] = const_cast<char*>(prepareKeys[i].c_str());
        offsets[i] = 0;
        lengths[i] = bs;
        locations[i] = *(prepareLocations[i]);
        valueAddrs[i] = 0;
        results[i] = 0;
        realLengths[i] = 0;
    }

    auto result = BioBatchExist(gTenantId, const_cast<const char **>(keys), locations, batchNum, existFlags);
    if (result != 0) {
        mPrintOp("Bio batch exist fail, ret:%d.\n", result);
        return;
    }
    for (uint32_t i = 0; i < batchNum; i++) {
        if (!existFlags[i]) {
            mPrintOp("Bio batch exit, key:%s is not exist.\n", keys[i]);
            return;
        }
    }
    sleep(5);

    result = BioBatchGet(gTenantId, const_cast<const char **>(keys), batchNum, offsets, lengths, locations, valueAddrs, realLengths, results);

    if (result != 0) {
        mPrintOp("Bio batch get fail, ret:%d.\n", result);
        return;
    }
    for (uint32_t i = 0; i < batchNum; i++) {
        if (results[i] != 0) {
            mPrintOp("Bio batch get fail, key:%s, ret:%d.\n", keys[i], result);
            return;
        }
        if (BioCrcUtil::Crc32(reinterpret_cast<void*>(values[i]), bs) != BioCrcUtil::Crc32(reinterpret_cast<void*>(valueAddrs[i]), bs)) {
            mPrintOp("Bio batch get fail, key:%s, crc check fail.\n", keys[i]);
            return;
        }
    }
    if (BioBatchGetFree(gTenantId, valueAddrs, batchNum) != 0) {
        mPrintOp("Bio batch get free shm fail.\n");
    }
    mPrintOp("Bio batch get success!\n");
}


void diagnose::BioSdkCommand::HandleBatchGetLocal(const std::vector<std::string> &cmds)
{
    uint32_t bs = (std::stoul(cmds[1]) * 1024);
    uint32_t batchNum = std::stoul(cmds[2]);
    char key[256];
    std::vector<std::string> prepareKeys;
    std::vector<std::shared_ptr<ObjLocation>> prepareLocations;
    uint32_t keyIndex = 0;
    char **values = reinterpret_cast<char**>(malloc(sizeof(char*) * batchNum));
    for (uint32_t idx = 0; idx < batchNum; idx++) {
        sprintf(key, "file_%u_%d", getpid(), keyIndex);

        std::shared_ptr<ObjLocation> location = std::make_shared<ObjLocation>();
        auto ret = BioCalcLocation(gTenantId, static_cast<uint64_t>(std::hash<std::string>{}(key)), location.get());
        if (ret != RET_CACHE_OK) {
            mPrintOp("Calculate location failed, result:%d.\n", ret);
            return;
        }
        char *value = new char[bs];
        ret = BioPut(gTenantId, key, value, bs, *location);
        if (ret != RET_CACHE_OK) {
            mPrintOp("Put key(%s) fail, result:%d\n", key, ret);
            return;
        }
        values[idx] = value;
        keyIndex++;
        prepareKeys.emplace_back(key);
        prepareLocations.emplace_back(location);
    }
    char **keys = reinterpret_cast<char**>(malloc(sizeof(char*) * batchNum));
    uint64_t *offsets = reinterpret_cast<uint64_t*>(malloc(sizeof(uint64_t) * batchNum));
    uint64_t *lengths = reinterpret_cast<uint64_t*>(malloc(sizeof(uint64_t) * batchNum));
    ObjLocation *locations = reinterpret_cast<ObjLocation*>(malloc(sizeof(ObjLocation) * batchNum));
    uintptr_t *valueAddrs = reinterpret_cast<uintptr_t*>(malloc(sizeof(uintptr_t) * batchNum));
    int32_t *results = reinterpret_cast<int32_t*>(malloc(sizeof(int32_t) * batchNum));
    KeyAddrInfo *infos = reinterpret_cast<KeyAddrInfo*>(malloc(sizeof(KeyAddrInfo) * batchNum));
    int32_t *positions = reinterpret_cast<int32_t*>(malloc(sizeof(int32_t) * batchNum));
    if (keys == nullptr || offsets == nullptr || lengths == nullptr || locations == nullptr ||
        valueAddrs == nullptr || results == nullptr) {
        mPrintOp("Malloc fail.");
        return;
    }

    for (uint32_t i = 0; i < batchNum; i++) {
        keys[i] = reinterpret_cast<char*>(malloc(sizeof(char) * 256));
        memcpy(reinterpret_cast<void*>(keys[i]), prepareKeys[i].c_str(), prepareKeys[i].size());
        reinterpret_cast<char*>(keys[i])[prepareKeys[i].size()] = '\0';
//        keys[i] = const_cast<char*>(prepareKeys[i].c_str());
        offsets[i] = 0;
        lengths[i] = bs;
        locations[i] = *(prepareLocations[i]);
        valueAddrs[i] = 0;
        results[i] = 0;
    }

    auto ret = BioBatchGetPositions(gTenantId, const_cast<const char**>(keys), batchNum, locations, positions);
    if (ret != RET_CACHE_OK) {
        mPrintOp("Get position fail, result:%d\n", ret);
        return;
    }

    std::vector<char*> localKeys;
    std::vector<ObjLocation> localLocations;
    std::vector<uint64_t> localLengths;
    std::vector<char*> localValues;
    std::vector<char*> remoteKeys;
    std::vector<ObjLocation> remoteLocations;
    std::vector<uint64_t> remoteLengths;
    std::vector<char*> remoteValues;
    for (uint32_t i = 0; i < batchNum; i++) {
        if (positions[i] == 0) {
            localKeys.emplace_back(keys[i]);
            localLocations.emplace_back(locations[i]);
            localLengths.emplace_back(bs);
            localValues.emplace_back(values[i]);
        } else {
            mPrintOp("key:%s is remote, location:%llu.\n", keys[i], locations[i].location[0]);
            remoteKeys.emplace_back(keys[i]);
            remoteLocations.emplace_back(locations[i]);
            remoteLengths.emplace_back(bs);
            remoteValues.emplace_back(values[i]);
        }
    }
    if (localKeys.size() != 0) {
        uintptr_t *localValueAddrs = reinterpret_cast<uintptr_t *>(malloc(sizeof(uintptr_t) * localKeys.size()));
        int32_t *localResults = reinterpret_cast<int32_t *>(malloc(sizeof(int32_t) * localKeys.size()));
        auto result = BioBatchGetLocal(gTenantId, const_cast<const char **>(localKeys.data()), localKeys.size(),
                                       localLengths.data(), localLocations.data(), localValueAddrs, localResults);

        if (result != 0) {
            mPrintOp("Bio batch get local fail, ret:%d.\n", result);
            return;
        }
        for (uint32_t i = 0; i < localKeys.size(); i++) {
            if (localResults[i] != 0) {
                mPrintOp("Bio batch get fail, key:%s, ret:%d.\n", localKeys[i], localResults[i]);
                return;
            }
            if (BioCrcUtil::Crc32(reinterpret_cast<void *>(localValues[i]), bs) !=
                BioCrcUtil::Crc32(reinterpret_cast<void *>(localValueAddrs[i]), bs)) {
                mPrintOp("Bio batch get fail, key:%s, crc check fail.\n", localKeys[i]);
                return;
            }
        }
        if (BioBatchGetFree(gTenantId, localValueAddrs, localKeys.size()) != 0) {
            mPrintOp("Bio batch get free shm fail.\n");
        }
    }
    if (remoteKeys.size() != 0) {
        uintptr_t *remoteValueAddrs = reinterpret_cast<uintptr_t *>(malloc(sizeof(uintptr_t) * remoteKeys.size()));
        int32_t *remoteResults = reinterpret_cast<int32_t *>(malloc(sizeof(int32_t) * remoteKeys.size()));
        auto remoteResult = BioBatchGetLocal(gTenantId, const_cast<const char **>(remoteKeys.data()), remoteKeys.size(),
                                             remoteLengths.data(), remoteLocations.data(), remoteValueAddrs,
                                             remoteResults);

        if (remoteResult != 0) {
            mPrintOp("Bio batch get remote fail, ret:%d.\n", result);
            return;
        }
        for (uint32_t i = 0; i < remoteKeys.size(); i++) {
            if (remoteResults[i] != 0) {
                mPrintOp("Bio batch get fail, key:%s, ret:%d.\n", remoteKeys[i], remoteResults[i]);
                return;
            }
            if (BioCrcUtil::Crc32(reinterpret_cast<void *>(remoteValues[i]), bs) !=
                BioCrcUtil::Crc32(reinterpret_cast<void *>(remoteValueAddrs[i]), bs)) {
                mPrintOp("Bio batch get fail, key:%s, crc check fail.\n", remoteKeys[i]);
                return;
            }
        }
        if (BioBatchGetFree(gTenantId, remoteValueAddrs, remoteKeys.size()) != 0) {
            mPrintOp("Bio batch get free shm fail.\n");
        }
    }
    mPrintOp("Bio batch get success!\n");
}

void diagnose::BioSdkCommand::HandlePerf(const std::vector<std::string> &cmds)
{
    for (int i = 2; i <= 4; i++) {
        if (!std::regex_match(cmds[i], GetPattern())) {
            mPrintOp("invalid input.\n");
            return;
        }
    }
    if (std::stoul(cmds[2]) == 0) {
        mPrintOp("Invalid param, bs:%s\n", cmds[2].c_str());
        return;
    }
    if (gTenantId == UINT64_MAX) {
        mPrintOp("Create and open a cache first!\n");
        return;
    }

    uint32_t bs = 0;
    uint32_t ioDepth = 0;
    uint64_t size = 0;
    auto rw = cmds[1].c_str();
    try {
        bs = (std::stoul(cmds[2]) * 1024);
        ioDepth = std::stoul(cmds[3]);
        size = (std::stoul(cmds[4]) * 1024 * 1024);
    } catch (std::exception e) {
        mPrintOp("invalid input.\n");
        return;
    }
    auto count = size / bs;

    perfTestRunner runner = nullptr;
    if (memcmp(rw, "read", sizeof("read")) == 0) {
        runner = PerfTestGetImpl;
    } else if (memcmp(rw, "write", sizeof("write")) == 0) {
        runner = PerfTestPutImpl;
    } else {
        mPrintOp("Invalid rw type:%s.\n", rw);
        return;
    }

    mPrintOp("Perf test start, rw:%s, bs:%u, ioDepth:%u, size:%u, count:%u.\n", rw, bs, ioDepth, size, count);
    auto *th = (pthread_t *)malloc(sizeof(pthread_t) * ioDepth);
    auto *param = (PerfTestParam *)malloc(sizeof(PerfTestParam) * ioDepth);
    if (th == nullptr || param == nullptr) {
        mPrintOp("Malloc memory failed.\n");
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
            mPrintOp("Perf test create pthread failed, ret:%d.\n", ret);
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
            mPrintOp("Perf test return failed, tid:%u, ret:%d.\n", k, param[k].result);
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
    mPrintOp("Perf Test Result: @ %s\n", asctime(timeinfo));
    mPrintOp("  IO depth                   : %lu\n", ioDepth);
    mPrintOp("  IO size                    : %lu\n", bs);
    mPrintOp("  total IO count             : %d\n", (int)totalCount);
    mPrintOp("  total spent                : %.2f ms\n", time_use / 1000U);
    mPrintOp("  throughput                 : %.4f MB/s\n", dataPerf * bwFactor);
    mPrintOp("  IOPS                       : %.2f /s\n", iops);
    mPrintOp("  latency                    : %.2f (us)\n", time_use / count);
    mPrintOp("Perf Test End.\n");

    free(param);
    free(th);
}

void diagnose::BioSdkCommand::BioSdkDebugHelp(char *command, int detail) noexcept
{
    mPrintOp("\tlist caches: sdk list\n");
    mPrintOp("\tcreate cache: sdk create [tenantId] [affinity] [strategy]\n");
    mPrintOp("\topen cache: sdk open [tenantId]\n");
    mPrintOp("\tdestroy cache: sdk destroy [tenantId]\n");
    mPrintOp("\tshow flow: sdk show flow [ptId]\n");
    mPrintOp("\tput value: sdk put [key] [filePath] [length] [sliceId]\n");
    mPrintOp("\tget value: sdk get [key] [offset] [length] [location] [filePath]\n");
    mPrintOp("\tstate object: sdk stat [key] [location]\n");
    mPrintOp("\tlist all object: sdk listall [prefix]\n");
    mPrintOp("\tload object: sdk load [key] [offset] [length] [location]\n");
    mPrintOp("\tdelete object: sdk delete [key] [location]\n");
    mPrintOp("\tshow view: sdk show [pt/node] [all/affinity]\n");
    mPrintOp("\ttrace: sdk trace [show/clear]\n");
    mPrintOp("\tCache hit: sdk cachehit\n");
    mPrintOp("\tAdd disk: sdk adddisk [diskPath]\n");
    mPrintOp("\tCache resource: sdk cacheresource\n");
    mPrintOp("\tperf test: sdk perf [rw] [bs(Kb)] [ioDepth] [size(Mb)]\n");
    mPrintOp("\tperf test: sdk batchget [bs(Kb)] [batchNUM]\n");
    mPrintOp("\tperf test: sdk batchgetpostion [bs(Kb)] [batchNUM]\n");
    mPrintOp("\tupdate prepare: sdk notifyupdate [tenantId]\n");
    mPrintOp("\tupdate check: sdk checkupdate [tenantId]\n");
    mPrintOp("\tupdate finish: sdk finishupdate [tenantId]\n");
    mPrintOp("\texit: exit console\n");
}

void diagnose::BioSdkCommand::BioSdkDebugProcess(int argc, char *argv[]) noexcept
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
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleCreate(cmds);
    } else if (cmdType == "open") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleOpen(cmds);
    } else if (cmdType == "destroy") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleDestroy(cmds);
    } else if (cmdType == "put") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 5) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandlePut(cmds);
    } else if (cmdType == "get") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 6) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleGet(cmds);
    } else if (cmdType == "stat") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleStat(cmds);
    }  else if (cmdType == "listall") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleList(cmds);
    } else if (cmdType == "load") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 5) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleLoad(cmds);
    } else if (cmdType == "delete") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleDelete(cmds);
    } else if (cmdType == "adddisk") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleAddDisk(cmds);
    } else if (cmdType == "show") {
        if (cmds.size() < 2) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleShow(cmds);
    } else if (cmdType == "trace") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleSdkTrace(cmds);
    } else if (cmdType == "perf") {
        if (cmds.size() != 5) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandlePerf(cmds);
    } else if (cmdType == "batchget") {
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleBatchGet(cmds);
    } else if (cmdType == "batchgetpostion") {
        if (cmds.size() != 3) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleBatchGetLocal(cmds);
    } else if (cmdType == "notifyupdate") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleNotifyUpdatePrepare(cmds);
    } else if (cmdType == "finishupdate") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleNotifyUpdateFinish(cmds);
    } else if (cmdType == "checkupdate") {
        if (cmds.size() != 2) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleCheckUpdateReady(cmds);
    } else if (cmdType == "cachehit") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 1) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleShowCacheHit(cmds);
    } else if (cmdType == "cacheresource") {
        if (gTenantId == UINT64_MAX) {
            mPrintOp("Create and open a cache first!\n");
            return;
        }
        if (cmds.size() != 1) {
            mPrintOp("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleShowCacheResource(cmds);
    } else if (cmdType == "exit") {
        return;
    } else {
        BioSdkDebugHelp(argv[0], 1);
    }
}
