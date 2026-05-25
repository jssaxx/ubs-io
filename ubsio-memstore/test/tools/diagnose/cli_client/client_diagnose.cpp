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

#include <chrono>
#include <iostream>
#include <memory>
#include <csignal>
#include <sys/resource.h>
#include <regex>
#include <condition_variable>
#include <semaphore.h>
#include <atomic>
#include "cli.h"
#include "tracer.h"
#include "mms_c.h"
#include "mms_client.h"
#include "mms_lock.h"
#include "client_diagnose.h"

#ifdef __cplusplus
extern "C" {
#endif

int ClientDiagnoseInit()
{
    return ock::mms::diagnose::MmsClientCommand::Initialize();
}

#ifdef __cplusplus
}
#endif

using namespace ock::mms;
std::regex pattern("[0-9]+");

static void MmsClientDebugProcess(int argc, char *argv[]) noexcept;
static void MmsClientDebugHelp(char *command, int detail) noexcept;

static bool mInited = false;
static std::atomic<bool> gNotifySwitch{false};

int diagnose::MmsClientCommand::Initialize() noexcept
{
    if (mInited) {
        return 0;
    }
    CliCommand command;
    strncpy(command.command, "mms", CLI_MAX_COMMAND_LEN);
    strncpy(command.description, "mms commands.", CLI_MAX_CMD_DESC_LEN);
    command.handler = MmsClientDebugProcess;
    command.help_handler = MmsClientDebugHelp;
    mInited = true;
    return cli_register_command(&command);
}

void diagnose::MmsClientCommand::Destroy() noexcept
{
    cli_unregister_command((char *)"sdk");
}

static constexpr uint32_t DIAG_KEY_BUFFER_LEN = 128;

struct PerfTestParam {
    bool done;
    uint64_t usrId;
    uint32_t id;
    uint32_t cpu;
    uint32_t batchNum;
    int32_t result;
    sem_t sem;
    uint32_t length;
    uint32_t count;
};

static void FillPerfKey(const PerfTestParam *param, const char *key, int32_t keyIndex)
{
    int ret = snprintf(const_cast<char *>(key), DIAG_KEY_BUFFER_LEN, "key_%lu_%u_%u_%d", param->usrId, param->id,
                       param->cpu, keyIndex);
    if (ret < 0) {
        cli_print_buffer("Format key failed.\n");
    }
}

typedef void *(*perfTestRunner)(void *param);

static uint32_t mUpdateLength = 0;
static bool mIsReady = false;
static constexpr uint16_t NOTIFY_SWITCH_OFF = 0;
static constexpr uint16_t NOTIFY_SWITCH_ON = 1;

static uint16_t GetNotifyFlag()
{
    return gNotifySwitch.load(std::memory_order_relaxed) ? NOTIFY_SWITCH_ON : NOTIFY_SWITCH_OFF;
}

static uint16_t GetKeyLen(const char *key)
{
    return static_cast<uint16_t>(strlen(key));
}

static PutItems MakePutItem(const char *key, const char *value, uint32_t valueLen, char **valueAddr, int32_t *result)
{
    return {key, value, valueLen, GetKeyLen(key), GetNotifyFlag(), valueAddr, result};
}

static GetItems MakeGetItem(const char *key, uint32_t offset, uint32_t length, char **value, uint32_t *realLength,
                            int32_t *result)
{
    return {key, GetKeyLen(key), offset, length, value, realLength, result};
}

static UpdateItems MakeUpdateItem(const char *key, const char *value, uint32_t offset, uint32_t valueLen,
                                  int32_t *result)
{
    return {key, value, GetKeyLen(key), valueLen, offset, result};
}

static DeleteItems MakeDeleteItem(const char *key, int32_t *result)
{
    return {key, GetKeyLen(key), GetNotifyFlag(), result};
}

static void RefreshKeyLen(PutItems *itemList, uint32_t itemNum)
{
    for (uint32_t i = 0; i < itemNum; i++) {
        itemList[i].keyLen = GetKeyLen(itemList[i].key);
    }
}

static void RefreshKeyLen(GetItems *itemList, uint32_t itemNum)
{
    for (uint32_t i = 0; i < itemNum; i++) {
        itemList[i].keyLen = GetKeyLen(itemList[i].key);
    }
}

static void RefreshKeyLen(UpdateItems *itemList, uint32_t itemNum)
{
    for (uint32_t i = 0; i < itemNum; i++) {
        itemList[i].keyLen = GetKeyLen(itemList[i].key);
    }
}

static void RefreshKeyLen(DeleteItems *itemList, uint32_t itemNum)
{
    for (uint32_t i = 0; i < itemNum; i++) {
        itemList[i].keyLen = GetKeyLen(itemList[i].key);
    }
}

static void HandlePut(std::vector<std::string> cmds)
{
    uint64_t userId = std::stoull(cmds[1]);
    auto key = cmds[2].c_str();
    auto filePath = cmds[3].c_str();
    uint32_t length = std::stoul(cmds[4]);

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        cli_print_buffer("fopen file failed, file: %s.\n", filePath);
        return;
    }
    char *value = new char[length];
    if (fread(value, sizeof(char), length, fp) != length) {
        cli_print_buffer("Read value from file failed, errno:%d.\n", errno);
        delete[] value;
        fclose(fp);
        return;
    }

    char *valueAddr = nullptr;
    int32_t result = RET_MMS_OK;
    PutItems item = MakePutItem(key, value, length, &valueAddr, &result);

    auto ret = MmsPut(&item, NO_1);
    if (ret != RET_MMS_OK) {
        cli_print_buffer("Failed to put a value, result:%d.\n", ret);
    } else {
        cli_print_buffer("Put value success, key:%s, length:%llu.\n", key, length);
    }
    delete[] value;
    fclose(fp);
}

static void HandleGet(std::vector<std::string> cmds)
{
    uint64_t userId = std::stoull(cmds[1]);
    auto key = cmds[2].c_str();
    uint32_t offset = std::stoul(cmds[3]);
    uint32_t length = std::stoul(cmds[4]);
    auto filePath = cmds[5].c_str();

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "w")) == nullptr) {
        cli_print_buffer("fopen file failed, file:%s.\n", filePath);
        return;
    }
    char *value = new char[length];
    char *valuePtr = value;
    uint32_t realLen = length;
    int32_t result = RET_MMS_OK;

    GetItems item = MakeGetItem(key, offset, length, &valuePtr, &realLen, &result);

    auto ret = MmsGet(&item, NO_1);
    if (ret != RET_MMS_OK) {
        cli_print_buffer("Failed to get a value, result:%d.\n", ret);
    } else {
        cli_print_buffer("Get value success, key:%s, offset:%llu, length:%llu, realLen:%llu.\n",
            key, offset, length, realLen);
        if (fwrite(value, sizeof(char), realLen, fp) != realLen) {
            cli_print_buffer("fwrite value to file failed, errno:%d.\n", errno);
        }
    }
    delete[] value;
    fclose(fp);
}

static void HandleCatchUp(std::vector<std::string> cmds)
{
    auto ret = MmsStartCatchUpTask();
    if (ret != RET_MMS_OK) {
        cli_print_buffer("start catch up task failed, result:%d.\n", ret);
    } else {
        cli_print_buffer("start catch up task success.\n");
    }
}

static void HandleUpdate(std::vector<std::string> cmds)
{
    uint64_t userId = std::stoull(cmds[1]);
    auto key = cmds[2].c_str();
    auto filePath = cmds[3].c_str();
    uint32_t offset = std::stoul(cmds[4]);
    uint32_t length = std::stoul(cmds[5]);

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        cli_print_buffer("fopen file failed, file: %s.\n", filePath);
        return;
    }
    char *value = new char[length];
    if (fread(value, sizeof(char), length, fp) != length) {
        cli_print_buffer("Read value from file failed, errno:%d.\n", errno);
        delete[] value;
        fclose(fp);
        return;
    }

    int32_t result = RET_MMS_OK;
    UpdateItems item = MakeUpdateItem(key, value, offset, length, &result);

    auto ret = MmsUpdate(&item, NO_1);
    if (ret != RET_MMS_OK) {
        cli_print_buffer("Failed to update a value, result:%d.\n", ret);
    } else {
        cli_print_buffer("Update value success, key:%s, length:%llu.\n", key, length);
    }
    delete[] value;
    fclose(fp);
}

static void HandleReplace(std::vector<std::string> cmds)
{
    uint64_t userId = std::stoull(cmds[1]);
    auto key = cmds[2].c_str();
    auto filePath = cmds[3].c_str();
    uint32_t offset = std::stoul(cmds[4]);
    uint32_t length = std::stoul(cmds[5]);

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        cli_print_buffer("fopen file failed, file: %s.\n", filePath);
        return;
    }
    char *value = new char[length];
    if (fread(value, sizeof(char), length, fp) != length) {
        cli_print_buffer("Read value from file failed, errno:%d.\n", errno);
        delete[] value;
        fclose(fp);
        return;
    }

    int32_t result = RET_MMS_OK;
    ReplaceItems item = MakeUpdateItem(key, value, offset, length, &result);

    auto ret = MmsReplace(&item, NO_1);
    if (ret != RET_MMS_OK) {
        cli_print_buffer("Failed to update a value, result:%d.\n", ret);
    } else {
        cli_print_buffer("Replace value success, key:%s, length:%llu.\n", key, length);
    }
    delete[] value;
    fclose(fp);
}

static void HandleDelete(std::vector<std::string> cmds)
{
    uint64_t userId = std::stoull(cmds[1]);
    auto key = cmds[2].c_str();

    int32_t result = RET_MMS_OK;
    DeleteItems item = MakeDeleteItem(key, &result);

    auto ret = MmsDelete(&item, NO_1);
    if (ret != RET_MMS_OK) {
        cli_print_buffer("Failed to delete, key%s, result:%d.\n", key, ret);
    } else {
        cli_print_buffer("Delete key success, key:%s.\n", key);
    }
}

static void HandleTrace(std::vector<std::string> cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "show") {
        auto info = ock::tracemark::TraceMark::GetTraceInfo();
        cli_print_buffer(info.c_str());
    } else if (viewType == "clear") {
        ock::tracemark::TraceMark::ClearTrace();
        cli_print_buffer("clearing statistics sdk records succeeded.\n");
    } else if (viewType == "open") {
        ock::tracemark::TraceMark::SetEnable(true);
        cli_print_buffer("open statistics sdk records succeeded.\n");
    } else if (viewType == "close") {
        ock::tracemark::TraceMark::SetEnable(false);
        cli_print_buffer("close statistics sdk records succeeded.\n");
    } else if (viewType == "open_tp99") {
        cli_print_buffer("open statistics sdk records succeeded.\n");
    } else if (viewType == "close_tp99") {
        cli_print_buffer("close statistics sdk records succeeded.\n");
    }
}

static void HandleSet(std::vector<std::string> cmds)
{
    mUpdateLength = std::stoul(cmds[1]);
    cli_print_buffer("reset update length: %u.\n", mUpdateLength);
}

static void HandleNotify(std::vector<std::string> cmds)
{
    std::string op = cmds[1];
    if (op == "open") {
        gNotifySwitch.store(true, std::memory_order_relaxed);
        cli_print_buffer("Open data change notify succeeded.\n");
        return;
    }
    if (op == "close") {
        gNotifySwitch.store(false, std::memory_order_relaxed);
        cli_print_buffer("Close data change notify succeeded.\n");
        return;
    }

    cli_print_buffer("Invalid notify operate type:%s.\n", op.c_str());
}

static void HandlePrefix(std::vector<std::string> cmds)
{
    auto prefix = cmds[1].c_str();
    ValueInfo *items = nullptr;
    uint64_t count = 0;
    int ret = MmsGetValuesByPrefix(prefix, &items, &count);
    if (ret != 0) {
        cli_print_buffer("get values by prefix failed, prefix:%s", prefix);
        return;
    }

    MmsFreeResources(&items, count);
    cli_print_buffer("success, prefix:%s, count:%d\n", prefix, count);
}

static void HandleRange(std::vector<std::string> cmds)
{
    std::string op = cmds[1];
    auto start = cmds[2].c_str();
    auto end = cmds[3].c_str();

    if (op == "delete") {
        int ret = MmsBatchDeleteByRange(start, end);
        if (ret == 0) {
            cli_print_buffer("batch delete by range success, start:%s, end%s\n", start, end);
        } else {
            cli_print_buffer("batch delete by range failed, start:%s, end%s\n", start, end);
        }

        return;
    }

    ValueInfo *items = nullptr;
    uint64_t count = 0;
    int ret = MmsGetValuesByRange(start, end, &items, &count);
    if (ret != 0) {
        cli_print_buffer("get values by range failed, start:%s, end%s\n", start, end);
        return;
    }

    MmsFreeResources(&items, count);
    cli_print_buffer("success, start:%s, end:%s, count:%d\n", start, end, count);
}

static void *PerfTestPutImpl(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    PerfTestParam *getParam = (PerfTestParam *)param;
    std::atomic<int32_t> keyIndex(0);

    uint32_t length = (mUpdateLength != 0) ? mUpdateLength : getParam->length;

    PutItems *itemList = new PutItems[getParam->batchNum];
    char **valueAddrs = new char *[getParam->batchNum]();
    int32_t *results = new int32_t[getParam->batchNum]();
    uint16_t isNotify = GetNotifyFlag();
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[DIAG_KEY_BUFFER_LEN];
        itemList[i].value = new char[length];
        memset(const_cast<char *>(itemList[i].value), 66, length);
        itemList[i].valueLen = length;
        itemList[i].isNotify = isNotify;
        itemList[i].valueAddr = &valueAddrs[i];
        itemList[i].result = &results[i];
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            FillPerfKey(getParam, itemList[i].key, keyIndex.load());
            keyIndex++;
        }
        RefreshKeyLen(itemList, getParam->batchNum);
        auto ret = MmsPut(itemList, getParam->batchNum);
        if (ret != RET_MMS_OK) {
            getParam->result = ret;
            break;
        }
    }

    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        delete[] itemList[i].key;
        delete[] itemList[i].value;
    }
    delete[] itemList;
    delete[] valueAddrs;
    delete[] results;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

static void *PerfTestGetImpl(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    PerfTestParam *getParam = (PerfTestParam *)param;
    std::atomic<int32_t> keyIndex(0);

    uint32_t length = (mUpdateLength != 0) ? mUpdateLength : getParam->length;

    GetItems *itemList = new GetItems[getParam->batchNum];
    char **values = new char *[getParam->batchNum]();
    uint32_t *realLengths = new uint32_t[getParam->batchNum]();
    int32_t *results = new int32_t[getParam->batchNum]();
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[DIAG_KEY_BUFFER_LEN];
        itemList[i].offset = 0;
        itemList[i].length = length;
        values[i] = new char[length];
        memset(values[i], 88, length);
        itemList[i].value = &values[i];
        itemList[i].realLength = &realLengths[i];
        itemList[i].result = &results[i];
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            FillPerfKey(getParam, itemList[i].key, keyIndex.load());
            keyIndex++;
        }
        RefreshKeyLen(itemList, getParam->batchNum);
        auto ret = MmsGet(itemList, getParam->batchNum);
        if (ret != RET_MMS_OK) {
            getParam->result = ret;
            break;
        }
    }

    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        delete[] itemList[i].key;
        delete[] values[i];
    }
    delete[] itemList;
    delete[] values;
    delete[] realLengths;
    delete[] results;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

static void *PerfTestUpdateImpl(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    PerfTestParam *getParam = (PerfTestParam *)param;
    std::atomic<int32_t> keyIndex(0);

    uint32_t length = (mUpdateLength != 0) ? mUpdateLength : getParam->length;

    UpdateItems *itemList = new UpdateItems[getParam->batchNum];
    int32_t *results = new int32_t[getParam->batchNum]();
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[DIAG_KEY_BUFFER_LEN];
        itemList[i].value = new char[length];
        memset(const_cast<char *>(itemList[i].value), 77, length);
        itemList[i].offset = 0;
        itemList[i].valueLen = length;
        itemList[i].result = &results[i];
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            FillPerfKey(getParam, itemList[i].key, keyIndex.load());
            keyIndex++;
        }
        RefreshKeyLen(itemList, getParam->batchNum);
        auto ret = MmsUpdate(itemList, getParam->batchNum);
        if (ret != RET_MMS_OK) {
            getParam->result = ret;
            break;
        }
    }

    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        delete[] itemList[i].key;
        delete[] itemList[i].value;
    }
    delete[] itemList;
    delete[] results;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

static void *PerfTestReplaceImpl(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    PerfTestParam *getParam = (PerfTestParam *)param;
    std::atomic<int32_t> keyIndex(0);

    uint32_t length = (mUpdateLength != 0) ? mUpdateLength : getParam->length;

    ReplaceItems *itemList = new ReplaceItems[getParam->batchNum];
    int32_t *results = new int32_t[getParam->batchNum]();
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[DIAG_KEY_BUFFER_LEN];
        itemList[i].value = new char[length];
        memset(const_cast<char *>(itemList[i].value), 77, length);
        itemList[i].offset = 0;
        itemList[i].valueLen = length;
        itemList[i].result = &results[i];
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            FillPerfKey(getParam, itemList[i].key, keyIndex.load());
            keyIndex++;
        }
        RefreshKeyLen(itemList, getParam->batchNum);
        auto ret = MmsReplace(itemList, getParam->batchNum);
        if (ret != RET_MMS_OK) {
            getParam->result = ret;
            break;
        }
    }

    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        delete[] itemList[i].key;
        delete[] itemList[i].value;
    }
    delete[] itemList;
    delete[] results;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

static void *PerfTestDeleteImpl(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    PerfTestParam *getParam = (PerfTestParam *)param;
    std::atomic<int32_t> keyIndex(0);

    DeleteItems *itemList = new DeleteItems[getParam->batchNum];
    int32_t *results = new int32_t[getParam->batchNum]();
    uint16_t isNotify = GetNotifyFlag();
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[DIAG_KEY_BUFFER_LEN];
        itemList[i].isNotify = isNotify;
        itemList[i].result = &results[i];
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            FillPerfKey(getParam, itemList[i].key, keyIndex.load());
            keyIndex++;
        }
        RefreshKeyLen(itemList, getParam->batchNum);
        auto ret = MmsDelete(itemList, getParam->batchNum);
        if (ret != RET_MMS_OK) {
            getParam->result = ret;
            break;
        }
    }

    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        delete[] itemList[i].key;
    }
    delete[] itemList;
    delete[] results;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

static void *PerfTestMixesImpl(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    PerfTestParam *getParam = (PerfTestParam *)param;
    std::atomic<int32_t> keyIndex(0);

    uint32_t length = (mUpdateLength != 0) ? mUpdateLength : getParam->length;

    PutItems *putList = new PutItems[getParam->batchNum];
    char **putValueAddrs = new char *[getParam->batchNum]();
    int32_t *putResults = new int32_t[getParam->batchNum]();
    uint16_t isNotify = GetNotifyFlag();
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        putList[i].key = new char[DIAG_KEY_BUFFER_LEN];
        putList[i].value = new char[length];
        memset(const_cast<char *>(putList[i].value), 66, length);
        putList[i].valueLen = length;
        putList[i].isNotify = isNotify;
        putList[i].valueAddr = &putValueAddrs[i];
        putList[i].result = &putResults[i];
    }

    GetItems *getList = new GetItems[getParam->batchNum];
    char **getValues = new char *[getParam->batchNum]();
    uint32_t *realLengths = new uint32_t[getParam->batchNum]();
    int32_t *getResults = new int32_t[getParam->batchNum]();
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        getList[i].key = new char[DIAG_KEY_BUFFER_LEN];
        getList[i].offset = 0;
        getList[i].length = length;
        getValues[i] = new char[length];
        memset(getValues[i], 88, length);
        getList[i].value = &getValues[i];
        getList[i].realLength = &realLengths[i];
        getList[i].result = &getResults[i];
    }

    for (uint32_t idx = 0; idx < 10; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            FillPerfKey(getParam, putList[i].key, keyIndex.load());
            keyIndex++;
        }
        RefreshKeyLen(putList, getParam->batchNum);
        auto ret = MmsPut(putList, getParam->batchNum);
        if (ret != RET_MMS_OK) {
            getParam->result = ret;
            break;
        }
    }

    for (uint32_t idx = 10; idx < getParam->count; idx++) {
        int32_t randnum = rand();
        if (randnum % 10 >= 7) {
            for (uint32_t i = 0; i < getParam->batchNum; i++) {
            FillPerfKey(getParam, putList[i].key, keyIndex.load());
                keyIndex++;
            }
            RefreshKeyLen(putList, getParam->batchNum);
            auto ret = MmsPut(putList, getParam->batchNum);
            if (ret != RET_MMS_OK) {
                getParam->result = ret;
                break;
            }
        } else {
            for (uint32_t i = 0; i < getParam->batchNum; i++) {
            FillPerfKey(getParam, getList[i].key, randnum % keyIndex.load());
            }
            RefreshKeyLen(getList, getParam->batchNum);
            auto ret = MmsGet(getList, getParam->batchNum);
            if (ret != RET_MMS_OK) {
                getParam->result = ret;
                break;
            }
        }
    }

    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        delete[] putList[i].key;
        delete[] putList[i].value;
        delete[] getList[i].key;
        delete[] getValues[i];
    }
    delete[] putList;
    delete[] putValueAddrs;
    delete[] putResults;
    delete[] getList;
    delete[] getValues;
    delete[] realLengths;
    delete[] getResults;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

static void HandlePerf(std::vector<std::string> cmds)
{
    auto rw = cmds[1].c_str();
    uint32_t bs = (std::stoul(cmds[2]) * 1024);
    uint32_t ioDepth = std::stoul(cmds[3]);
    uint32_t batchNum = std::stoul(cmds[4]);
    uint64_t size = (std::stoul(cmds[5]) * 1024 * 1024);
    uint64_t userId = std::stoul(cmds[6]);
    uint32_t numaNum = std::stol(cmds[7]);
    uint32_t cpuNum = std::stol(cmds[8]);
    uint32_t cpuStart = std::stol(cmds[9]);
    auto count = size / bs / batchNum / ioDepth;
    if (bs == 0 || batchNum == 0 || ioDepth == 0) {
        cli_print_buffer("Invalid para, bs:%u, batchNum:%u, ioDepth:%u", bs, batchNum, ioDepth);
        return;
    }

    perfTestRunner runner = nullptr;
    if (memcmp(rw, "put", sizeof("put")) == 0) {
        runner = PerfTestPutImpl;
    } else if (memcmp(rw, "get", sizeof("get")) == 0) {
        runner = PerfTestGetImpl;
    } else if (memcmp(rw, "update", sizeof("update")) == 0) {
        runner = PerfTestUpdateImpl;
    } else if (memcmp(rw, "replace", sizeof("replace")) == 0) {
        runner = PerfTestReplaceImpl;
    } else if (memcmp(rw, "delete", sizeof("delete")) == 0) {
        runner = PerfTestDeleteImpl;
    } else if (memcmp(rw, "mixes", sizeof("mixes")) == 0) {
        runner = PerfTestMixesImpl;
    } else {
        cli_print_buffer("Invalid operate type:%s.\n", rw);
        return;
    }

    cli_print_buffer("Perf test start, operate:%s, bs:%u, ioDepth:%u, batchNum:%u, size:%u, count:%u.\n", rw, bs, ioDepth,
                 batchNum, size, count);
    pthread_t *th = (pthread_t *)malloc(sizeof(pthread_t) * ioDepth);
    PerfTestParam *param = (PerfTestParam *)malloc(sizeof(PerfTestParam) * ioDepth);
    if (th == nullptr || param == nullptr) {
        cli_print_buffer("Malloc memory failed.\n");
        return;
    }
    uint32_t index = 0;
    for (uint32_t i = 0; i < ioDepth; i++) {
        for (uint32_t j = 0; j < numaNum; j++) {
            param[index].done = false;
            param[index].usrId = userId;
            param[index].id = index;
            param[index].cpu = cpuStart + i + j * cpuNum;
            param[index].batchNum = batchNum;
            param[index].result = RET_MMS_OK;
            sem_init(&param[index].sem, 0, 0);
            param[index].length = bs;
            param[index].count = count;
            index++;
            if (index == ioDepth) {
                break;
            }
        }
        if (index == ioDepth) {
            break;
        }
    }

    for (uint32_t i = 0; i < ioDepth; i++) {
        int ret = pthread_create(&th[i], nullptr, runner, &param[i]);
        if (ret != 0) {
            cli_print_buffer("Perf test create pthread failed, ret:%d.\n", ret);
            free(param);
            free(th);
            return;
        }
        std::string threadName = "perf-io-" + std::to_string(i);
        threadName += "-" + std::to_string(i);
        if (pthread_setname_np(th[i], threadName.c_str()) != 0) {
            cli_print_buffer("Failed to set name of BoostIO thread.\n");
        }

        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        CPU_SET(param[i].cpu, &cpuSet);
        if (pthread_setaffinity_np(th[i], sizeof(cpuSet), &cpuSet) != 0) {
            cli_print_buffer("Failed to bind thread %s, to cpu %u.\n", threadName.c_str(), param[i].cpu);
        } else {
            cli_print_buffer("Bind thread %s, to cpu %u.\n", threadName.c_str(), param[i].cpu);
        }
    }

    mIsReady = true;

    struct timeval startT;
    struct timeval stopT;
    gettimeofday(&startT, nullptr);

    for (uint32_t j = 0; j < ioDepth; j++) {
        while (!param[j].done) {
            sem_wait(&param[j].sem);
            j = 0;
        }
    }

    gettimeofday(&stopT, nullptr);
    for (uint32_t k = 0; k < ioDepth; k++) {
        if (param[k].result != 0) {
            cli_print_buffer("Perf test return failed, tid:%u, ret:%d.\n", k, param[k].result);
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
    cli_print_buffer("Perf Test Result: @ %s\n", asctime(timeinfo));
    cli_print_buffer("  IO depth                   : %lu\n", ioDepth);
    cli_print_buffer("  IO size                    : %lu\n", bs);
    cli_print_buffer("  total IO count             : %d\n", (int)totalCount);
    cli_print_buffer("  total spent                : %.2f ms\n", time_use / 1000U);
    cli_print_buffer("  throughput                 : %.4f MB/s\n", dataPerf * bwFactor);
    cli_print_buffer("  IOPS                       : %.2f /s\n", iops);
    cli_print_buffer("  latency                    : %.2f (us)\n", time_use / count);
    cli_print_buffer("Perf Test End.\n");

    mIsReady = false;

    free(param);
    free(th);
}

static void MmsClientDebugHelp(char *command, int detail) noexcept
{
    cli_print_buffer("\tput value: mms put [userId] [key] [filePath] [length]\n");
    cli_print_buffer("\tget value: mms get [userId] [key] [offset] [length] [filePath]\n");
    cli_print_buffer("\tupdate value: mms update [userId] [key] [filePath] [offset] [length]\n");
    cli_print_buffer("\treplace value: mms replace [userId] [key] [filePath] [offset] [length]\n");
    cli_print_buffer("\tdelete object: mms delete [userId] [key]\n");
    cli_print_buffer("\tprefix search: mms prefix [prefix]\n");
    cli_print_buffer("\trange search: mms range delete/search [start] [end]\n");
    cli_print_buffer("\tcatchup: mms catchup \n");
    cli_print_buffer("\ttrace: mms trace [open/close/show/clear]\n");
    cli_print_buffer("\tnotify: mms notify [open/close]\n");
    cli_print_buffer("\tperf: mms perf [put/get/update/replace/delete/mixes] [bs(Kb)] [ioDepth] [batchNum] [size(Mb)] "
                 "[userId] [numaNum] [cpuNum] [cpuStart]\n");
    cli_print_buffer("\texit: exit console\n");
}

static void MmsClientSetCpuAffinity(void)
{
    // 将线程绑定到核心0
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);

    // 设置线程亲和性
    int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        cli_print_buffer("Error setting thread affinity: %s.\n", strerror(ret));
        return;
    }
}

static void MmsClientDebugProcess(int argc, char *argv[]) noexcept
{
    if (argc <= 1) {
        MmsClientDebugHelp(argv[0], 1);
        return;
    }

    std::vector<std::string> cmds;
    for (int i = 1; i < argc; i++) {
        std::string str(argv[i]);
        cmds.emplace_back(str);
    }

    MmsClientSetCpuAffinity();

    std::string cmdType = cmds[0];
    if (cmdType == "put") {
        if (cmds.size() != 5) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandlePut(cmds);
    } else if (cmdType == "get") {
        if (cmds.size() != 6) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleGet(cmds);
    } else if (cmdType == "update") {
        if (cmds.size() != 6) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleUpdate(cmds);
    } else if (cmdType == "replace") {
        if (cmds.size() != 6) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleReplace(cmds);
    } else if (cmdType == "delete") {
        if (cmds.size() != 3) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleDelete(cmds);
    } else if (cmdType == "catchup") {
        HandleCatchUp(cmds);
    }  else if (cmdType == "trace") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleTrace(cmds);
    } else if (cmdType == "perf") {
        if (cmds.size() != 10) {
            cli_print_buffer("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandlePerf(cmds);
    } else if (cmdType == "set") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleSet(cmds);
    } else if (cmdType == "notify") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleNotify(cmds);
    } else if (cmdType == "prefix") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandlePrefix(cmds);
    } else if (cmdType == "range") {
        if (cmds.size() != 4) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleRange(cmds);
    } else if (cmdType == "exit") {
        return;
    } else {
        MmsClientDebugHelp(argv[0], 1);
    }
}
