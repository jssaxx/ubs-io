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
#include <memory>
#include <limits>
#include <new>
#include <semaphore.h>
#include <atomic>
#include "cli.h"
#include "tracer.h"
#include "mms_c.h"
#include "mms_client.h"
#include "mms_message.h"
#include "mms_lock.h"
#include "client_diagnose.h"

using namespace ock::mms;

static void MmsClientDebugProcess(int argc, char *argv[]) noexcept;
static void MmsClientDebugHelp(char *, int) noexcept;

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
    int ret = cli_register_command(&command);
    if (ret == 0) {
        mInited = true;
    }
    return ret;
}

void diagnose::MmsClientCommand::Destroy() noexcept
{
    if (!mInited) {
        return;
    }
    cli_unregister_command((char *)"mms");
    mInited = false;
}

static constexpr uint32_t DIAG_KEY_BUFFER_LEN = MAX_KEY_SIZE;

static bool CliKeyValid(const char *key)
{
    size_t keyLen = key == nullptr ? 0 : strnlen(key, MAX_KEY_SIZE);
    if (keyLen == 0 || keyLen >= MAX_KEY_SIZE) {
        cli_print_buffer("Invalid key, valid length range:[1,%u].\n", MAX_KEY_LENGTH);
        return false;
    }
    return true;
}

static bool ParseUint32Arg(const std::string &text, const char *name, uint32_t &value)
{
    if (text.empty()) {
        cli_print_buffer("Invalid %s:%s, expected uint32.\n", name, text.c_str());
        return false;
    }

    uint64_t parsed = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            cli_print_buffer("Invalid %s:%s, expected uint32.\n", name, text.c_str());
            return false;
        }
        parsed = parsed * NO_10 + static_cast<uint32_t>(ch - '0');
        if (parsed > std::numeric_limits<uint32_t>::max()) {
            cli_print_buffer("Invalid %s:%s, expected uint32.\n", name, text.c_str());
            return false;
        }
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

static bool ParseIoRange(const std::string &offsetText, const std::string &lengthText, uint32_t &offset,
                         uint32_t &length)
{
    if (!ParseUint32Arg(offsetText, "offset", offset) || !ParseUint32Arg(lengthText, "length", length)) {
        return false;
    }
    if (length == 0 || length > MMS_MAX_VALUE_SIZE || offset > MMS_MAX_VALUE_SIZE - length) {
        cli_print_buffer("Invalid IO range, offset:%u, length:%u, max:%u.\n", offset, length, MMS_MAX_VALUE_SIZE);
        return false;
    }
    return true;
}

struct PerfTestParam {
    bool done;
    uint32_t id;
    uint32_t cpu;
    uint32_t batchNum;
    int32_t result;
    sem_t sem;
    uint32_t length;
    uint32_t count;
    bool remoteOnly;
    bool consistency;
    uint32_t *keyIndexes;
};

static void FormatPerfKey(const PerfTestParam *param, const char *key, uint32_t keyIndex)
{
    const char *prefix = param->consistency ? "check_key" : "key";
    int ret = snprintf(const_cast<char *>(key), DIAG_KEY_BUFFER_LEN, "%s_%u_%u_%u", prefix, param->id, param->cpu,
                       keyIndex);
    if (ret < 0) {
        cli_print_buffer("Format key failed.\n");
    }
}

static void FillPerfKey(const PerfTestParam *param, const char *key, uint32_t keyIndex)
{
    uint32_t actualIndex = keyIndex;
    if (param->remoteOnly && param->keyIndexes != nullptr) {
        actualIndex = param->keyIndexes[keyIndex];
    }
    FormatPerfKey(param, key, actualIndex);
}

static BResult PrepareRemotePerfKeys(PerfTestParam &param)
{
    if (!param.remoteOnly) {
        return MMS_OK;
    }

    uint64_t keyNum = static_cast<uint64_t>(param.count) * param.batchNum;
    if (keyNum == 0 || keyNum > std::numeric_limits<uint32_t>::max()) {
        return MMS_INVALID_PARAM;
    }

    param.keyIndexes = new (std::nothrow) uint32_t[keyNum];
    if (param.keyIndexes == nullptr) {
        return MMS_ALLOC_FAIL;
    }

    constexpr uint32_t remoteKeySearchLimit = 65536;
    uint64_t candidate = 0;
    char key[DIAG_KEY_BUFFER_LEN] = {};
    for (uint64_t index = 0; index < keyNum; ++index) {
        bool found = false;
        for (uint32_t attempt = 0; attempt < remoteKeySearchLimit; ++attempt) {
            if (UNLIKELY(candidate > std::numeric_limits<uint32_t>::max())) {
                return MMS_INVALID_PARAM;
            }
            FormatPerfKey(&param, key, static_cast<uint32_t>(candidate));
            bool remoteKey = false;
            auto ret = MmsClient::Instance()->IsRemoteKey(key, remoteKey);
            if (UNLIKELY(ret != MMS_OK)) {
                return ret;
            }
            if (remoteKey) {
                param.keyIndexes[index] = static_cast<uint32_t>(candidate++);
                found = true;
                break;
            }
            candidate++;
        }
        if (UNLIKELY(!found)) {
            return MMS_NOT_EXISTS;
        }
    }
    return MMS_OK;
}

static void ReleaseRemotePerfKeys(PerfTestParam *params, uint32_t ioDepth)
{
    for (uint32_t index = 0; index < ioDepth; ++index) {
        delete[] params[index].keyIndexes;
        params[index].keyIndexes = nullptr;
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

static void HandlePut(const std::vector<std::string> &cmds)
{
    auto key = cmds[1].c_str();
    if (!CliKeyValid(key)) {
        return;
    }
    auto filePath = cmds[2].c_str();
    uint32_t length = 0;
    if (!ParseUint32Arg(cmds[3], "length", length) || length == 0 || length > MMS_MAX_VALUE_SIZE) {
        cli_print_buffer("Invalid value length:%s, valid range:[1,%u].\n", cmds[3].c_str(), MMS_MAX_VALUE_SIZE);
        return;
    }

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        cli_print_buffer("fopen file failed, file: %s.\n", filePath);
        return;
    }
    char *value = new (std::nothrow) char[length];
    if (value == nullptr) {
        cli_print_buffer("Allocate value buffer failed, length:%u.\n", length);
        fclose(fp);
        return;
    }
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
    if (ret != RET_MMS_OK || result != RET_MMS_OK) {
        cli_print_buffer("Failed to put a value, result:%d, item result:%d.\n", ret, result);
    } else {
        cli_print_buffer("Put value success, key:%s, length:%u.\n", key, length);
    }
    delete[] value;
    fclose(fp);
}

static void HandleGet(const std::vector<std::string> &cmds)
{
    auto key = cmds[1].c_str();
    if (!CliKeyValid(key)) {
        return;
    }
    uint32_t offset = 0;
    uint32_t length = 0;
    if (!ParseIoRange(cmds[2], cmds[3], offset, length)) {
        return;
    }
    auto filePath = cmds[4].c_str();

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "w")) == nullptr) {
        cli_print_buffer("fopen file failed, file:%s.\n", filePath);
        return;
    }
    char *value = new (std::nothrow) char[length];
    if (value == nullptr) {
        cli_print_buffer("Allocate value buffer failed, length:%u.\n", length);
        fclose(fp);
        return;
    }
    char *valuePtr = value;
    uint32_t realLen = length;
    int32_t result = RET_MMS_OK;

    GetItems item = MakeGetItem(key, offset, length, &valuePtr, &realLen, &result);

    auto ret = MmsGet(&item, NO_1);
    if (ret != RET_MMS_OK || result != RET_MMS_OK) {
        cli_print_buffer("Failed to get a value, result:%d, item result:%d.\n", ret, result);
    } else {
        cli_print_buffer("Get value success, key:%s, offset:%u, length:%u, realLen:%u.\n",
            key, offset, length, realLen);
        if (fwrite(value, sizeof(char), realLen, fp) != realLen) {
            cli_print_buffer("fwrite value to file failed, errno:%d.\n", errno);
        }
    }
    delete[] value;
    fclose(fp);
}

static void HandleCatchUp()
{
    auto ret = MmsStartCatchUpTask();
    if (ret != RET_MMS_OK) {
        cli_print_buffer("start catch up task failed, result:%d.\n", ret);
    } else {
        cli_print_buffer("start catch up task success.\n");
    }
}

static void HandleUpdate(const std::vector<std::string> &cmds)
{
    auto key = cmds[1].c_str();
    if (!CliKeyValid(key)) {
        return;
    }
    auto filePath = cmds[2].c_str();
    uint32_t offset = 0;
    uint32_t length = 0;
    if (!ParseIoRange(cmds[3], cmds[4], offset, length)) {
        return;
    }

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        cli_print_buffer("fopen file failed, file: %s.\n", filePath);
        return;
    }
    char *value = new (std::nothrow) char[length];
    if (value == nullptr) {
        cli_print_buffer("Allocate value buffer failed, length:%u.\n", length);
        fclose(fp);
        return;
    }
    if (fread(value, sizeof(char), length, fp) != length) {
        cli_print_buffer("Read value from file failed, errno:%d.\n", errno);
        delete[] value;
        fclose(fp);
        return;
    }

    int32_t result = RET_MMS_OK;
    UpdateItems item = MakeUpdateItem(key, value, offset, length, &result);

    auto ret = MmsUpdate(&item, NO_1);
    if (ret != RET_MMS_OK || result != RET_MMS_OK) {
        cli_print_buffer("Failed to update a value, result:%d, item result:%d.\n", ret, result);
    } else {
        cli_print_buffer("Update value success, key:%s, length:%u.\n", key, length);
    }
    delete[] value;
    fclose(fp);
}

static void HandleReplace(const std::vector<std::string> &cmds)
{
    auto key = cmds[1].c_str();
    if (!CliKeyValid(key)) {
        return;
    }
    auto filePath = cmds[2].c_str();
    uint32_t offset = 0;
    uint32_t length = 0;
    if (!ParseIoRange(cmds[3], cmds[4], offset, length)) {
        return;
    }

    FILE *fp = nullptr;
    if ((fp = fopen(filePath, "r")) == nullptr) {
        cli_print_buffer("fopen file failed, file: %s.\n", filePath);
        return;
    }
    char *value = new (std::nothrow) char[length];
    if (value == nullptr) {
        cli_print_buffer("Allocate value buffer failed, length:%u.\n", length);
        fclose(fp);
        return;
    }
    if (fread(value, sizeof(char), length, fp) != length) {
        cli_print_buffer("Read value from file failed, errno:%d.\n", errno);
        delete[] value;
        fclose(fp);
        return;
    }

    int32_t result = RET_MMS_OK;
    ReplaceItems item = MakeUpdateItem(key, value, offset, length, &result);

    auto ret = MmsReplace(&item, NO_1);
    if (ret != RET_MMS_OK || result != RET_MMS_OK) {
        cli_print_buffer("Failed to replace a value, result:%d, item result:%d.\n", ret, result);
    } else {
        cli_print_buffer("Replace value success, key:%s, length:%u.\n", key, length);
    }
    delete[] value;
    fclose(fp);
}

static void HandleDelete(const std::vector<std::string> &cmds)
{
    auto key = cmds[1].c_str();
    if (!CliKeyValid(key)) {
        return;
    }

    int32_t result = RET_MMS_OK;
    DeleteItems item = MakeDeleteItem(key, &result);

    auto ret = MmsDelete(&item, NO_1);
    if (ret != RET_MMS_OK || result != RET_MMS_OK) {
        cli_print_buffer("Failed to delete, key:%s, result:%d, item result:%d.\n", key, ret, result);
    } else {
        cli_print_buffer("Delete key success, key:%s.\n", key);
    }
}

static void HandleTrace(const std::vector<std::string> &cmds)
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

static void HandleSet(const std::vector<std::string> &cmds)
{
    uint32_t length = 0;
    if (!ParseUint32Arg(cmds[1], "length", length) || length > MMS_MAX_VALUE_SIZE) {
        cli_print_buffer("Invalid update length:%s, valid range:[0,%u].\n", cmds[1].c_str(), MMS_MAX_VALUE_SIZE);
        return;
    }
    mUpdateLength = length;
    cli_print_buffer("reset update length: %u.\n", mUpdateLength);
}

static void HandleNotify(const std::vector<std::string> &cmds)
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

static void *PerfTestPutImpl(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    PerfTestParam *getParam = (PerfTestParam *)param;
    uint32_t keyIndex = 0;

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
            FillPerfKey(getParam, itemList[i].key, keyIndex);
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
    uint32_t keyIndex = 0;

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
            FillPerfKey(getParam, itemList[i].key, keyIndex);
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

static void FillConsistencyValue(char *value, uint32_t length, uint32_t threadId, uint32_t keyIndex)
{
    uint8_t seed = static_cast<uint8_t>((threadId * 131U + keyIndex * 17U) & UINT8_MAX);
    for (uint32_t index = 0; index < length; ++index) {
        value[index] = static_cast<char>(seed + static_cast<uint8_t>(index * 31U));
    }
}

static bool CheckItemResults(const int32_t *results, uint32_t itemNum, int32_t expected, uint32_t &failedIndex)
{
    for (uint32_t index = 0; index < itemNum; ++index) {
        if (results[index] != expected) {
            failedIndex = index;
            return false;
        }
    }
    return true;
}

static void *PerfTestConsistencyImpl(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    auto *testParam = static_cast<PerfTestParam *>(param);
    uint32_t batchNum = testParam->batchNum;
    std::vector<PutItems> putItems(batchNum);
    std::vector<GetItems> getItems(batchNum);
    std::vector<DeleteItems> deleteItems(batchNum);
    std::vector<std::unique_ptr<char[]>> keys(batchNum);
    std::vector<std::unique_ptr<char[]>> putValues(batchNum);
    std::vector<std::unique_ptr<char[]>> getValues(batchNum);
    std::vector<char *> putValueAddrs(batchNum, nullptr);
    std::vector<char *> getValueAddrs(batchNum, nullptr);
    std::vector<uint32_t> realLengths(batchNum, 0);
    std::vector<int32_t> putResults(batchNum, RET_MMS_BUTT);
    std::vector<int32_t> getResults(batchNum, RET_MMS_BUTT);
    std::vector<int32_t> deleteResults(batchNum, RET_MMS_BUTT);

    for (uint32_t index = 0; index < batchNum; ++index) {
        keys[index].reset(new char[DIAG_KEY_BUFFER_LEN]);
        putValues[index].reset(new char[testParam->length]);
        getValues[index].reset(new char[testParam->length]);
        getValueAddrs[index] = getValues[index].get();
        putItems[index] = MakePutItem(keys[index].get(), putValues[index].get(), testParam->length,
                                       &putValueAddrs[index], &putResults[index]);
        getItems[index] = MakeGetItem(keys[index].get(), 0, testParam->length, &getValueAddrs[index],
                                     &realLengths[index], &getResults[index]);
        deleteItems[index] = MakeDeleteItem(keys[index].get(), &deleteResults[index]);
    }

    uint32_t keyIndex = 0;
    for (uint32_t round = 0; round < testParam->count; ++round) {
        for (uint32_t index = 0; index < batchNum; ++index) {
            FillPerfKey(testParam, keys[index].get(), keyIndex);
            FillConsistencyValue(putValues[index].get(), testParam->length, testParam->id, keyIndex);
            putItems[index].keyLen = GetKeyLen(keys[index].get());
            getItems[index].keyLen = putItems[index].keyLen;
            deleteItems[index].keyLen = putItems[index].keyLen;
            putResults[index] = RET_MMS_BUTT;
            getResults[index] = RET_MMS_BUTT;
            deleteResults[index] = RET_MMS_BUTT;
            realLengths[index] = 0;
            ++keyIndex;
        }

        auto ret = MmsPut(putItems.data(), batchNum);
        uint32_t failedIndex = 0;
        if (ret != RET_MMS_OK || !CheckItemResults(putResults.data(), batchNum, RET_MMS_OK, failedIndex)) {
            cli_print_buffer("Consistency put failed, tid:%u, round:%u, item:%u, ret:%d, itemRet:%d.\n",
                             testParam->id, round, failedIndex, ret, putResults[failedIndex]);
            testParam->result = (ret == RET_MMS_OK) ? RET_MMS_ERROR : ret;
            break;
        }

        ret = MmsGet(getItems.data(), batchNum);
        if (ret != RET_MMS_OK || !CheckItemResults(getResults.data(), batchNum, RET_MMS_OK, failedIndex)) {
            cli_print_buffer("Consistency get failed, tid:%u, round:%u, item:%u, ret:%d, itemRet:%d.\n",
                             testParam->id, round, failedIndex, ret, getResults[failedIndex]);
            testParam->result = (ret == RET_MMS_OK) ? RET_MMS_ERROR : ret;
            break;
        }
        for (uint32_t index = 0; index < batchNum; ++index) {
            if (realLengths[index] != testParam->length ||
                memcmp(putValues[index].get(), getValues[index].get(), testParam->length) != 0) {
                cli_print_buffer("Consistency data mismatch, tid:%u, round:%u, item:%u, expectedLen:%u, realLen:%u.\n",
                                 testParam->id, round, index, testParam->length, realLengths[index]);
                testParam->result = RET_MMS_ERROR;
                break;
            }
        }
        if (testParam->result != RET_MMS_OK) {
            break;
        }

        ret = MmsDelete(deleteItems.data(), batchNum);
        if (ret != RET_MMS_OK || !CheckItemResults(deleteResults.data(), batchNum, RET_MMS_OK, failedIndex)) {
            cli_print_buffer("Consistency delete failed, tid:%u, round:%u, item:%u, ret:%d, itemRet:%d.\n",
                             testParam->id, round, failedIndex, ret, deleteResults[failedIndex]);
            testParam->result = (ret == RET_MMS_OK) ? RET_MMS_ERROR : ret;
            break;
        }

        std::fill(getResults.begin(), getResults.end(), RET_MMS_BUTT);
        std::fill(realLengths.begin(), realLengths.end(), 0);
        ret = MmsGet(getItems.data(), batchNum);
        if ((ret != RET_MMS_OK && ret != RET_MMS_NOT_FOUND) ||
            !CheckItemResults(getResults.data(), batchNum, RET_MMS_NOT_FOUND, failedIndex)) {
            cli_print_buffer("Consistency post-delete get failed, tid:%u, round:%u, item:%u, ret:%d, itemRet:%d.\n",
                             testParam->id, round, failedIndex, ret, getResults[failedIndex]);
            testParam->result = (ret == RET_MMS_OK || ret == RET_MMS_NOT_FOUND) ? RET_MMS_ERROR : ret;
            break;
        }
    }

    testParam->done = true;
    sem_post(&testParam->sem);
    return nullptr;
}

static void *PerfTestUpdateImpl(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    PerfTestParam *getParam = (PerfTestParam *)param;
    uint32_t keyIndex = 0;

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
            FillPerfKey(getParam, itemList[i].key, keyIndex);
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
    uint32_t keyIndex = 0;

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
            FillPerfKey(getParam, itemList[i].key, keyIndex);
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
    uint32_t keyIndex = 0;

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
            FillPerfKey(getParam, itemList[i].key, keyIndex);
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
    uint32_t keyIndex = 0;

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
            FillPerfKey(getParam, putList[i].key, keyIndex);
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
            FillPerfKey(getParam, putList[i].key, keyIndex);
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
            FillPerfKey(getParam, getList[i].key, randnum % keyIndex);
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

static void HandlePerf(const std::vector<std::string> &cmds)
{
    bool remoteOnly = cmds.size() == 10;
    auto rw = cmds[1].c_str();
    uint32_t bsKb = 0;
    uint32_t ioDepth = 0;
    uint32_t batchNum = 0;
    uint32_t sizeMb = 0;
    uint32_t numaNum = 0;
    uint32_t cpuNum = 0;
    uint32_t cpuStart = 0;
    if (!ParseUint32Arg(cmds[2], "bs", bsKb) || !ParseUint32Arg(cmds[3], "ioDepth", ioDepth) ||
        !ParseUint32Arg(cmds[4], "batchNum", batchNum) || !ParseUint32Arg(cmds[5], "size", sizeMb) ||
        !ParseUint32Arg(cmds[6], "numaNum", numaNum) || !ParseUint32Arg(cmds[7], "cpuNum", cpuNum) ||
        !ParseUint32Arg(cmds[8], "cpuStart", cpuStart)) {
        return;
    }
    if (bsKb == 0 || bsKb > MMS_MAX_VALUE_SIZE / KB_UNIT || batchNum == 0 || ioDepth == 0 || sizeMb == 0 ||
        numaNum == 0 || numaNum > MAX_NUMAS_NUM || cpuNum == 0 || cpuStart >= CPU_SETSIZE) {
        cli_print_buffer("Invalid perf parameters, bs(KiB):%u, ioDepth:%u, batchNum:%u, size(MiB):%u, "
                         "numaNum:%u, cpuNum:%u, cpuStart:%u.\n", bsKb, ioDepth, batchNum, sizeMb, numaNum,
                         cpuNum, cpuStart);
        return;
    }
    uint32_t bs = bsKb * KB_UNIT;
    uint64_t size = static_cast<uint64_t>(sizeMb) * IO_SIZE_1M;
    auto count = size / bs / batchNum / ioDepth;
    if (count == 0 || count > std::numeric_limits<uint32_t>::max()) {
        cli_print_buffer("Invalid test round count:%llu, bs:%u, batchNum:%u, ioDepth:%u.\n",
                         static_cast<unsigned long long>(count), bs, batchNum, ioDepth);
        return;
    }

    perfTestRunner runner = nullptr;
    bool consistency = false;
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
    } else if (memcmp(rw, "check", sizeof("check")) == 0) {
        runner = PerfTestConsistencyImpl;
        consistency = true;
    } else {
        cli_print_buffer("Invalid operate type:%s.\n", rw);
        return;
    }

    cli_print_buffer("Perf test start, operate:%s, bs:%u, ioDepth:%u, batchNum:%u, size:%llu, count:%llu, "
                     "route:%s.\n", rw, bs, ioDepth, batchNum, static_cast<unsigned long long>(size),
                     static_cast<unsigned long long>(count), remoteOnly ? "remote" : "default");
    pthread_t *th = (pthread_t *)malloc(sizeof(pthread_t) * ioDepth);
    PerfTestParam *param = (PerfTestParam *)malloc(sizeof(PerfTestParam) * ioDepth);
    if (th == nullptr || param == nullptr) {
        cli_print_buffer("Malloc memory failed.\n");
        free(param);
        free(th);
        return;
    }
    uint32_t index = 0;
    for (uint32_t i = 0; i < ioDepth; i++) {
        for (uint32_t j = 0; j < numaNum; j++) {
            uint64_t cpu = static_cast<uint64_t>(cpuStart) + i + static_cast<uint64_t>(j) * cpuNum;
            if (cpu >= CPU_SETSIZE) {
                cli_print_buffer("Invalid CPU index:%llu, CPU_SETSIZE:%u.\n",
                                 static_cast<unsigned long long>(cpu), CPU_SETSIZE);
                free(param);
                free(th);
                return;
            }
            param[index].done = false;
            param[index].id = index;
            param[index].cpu = static_cast<uint32_t>(cpu);
            param[index].batchNum = batchNum;
            param[index].result = RET_MMS_OK;
            sem_init(&param[index].sem, 0, 0);
            param[index].length = bs;
            param[index].count = static_cast<uint32_t>(count);
            param[index].remoteOnly = remoteOnly;
            param[index].consistency = consistency;
            param[index].keyIndexes = nullptr;
            index++;
            if (index == ioDepth) {
                break;
            }
        }
        if (index == ioDepth) {
            break;
        }
    }

    if (remoteOnly) {
        for (uint32_t i = 0; i < ioDepth; ++i) {
            auto ret = PrepareRemotePerfKeys(param[i]);
            if (UNLIKELY(ret != MMS_OK)) {
                cli_print_buffer("Prepare remote keys failed, tid:%u, ret:%d.\n", i, ret);
                ReleaseRemotePerfKeys(param, ioDepth);
                free(param);
                free(th);
                return;
            }
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
            ReleaseRemotePerfKeys(param, ioDepth);
            free(param);
            free(th);
            return;
        }
    }

    float cost_sec = stopT.tv_sec - startT.tv_sec;
    float cost_usec = stopT.tv_usec - startT.tv_usec;
    float time_use = cost_sec * 1000000U + cost_usec;
    uint64_t totalCount = static_cast<uint64_t>(count) * ioDepth * batchNum;
    if (consistency) {
        cli_print_buffer("Consistency Test Result: PASS, valueSize:%u, ioDepth:%u, batchNum:%u, rounds:%llu, "
                         "items:%llu, route:%s, spent:%.2f ms.\n", bs, ioDepth, batchNum,
                         static_cast<unsigned long long>(count),
                         static_cast<unsigned long long>(totalCount), remoteOnly ? "remote" : "default",
                         time_use / 1000U);
        mIsReady = false;
        ReleaseRemotePerfKeys(param, ioDepth);
        free(param);
        free(th);
        return;
    }
    uint64_t totalSize = static_cast<uint64_t>(count) * bs * batchNum * ioDepth;
    double dataPerf = static_cast<double>(totalSize) / 1048576U * 1000000U / time_use;
    double iops = static_cast<double>(totalCount) * 1000000U / time_use;
    time_t rawtime;
    struct tm timebuf{};
    rawtime = time(nullptr);
    localtime_r(&rawtime, &timebuf);
    cli_print_buffer("Perf Test Result: @ %s\n", asctime(&timebuf));
    cli_print_buffer("  IO depth                   : %u\n", ioDepth);
    cli_print_buffer("  IO size                    : %u\n", bs);
    cli_print_buffer("  total IO count             : %llu\n", static_cast<unsigned long long>(totalCount));
    cli_print_buffer("  total spent                : %.2f ms\n", time_use / 1000U);
    cli_print_buffer("  throughput                 : %.4f MB/s\n", dataPerf);
    cli_print_buffer("  IOPS                       : %.2f /s\n", iops);
    cli_print_buffer("  batch latency              : %.2f (us)\n", time_use / count);
    cli_print_buffer("  latency                    : %.2f (us)\n", time_use / count / batchNum);
    cli_print_buffer("Perf Test End.\n");

    mIsReady = false;

    ReleaseRemotePerfKeys(param, ioDepth);
    free(param);
    free(th);
}

static void MmsClientDebugHelp(char *, int) noexcept
{
    cli_print_buffer("\tput value: mms put [key] [filePath] [length]\n");
    cli_print_buffer("\tget value: mms get [key] [offset] [length] [filePath]\n");
    cli_print_buffer("\tupdate value: mms update [key] [filePath] [offset] [length]\n");
    cli_print_buffer("\treplace value: mms replace [key] [filePath] [offset] [length]\n");
    cli_print_buffer("\tdelete object: mms delete [key]\n");
    cli_print_buffer("\tcatchup: mms catchup \n");
    cli_print_buffer("\ttrace: mms trace [open/close/show/clear]\n");
    cli_print_buffer("\tnotify: mms notify [open/close]\n");
    cli_print_buffer("\tperf: mms perf [put/get/update/replace/delete/mixes] [bs(Kb)] [ioDepth] [batchNum] [size(Mb)] "
                 "[numaNum] [cpuNum] [cpuStart]\n");
    cli_print_buffer("\tremote perf: mms perf [put/get/update/replace/delete/mixes] [bs(Kb)] [ioDepth] [batchNum] "
                 "[size(Mb)] [numaNum] [cpuNum] [cpuStart] remote\n");
    cli_print_buffer("\tconsistency: mms perfcheck [bs(Kb)] [ioDepth] [batchNum] [size(Mb)] "
                 "[numaNum] [cpuNum] [cpuStart] [remote]\n");
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
        if (cmds.size() != 4) {
            cli_print_buffer("Input parameters failed!, num:%zu.\n", cmds.size());
            return;
        }
        HandlePut(cmds);
    } else if (cmdType == "get") {
        if (cmds.size() != 5) {
            cli_print_buffer("Input parameters failed!, num:%zu.\n", cmds.size());
            return;
        }
        HandleGet(cmds);
    } else if (cmdType == "update") {
        if (cmds.size() != 5) {
            cli_print_buffer("Input parameters failed!, num:%zu.\n", cmds.size());
            return;
        }
        HandleUpdate(cmds);
    } else if (cmdType == "replace") {
        if (cmds.size() != 5) {
            cli_print_buffer("Input parameters failed!, num:%zu.\n", cmds.size());
            return;
        }
        HandleReplace(cmds);
    } else if (cmdType == "delete") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%zu.\n", cmds.size());
            return;
        }
        HandleDelete(cmds);
    } else if (cmdType == "catchup") {
        HandleCatchUp();
    }  else if (cmdType == "trace") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%zu\n", cmds.size());
            return;
        }
        HandleTrace(cmds);
    } else if (cmdType == "perf") {
        if (cmds.size() != 9 && (cmds.size() != 10 || cmds[9] != "remote")) {
            cli_print_buffer("Input parameters failed!, num:%zu\n", cmds.size());
            return;
        }
        HandlePerf(cmds);
    } else if (cmdType == "perfcheck") {
        if (cmds.size() != 8 && (cmds.size() != 9 || cmds[8] != "remote")) {
            cli_print_buffer("Input parameters failed!, num:%zu\n", cmds.size());
            return;
        }
        cmds.insert(cmds.begin() + 1, "check");
        HandlePerf(cmds);
    } else if (cmdType == "set") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%zu\n", cmds.size());
            return;
        }
        HandleSet(cmds);
    } else if (cmdType == "notify") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%zu\n", cmds.size());
            return;
        }
        HandleNotify(cmds);
    } else if (cmdType == "exit") {
        return;
    } else {
        MmsClientDebugHelp(argv[0], 1);
    }
}
