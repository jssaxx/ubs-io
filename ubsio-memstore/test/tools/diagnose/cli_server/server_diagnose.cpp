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
#include <time.h>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <mutex>
#include <ctime>
#include "tracer.h"
#include "cli.h"
#include "mms_config_instance.h"
#include "mms_log.h"
#include "mms_server.h"
#include "mms_functions.h"
#include "mms_crc_util.h"
#include "server_diagnose.h"

using namespace ock::mms;

static void MmsServerDebugProcess(int argc, char *argv[]) noexcept;
static void MmsServerDebugHelp(char *command, int detail) noexcept;

static void InitRandom()
{
    srand(static_cast<unsigned>(time(nullptr)));
}

// 随机填充数据
static void FillRandomData(char *value, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        value[i] = (char)(rand() % 256);  // 填充随机字节 (0~255)
    }
}

class DiagLogger {
public:
    DiagLogger(const DiagLogger &) = delete;
    DiagLogger &operator=(const DiagLogger &) = delete;

    static DiagLogger &instance()
    {
        static DiagLogger instance;
        return instance;
    }

    bool init(const std::string &filePath)
    {
        if (filePath.empty()) {
            return false;
        }

        logFile.open(filePath, std::ios::out | std::ios::trunc);
        if (!logFile.is_open()) {
            cli_print_buffer("Failed to open file:%s, reason:%s\n", filePath.c_str(), strerror(errno));
            return false;
        }

        return true;
    }

    void write(const std::string &msg)
    {
        std::lock_guard<std::mutex> lock(fileLock);
        logFile << msg << std::endl;
        logFile.flush();
    }

private:
    DiagLogger() = default;

    ~DiagLogger()
    {
        if (logFile.is_open()) {
            logFile.close();
        }
    }

    std::ofstream logFile;
    std::mutex fileLock;
};

class LogStream {
public:
    ~LogStream()
    {
        DiagLogger::instance().write(msgStream.str());
    }

    template<typename T>
    LogStream &operator<<(const T &value)
    {
        msgStream << value;
        return *this;
    }

private:
    std::ostringstream msgStream;
};

#define LOG_FILE(msg) LogStream() << msg

static bool mInited = false;
int diagnose::MmsServerCommand::Initialize() noexcept
{
    if (mInited) {
        return 0;
    }

    CliCommand command;
    strncpy(command.command, "mms", CLI_MAX_COMMAND_LEN);
    strncpy(command.description, "mms commands.", CLI_MAX_CMD_DESC_LEN);
    command.handler = MmsServerDebugProcess;
    command.help_handler = MmsServerDebugHelp;
    auto result = cli_register_command(&command);
    if (result != 0) {
        printf("register MmsServer diagnose failed.");
    }
    mInited = true;
    return result;
}

void diagnose::MmsServerCommand::Destroy() noexcept
{
    cli_unregister_command((char *)"mms");
}

static uint32_t mUpdateLength = 0;
static bool mIsReady = false;

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
    uint16_t readWriteRate;
};

typedef void *(*perfTestRunner)(void *param);

static bool CanConvertToUint64(const std::string &str, uint64_t &val)
{
    try {
        std::size_t pos;
        val = std::stoull(str, &pos);
        if (pos < str.size() && str.find_first_not_of(" \t\n\v\f\r", pos) != std::string::npos) {
            return false;
        }
        return true;
    } catch (const std::invalid_argument &ia) {
        return false;
    } catch (const std::out_of_range &oor) {
        return false;
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

    PutItems item = { const_cast<char *>(key), value, length };

    auto ret = MmsPut(userId, &item, NO_1);
    if (ret != RET_MMS_OK) {
        cli_print_buffer("Failed to put a value, result:%d.\n", ret);
    } else {
        cli_print_buffer("Put value success, key:%s, length:%llu.\n", key, length);
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
    uint64_t realLen = length;

    GetItems item = { const_cast<char *>(key), offset, length, value, &realLen };

    auto ret = MmsGet(userId, &item, NO_1);
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

    UpdateItems item = { const_cast<char *>(key), value, offset, length };

    auto ret = MmsUpdate(userId, &item, NO_1);
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

    UpdateItems item = { const_cast<char *>(key), value, offset, length };

    auto ret = MmsReplace(userId, &item, NO_1);
    if (ret != RET_MMS_OK) {
        cli_print_buffer("Failed to replace a value, result:%d.\n", ret);
    } else {
        cli_print_buffer("Replace value success, key:%s, offset:%llu, length:%llu.\n", key, offset, length);
    }
    delete[] value;
    fclose(fp);
}

static void HandleDelete(std::vector<std::string> cmds)
{
    uint64_t userId = std::stoull(cmds[1]);
    auto key = cmds[2].c_str();

    DeleteItems item = { const_cast<char *>(key) };

    auto ret = MmsDelete(userId, &item, NO_1);
    if (ret != RET_MMS_OK) {
        cli_print_buffer("Failed to delete, key%s, result:%d.\n", key, ret);
    } else {
        cli_print_buffer("Delete key success, key:%s.\n", key);
    }
}

static void HandleSet(std::vector<std::string> cmds)
{
    mUpdateLength = std::stoul(cmds[1]);
    cli_print_buffer("reset update length: %u.\n", mUpdateLength);
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
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[128];
        itemList[i].value = new char[length];
        FillRandomData(itemList[i].value, length);
        itemList[i].length = length;
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            sprintf(itemList[i].key, "key_%lu_%u_%u_%d", getParam->usrId, getParam->id, getParam->cpu,
                    keyIndex.load());
            keyIndex++;
        }
        auto ret = MmsPut(getParam->usrId, itemList, getParam->batchNum);
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

    uint64_t realLength;
    GetItems *itemList = new GetItems[getParam->batchNum];
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[128];
        itemList[i].offset = 0;
        itemList[i].length = length;
        itemList[i].value = new char[length];
        memset(itemList[i].value, 88, length);
        itemList[i].realLength = &realLength;
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            sprintf(itemList[i].key, "key_%lu_%u_%u_%d", getParam->usrId, getParam->id, getParam->cpu,
                    keyIndex.load());
            keyIndex++;
        }
        auto ret = MmsGet(getParam->usrId, itemList, getParam->batchNum);
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
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[128];
        itemList[i].value = new char[length];
        FillRandomData(itemList[i].value, length);
        itemList[i].offset = 0;
        itemList[i].length = length;
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            sprintf(itemList[i].key, "key_%lu_%u_%u_%d", getParam->usrId, getParam->id, getParam->cpu,
                    keyIndex.load());
            keyIndex++;
        }
        auto ret = MmsUpdate(getParam->usrId, itemList, getParam->batchNum);
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

    UpdateItems *itemList = new UpdateItems[getParam->batchNum];
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[128];
        itemList[i].value = new char[length];
        FillRandomData(itemList[i].value, length);
        itemList[i].offset = 0;
        itemList[i].length = length;
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            sprintf(itemList[i].key, "key_%lu_%u_%u_%d", getParam->usrId, getParam->id, getParam->cpu,
                    keyIndex.load());
            keyIndex++;
        }
        auto ret = MmsReplace(getParam->usrId, itemList, getParam->batchNum);
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
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[128];
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            sprintf(itemList[i].key, "key_%lu_%u_%u_%d", getParam->usrId, getParam->id, getParam->cpu,
                    keyIndex.load());
            keyIndex++;
        }
        auto ret = MmsDelete(getParam->usrId, itemList, getParam->batchNum);
        if (ret != RET_MMS_OK) {
            getParam->result = ret;
            break;
        }
    }

    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        delete[] itemList[i].key;
    }
    delete[] itemList;
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
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        putList[i].key = new char[128];
        putList[i].value = new char[length];
        memset(putList[i].value, 66, length);
        putList[i].length = length;
    }

    uint64_t realLength;
    GetItems *getList = new GetItems[getParam->batchNum];
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        getList[i].key = new char[128];
        getList[i].offset = 0;
        getList[i].length = length;
        getList[i].value = new char[length];
        memset(getList[i].value, 88, length);
        getList[i].realLength = &realLength;
    }

    for (uint32_t idx = 0; idx < 10; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            sprintf(putList[i].key, "key_%lu_%u_%u_%d", getParam->usrId, getParam->id, getParam->cpu,
                    keyIndex.load());
            keyIndex++;
        }
        auto ret = MmsPut(getParam->usrId, putList, getParam->batchNum);
        if (ret != RET_MMS_OK) {
            getParam->result = ret;
            break;
        }
    }

    uint16_t readRate = getParam->readWriteRate;
    for (uint32_t idx = 10; idx < getParam->count; idx++) {
        int32_t randnum = rand();
        if (randnum % 10 >= readRate) {
            for (uint32_t i = 0; i < getParam->batchNum; i++) {
                sprintf(putList[i].key, "key_%lu_%u_%u_%d", getParam->usrId, getParam->id, getParam->cpu,
                        keyIndex.load());
                keyIndex++;
            }
            auto ret = MmsPut(getParam->usrId, putList, getParam->batchNum);
            if (ret != RET_MMS_OK) {
                getParam->result = ret;
                break;
            }
        } else {
            for (uint32_t i = 0; i < getParam->batchNum; i++) {
                sprintf(getList[i].key, "key_%lu_%u_%u_%d", getParam->usrId, getParam->id, getParam->cpu,
                        randnum % keyIndex.load());
            }
            auto ret = MmsGet(getParam->usrId, getList, getParam->batchNum);
            if (ret != RET_MMS_OK) {
                getParam->result = ret;
                break;
            }
        }
    }

    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        delete[] putList[i].key;
        delete[] putList[i].value;
    }
    delete[] putList;
    getParam->done = true;
    sem_post(&getParam->sem);
    return nullptr;
}

int WriteToFile(const char *path, const char *value, size_t length)
{
    if (path == nullptr || value == nullptr) {
        return -1;
    }

    FILE *fp = fopen(path, "wb");  // 若文件不存在则创建
    if (!fp) {
        cli_print_buffer("Failed to open file:%s, error:%s\n", path, strerror(errno));
        return -1;
    }

    size_t written = fwrite(value, 1, length, fp);
    if (written != length) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

struct CheckPara {
    void *para;
    std::unordered_map<std::string, uint32_t> *dataCrc;
    pthread_mutex_t *mutex;
};

// 从文件里提取crc信息
bool GetCrcFromFile(const std::string &file, std::unordered_map<std::string, uint32_t> &dataCrc)
{
    std::ifstream ifs(file);
    if (!ifs.is_open()) {
        cli_print_buffer("Failed to open file%s\n: ", file.c_str());
        return false;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        std::string key;
        uint32_t crc = 0;

        size_t keyPos = line.find("key:");
        size_t crcPos = line.find(", put crc:");
        if (keyPos == std::string::npos || crcPos == std::string::npos) {
            continue; // 跳过格式不对的行
        }

        key = line.substr(keyPos + 4, crcPos - (keyPos + 4));

        std::string crcStr = line.substr(crcPos + 10);
        try {
            crc = static_cast<uint32_t>(std::stoul(crcStr));
        } catch (...) {
            cli_print_buffer("Failed to parse CRC in line%s\n: ", line.c_str());
            return false;
        }

        dataCrc[key] = crc;
    }

    return true;
}

// 校验数据一致性写
static void *PerfTestPutCheckData(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    CheckPara *checkPara = (CheckPara *)param;

    PerfTestParam *getParam = (PerfTestParam *)checkPara->para;
    std::unordered_map<std::string, uint32_t> &dataCrc = *(checkPara->dataCrc);
    std::atomic<int32_t> keyIndex(0);

    uint32_t length = (mUpdateLength != 0) ? mUpdateLength : getParam->length;
    getParam->result = 0;

    ReplaceItems *itemList = new ReplaceItems[getParam->batchNum];
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[128];
        itemList[i].value = new char[length];
        itemList[i].offset = 0;
        itemList[i].length = length;
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            sprintf(itemList[i].key, "key_%lu_%u_%u_%d", getParam->usrId, getParam->id, getParam->cpu,
                    keyIndex.load());
            std::string keyStr(itemList[i].key);
            FillRandomData(itemList[i].value, length);
            std::string filePath = "./perf_input/" + keyStr;
            int ret = WriteToFile(filePath.c_str(), itemList[i].value, length);
            if (ret != 0) {
                getParam->result = ret;
                break;
            }

            pthread_mutex_lock(checkPara->mutex);
            dataCrc[keyStr] = MmsCrcUtil::Crc32(itemList[i].value, itemList[i].length);
            pthread_mutex_unlock(checkPara->mutex);
            keyIndex++;
        }
        auto ret = MmsReplace(getParam->usrId, itemList, getParam->batchNum);
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
    getParam->done = true;
    return nullptr;
}

// 校验数据一致性读
static void *PerfTestGetCheckData(void *param)
{
    while (!mIsReady) {
        usleep(1);
    }

    CheckPara *checkPara = (CheckPara *)param;

    PerfTestParam *getParam = (PerfTestParam *)checkPara->para;
    std::unordered_map<std::string, uint32_t> &dataCrc = *(checkPara->dataCrc);
    std::atomic<int32_t> keyIndex(0);

    getParam->result = 0;
    uint32_t length = (mUpdateLength != 0) ? mUpdateLength : getParam->length;

    uint64_t realLength;
    GetItems *itemList = new GetItems[getParam->batchNum];
    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        itemList[i].key = new char[128];
        itemList[i].offset = 0;
        itemList[i].length = length;
        itemList[i].value = new char[length];
        memset(itemList[i].value, 88, length);
        itemList[i].realLength = &realLength;
    }

    for (uint32_t idx = 0; idx < getParam->count; idx++) {
        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            sprintf(itemList[i].key, "key_%lu_%u_%u_%d", getParam->usrId, getParam->id, getParam->cpu,
                    keyIndex.load());
            keyIndex++;
        }

        auto ret = MmsGet(getParam->usrId, itemList, getParam->batchNum);
        if (ret != RET_MMS_OK) {
            getParam->result = ret;
            break;
        }

        for (uint32_t i = 0; i < getParam->batchNum; i++) {
            std::string keyStr(itemList[i].key);
            std::string filePath = "./perf_output/" + keyStr;
            int ret = WriteToFile(filePath.c_str(), itemList[i].value, *itemList[i].realLength);
            if (ret != 0) {
                getParam->result = ret;
                break;
            }

            pthread_mutex_lock(checkPara->mutex);
            dataCrc[keyStr] = MmsCrcUtil::Crc32(itemList[i].value, *itemList[i].realLength);
            pthread_mutex_unlock(checkPara->mutex);
        }
    }

    for (uint32_t i = 0; i < getParam->batchNum; i++) {
        delete[] itemList[i].key;
        delete[] itemList[i].value;
    }
    delete[] itemList;
    getParam->done = true;
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
    uint16_t readWriteRate = 7; // 默认7:3
    if (memcmp(rw, "mixes", sizeof("mixes")) == 0 && (cmds.size() == 11)) {
        readWriteRate = std::stol(cmds[10]);
    }
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
            param[index].readWriteRate = readWriteRate;
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
    cli_print_buffer("  total IO count             : %d\n", static_cast<int>(totalCount));
    cli_print_buffer("  total spent                : %.2f ms\n", time_use / 1000U);
    cli_print_buffer("  throughput                 : %.4f MB/s\n", dataPerf * bwFactor);
    cli_print_buffer("  IOPS                       : %.2f /s\n", iops);
    cli_print_buffer("  latency                    : %.2f (us)\n", time_use / count);
    cli_print_buffer("Perf Test End.\n");

    mIsReady = false;

    free(param);
    free(th);
}

static bool HandlePerfCheckImpl(std::vector<std::string> cmds, std::unordered_map<std::string, uint32_t> &dataCrc)
{
    uint32_t bs = (std::stoul(cmds[3]) * 1024);
    uint32_t ioDepth = std::stoul(cmds[4]);
    uint32_t batchNum = std::stoul(cmds[5]);
    uint64_t size = (std::stoul(cmds[6]) * 1024 * 1024);
    uint64_t userId = std::stoul(cmds[7]);
    uint32_t numaNum = std::stol(cmds[8]);
    uint32_t cpuNum = std::stol(cmds[9]);
    uint32_t cpuStart = std::stol(cmds[10]);
    auto rw = cmds[11].c_str();
    auto count = size / bs / batchNum / ioDepth;
    if (bs == 0 || batchNum == 0 || ioDepth == 0) {
        cli_print_buffer("Invalid para, bs:%u, batchNum:%u, ioDepth:%u", bs, batchNum, ioDepth);
        return false;
    }

    perfTestRunner runner = nullptr;
    if (memcmp(rw, "put", sizeof("put")) == 0) {
        runner = PerfTestPutCheckData;
    } else if (memcmp(rw, "get", sizeof("get")) == 0) {
        runner = PerfTestGetCheckData;
    } else {
        cli_print_buffer("Invalid operate type:%s.\n", rw);
        return false;
    }

    cli_print_buffer("Perf check test start, operate:%s, bs:%u, ioDepth:%u, batchNum:%u, size:%u, count:%u.\n", rw, bs,
                 ioDepth, batchNum, size, count);
    pthread_t *th = (pthread_t *)malloc(sizeof(pthread_t) * ioDepth);
    PerfTestParam *param = (PerfTestParam *)malloc(sizeof(PerfTestParam) * ioDepth);
    CheckPara *checkParas = (CheckPara *) malloc(sizeof(CheckPara) * ioDepth);
    if (th == nullptr || param == nullptr || checkParas == nullptr) {
        cli_print_buffer("Malloc memory failed.\n");
        return false;
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

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    for (uint32_t idx = 0; idx < ioDepth; ++idx) {
        checkParas[idx] = {&param[idx], &dataCrc, &mutex};
    }

    for (uint32_t i = 0; i < ioDepth; i++) {
        int ret = pthread_create(&th[i], nullptr, runner, &checkParas[i]);
        if (ret != 0) {
            cli_print_buffer("Perf test create pthread failed, ret:%d.\n", ret);
            free(param);
            free(th);
            free(checkParas);
            return false;
        }
        std::string threadName = "perf-io-" + std::to_string(i);
        threadName += "-" + std::to_string(i);
        if (pthread_setname_np(th[i], threadName.c_str()) != 0) {
            cli_print_buffer("Failed to set name of BoostIO thread.\n");
            free(param);
            free(th);
            free(checkParas);
            return false;
        }

        cpu_set_t cpuSet;
        CPU_ZERO(&cpuSet);
        CPU_SET(param[i].cpu, &cpuSet);
        if (pthread_setaffinity_np(th[i], sizeof(cpuSet), &cpuSet) != 0) {
            cli_print_buffer("Failed to bind thread %s, to cpu %u.\n", threadName.c_str(), param[i].cpu);
            free(param);
            free(th);
            free(checkParas);
            return false;
        } else {
            cli_print_buffer("Bind thread %s, to cpu %u.\n", threadName.c_str(), param[i].cpu);
        }
    }

    mIsReady = true;

    struct timeval startT;
    struct timeval stopT;
    gettimeofday(&startT, nullptr);

    for (uint32_t j = 0; j < ioDepth; j++) {
        pthread_join(th[j], nullptr);
    }

    gettimeofday(&stopT, nullptr);
    for (uint32_t k = 0; k < ioDepth; k++) {
        if (param[k].result != 0) {
            cli_print_buffer("Perf test return failed, tid:%u, ret:%d.\n", k, param[k].result);
            free(param);
            free(th);
            return false;
        }
    }

    mIsReady = false;
    cli_print_buffer("Perf %s success.\n", rw);
    free(param);
    free(th);
    free(checkParas);
    return true;
}

static bool PerfCheckLocalWrite(std::vector<std::string> &cmds)
{
    InitRandom();
    std::string filePath = cmds[2];
    bool ret = DiagLogger::instance().init(filePath);
    if (!ret) {
        return false;
    }

    std::vector<std::string> curCmds = cmds;
    curCmds.emplace_back("put");
    std::unordered_map<std::string, uint32_t> dataCrcPut{};
    ret = HandlePerfCheckImpl(curCmds, dataCrcPut);
    if (!ret) {
        cli_print_buffer("Perf put failed.\n");
        return false;
    }

    curCmds.pop_back();
    curCmds.emplace_back("get");
    std::unordered_map<std::string, uint32_t> dataCrcGet{};
    ret = HandlePerfCheckImpl(curCmds, dataCrcGet);
    if (!ret) {
        cli_print_buffer("Perf get failed.\n");
        return false;
    }

    for (auto &item: dataCrcPut) {
        LOG_FILE("key:" << item.first << ", put crc:" << item.second << ", get crc:" <<
                 dataCrcGet[item.first] << ".");
        if (item.second != dataCrcGet[item.first]) {
            cli_print_buffer("Check crc failed, key:%s, put crc:%u, get crc:%u\n", item.first.c_str(), item.second,
                         dataCrcGet[item.first]);
            return false;
        }
    }

    cli_print_buffer("Perf check all data crc success.\n");
    return true;
}

static bool PerfCheckRemoteWrite(std::vector<std::string> &cmds)
{
    std::string filePath =  cmds[2];
    std::unordered_map<std::string, uint32_t> baseDataCrc;
    bool ret = GetCrcFromFile(filePath, baseDataCrc);
    if (!ret) {
        return false;
    }

    std::vector<std::string> curCmds = cmds;
    curCmds.emplace_back("get");
    std::unordered_map<std::string, uint32_t> dataCrcGet{};
    ret = HandlePerfCheckImpl(curCmds, dataCrcGet);
    if (!ret) {
        cli_print_buffer("Perf get failed.\n");
        return false;
    }

    for (auto &item: baseDataCrc) {
        if (item.second != dataCrcGet[item.first]) {
            cli_print_buffer("Check crc failed, key:%s, put crc:%u, get crc:%u\n", item.first.c_str(), item.second,
                         dataCrcGet[item.first]);
            return false;
        }
    }

    cli_print_buffer("Perf check all data crc success.\n");
    return true;
}

static void HandlePerfCheck(std::vector<std::string> &cmds)
{
    std::string checkType = cmds[1];
    bool ret = false;
    if (checkType == "lwrite") {
        ret = PerfCheckLocalWrite(cmds);
    } else if (checkType == "rwrite") {
        ret = PerfCheckRemoteWrite(cmds);
    } else {
        cli_print_buffer("Invalid parameter:%s\n", checkType.c_str());
        return;
    }

    if (!ret) {
        cli_print_buffer("Perf check %s failed.\n", checkType.c_str());
        return;
    }
}

static void MmsServerHandleShow(std::vector<std::string> cmds)
{
    auto cType = cmds[1].c_str();
    std::string cmdType(cType);
    if (cmdType == "net") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        std::string protoStr[4U] = { "RDMA", "TCP", "UDS", "SHM" };
        std::string modeStr[2U] = { "BUSY_POLLING", "EVENT_POLLING" };
        uint32_t executorNum = 0;
        NetOptions rpcOption;
        NetOptions ipcOption;
        MmsServer::Instance()->GetNetEngine()->Show(executorNum, rpcOption, ipcOption);
        cli_print_buffer("mms rpc info: \n");
        cli_print_buffer("  ip: %s:%u, protocol:%s, mode:%s, connect_count:%u, worker groups:%s, worker groups cpuset:%s\n",
            rpcOption.ipMask.c_str(), rpcOption.port, protoStr[rpcOption.protocol].c_str(),
            (rpcOption.isBusyPolling) ? modeStr[0].c_str() : modeStr[1].c_str(), rpcOption.connCount,
            rpcOption.workerGroups.c_str(), rpcOption.workerGroupsCpuSet.c_str());
        cli_print_buffer("mms ipc info: \n");
        cli_print_buffer("  ip: %s:%u, protocol:%s, mode:%s, connect_count:%u, worker groups:%s, worker groups cpuset:%s\n",
            ipcOption.ipMask.c_str(), ipcOption.port, protoStr[ipcOption.protocol].c_str(),
            (ipcOption.isBusyPolling) ? modeStr[0].c_str() : modeStr[1].c_str(), ipcOption.connCount,
            ipcOption.workerGroups.c_str(), ipcOption.workerGroupsCpuSet.c_str());
        cli_print_buffer("mms rpc schedule threads:%u \n", executorNum);
    } else if (cmdType == "pt") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        std::map<uint16_t, CmPtInfo> ptView = MmsServer::Instance()->GetCm()->GetPtView();
        cli_print_buffer("Pt view:\n");
        for (auto &ptEntry : ptView) {
            cli_print_buffer("%s\n", ptEntry.second.ToString().c_str());
        }
        uint16_t status = MmsServer::Instance()->GetCm()->GetServiceStatus();
        cli_print_buffer("Local status  %d\n", status);
    } else if (cmdType == "node") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        std::map<uint16_t, CmNodeInfo> nodeView = MmsServer::Instance()->GetCm()->GetNodeView();
        cli_print_buffer("Node view:\n");
        for (auto &nodeEntry : nodeView) {
            cli_print_buffer("%s\n", nodeEntry.second.ToString().c_str());
        }
        uint16_t localNode = MmsServer::Instance()->GetCm()->GetLocalNid();
        cli_print_buffer("Local id  %d\n", localNode);
    } else if (cmdType == "multicast") {
        auto instance = MmsServer::Instance()->GetMulticastEngine();
        if (instance == nullptr) {
            cli_print_buffer("Multicast is disabled.\n");
            return;
        }
        std::string subsribersInfos = instance->GetMulticastInfoStr();
        cli_print_buffer(subsribersInfos.c_str());
    } else {
        cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
    }
}

static void HandleServerTrace(std::vector<std::string> cmds)
{
    auto cType = cmds[1].c_str();
    std::string viewType(cType);
    if (viewType == "show") {
        auto info = ock::tracemark::TraceMark::GetTraceInfo();
        cli_print_buffer(info.c_str());
    } else if (viewType == "clear") {
        ock::tracemark::TraceMark::ClearTrace();
        cli_print_buffer("clearing statistics server records succeeded.\n");
    } else if (viewType == "open") {
        ock::tracemark::TraceMark::SetEnable(true);
        cli_print_buffer("open statistics server records succeeded.\n");
    } else if (viewType == "close") {
        ock::tracemark::TraceMark::SetEnable(false);
        cli_print_buffer("close statistics server records succeeded.\n");
    } else {
        cli_print_buffer("unknown cmd.\n");
    }
}

static void MmsServerDebugHelp(char *command, int detail) noexcept
{
    cli_print_buffer("\tshow: mms show [node/pt/net/multicast]\n");
    cli_print_buffer("\tput value: mms put [userId] [key] [filePath] [length]\n");
    cli_print_buffer("\tget value: mms get [userId] [key] [offset] [length] [filePath]\n");
    cli_print_buffer("\tupdate value: mms update [userId] [key] [filePath] [offset] [length]\n");
    cli_print_buffer("\treplace value: mms replace [userId] [key] [filePath] [offset] [length]\n");
    cli_print_buffer("\tdelete object: mms delete [userId] [key]\n");
    cli_print_buffer("\tcatchup: mms catchup\n");
    cli_print_buffer("\ttrace: mms trace [open/close/show/clear]\n");
    cli_print_buffer("\tperf: mms perf [put/get/update/replace/delete/mixes] [bs(Kb)] [ioDepth] [batchNum] [size(Mb)] "
                 "[userId] [numaNum] [cpuNum] [cpuStart] [readRate]\n");
    cli_print_buffer("\tperf: mms perfcheck [lwrite/rwrite] [filePath] [bs(Kb)] [ioDepth] [batchNum] [size(Mb)] [userId] "
                 "[numaNum] [cpuNum] [cpuStart]\n");
    cli_print_buffer("\texit: exit console\n");
}

static void MmsServerSetCpuAffinity(void)
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

static void MmsServerDebugProcess(int argc, char *argv[]) noexcept
{
    if (argc <= 1) {
        MmsServerDebugHelp(argv[0], 1);
        return;
    }
    std::vector<std::string> cmds;
    for (int i = 1; i < argc; i++) {
        std::string str(argv[i]);
        cmds.emplace_back(str);
    }

    MmsServerSetCpuAffinity();

    std::string cmdType = cmds[0];
    if (cmdType == "put") {
        if (cmds.size() != 5) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandlePut(cmds);
    } else if (cmdType == "catchup") {
        if (cmds.size() != 1) {
            cli_print_buffer("Input parameters failed!, num:%u.\n", cmds.size());
            return;
        }
        HandleCatchUp(cmds);
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
    } else if (cmdType == "show") {
        if (cmds.size() < 2) {
            cli_print_buffer("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        MmsServerHandleShow(cmds);
    } else if (cmdType == "trace") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%d\n", cmds.size());
            return;
        }
        HandleServerTrace(cmds);
    } else if (cmdType == "perf") {
        if (cmds.size() < 10) {
            cli_print_buffer("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandlePerf(cmds);
    } else if (cmdType == "perfcheck") {
        if (cmds.size() != 11) {
            cli_print_buffer("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandlePerfCheck(cmds);
    } else if (cmdType == "set") {
        if (cmds.size() != 2) {
            cli_print_buffer("Input parameters failed!, num:%u\n", cmds.size());
            return;
        }
        HandleSet(cmds);
    } else if (cmdType == "exit") {
        return;
    } else {
        MmsServerDebugHelp(argv[0], 1);
    }
}

int ServerDiagnoseInit()
{
    return ock::mms::diagnose::MmsServerCommand::Initialize();
}
