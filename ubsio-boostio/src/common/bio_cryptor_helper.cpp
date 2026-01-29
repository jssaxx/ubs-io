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

#include <sys/ipc.h>
#include <fstream>

#include "bio_log.h"
#include "bio_cryptor_helper.h"

using namespace ock::hse;
using namespace ock::bio;

std::mutex BioCryptorHelper::globalMutex;

static constexpr int MIN_VALID_KEY = 0x20161111;
static constexpr int MAX_VALID_KEY = 0x20169999;
static constexpr int64_t MAX_KEY_LENGTH = 10L * 1024L * 1024L;

BioCryptorHelper::BioCryptorHelper(std::string kfsMaster, std::string kfsStandby) noexcept
    : kfsMasterPath{ std::move(kfsMaster) }, kfsStandbyPath{ std::move(kfsStandby) }
{}

static int ReadFile(const std::string &path, std::string &content) noexcept
{
    std::ifstream in(path);
    if (!in.is_open()) {
        LOG_ERROR("Failed to open the file.");
        return -1;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    content = buffer.str();
    return 0;
}

int BioCryptorHelper::Decrypt(int domainId, const std::string &filePath, std::pair<char *, int> &result) noexcept
{
    struct stat fileStat {};
    auto ret = stat(filePath.c_str(), &fileStat);
    if (ret != 0) {
        LOG_ERROR("Stat for private key file failed: " << errno << " : " << strerror(errno) << ".");
        return -1;
    }
    if (fileStat.st_size > MAX_KEY_LENGTH) {
        LOG_ERROR("Stat for private key file is abnormal, size too large: " << fileStat.st_size << ".");
        return -1;
    }
    std::string encryptedText;
    ret = ReadFile(filePath, encryptedText);
    if (ret != 0) {
        LOG_ERROR("Read private key file error: " << ret);
        return -1;
    }

    auto buffer = new (std::nothrow) char[encryptedText.length()];
    if (buffer == nullptr) {
        LOG_ERROR("allocate memory for buffer failed");
        return -1;
    }

    CryptorHelperInitializer initializer{ kfsMasterPath, kfsStandbyPath };
    if (!initializer.Initialized()) {
        LOG_ERROR("CryptorHelperInitializer failed for file");
        delete[] buffer;
        return -1;
    }

    ret = HseCryptor::RefreshMkMask();
    if (ret != 0) {
        LOG_ERROR("Refresh hse mk mask failed: " << ret << " for file.");
        delete[] buffer;
        return -1;
    }

    auto encryptLength = encryptedText.length();
    if (encryptLength > INTMAX_MAX) {
        LOG_ERROR("EncryptedText length too long");
        delete[] buffer;
        return -1;
    }
    auto dataLength = static_cast<int>(encryptLength);
    ret = HseCryptor::Decrypt(domainId, encryptedText, buffer, dataLength);
    if (ret != 0) {
        LOG_ERROR("Decrypt tls key password failed: " << ret << " for file.");
        delete[] buffer;
        return -1;
    }

    LOG_INFO("decrypt success.");
    result = std::make_pair(buffer, dataLength);
    return 0;
}

int BioCryptorHelper::CheckMasterKeyExpired(int domainId, bool &isRkExpired, uint32_t lead) noexcept
{
    CryptorHelperInitializer initializer{ kfsMasterPath, kfsStandbyPath };
    if (!initializer.Initialized()) {
        return -1;
    }

    return HseCryptor::CheckMasterKeyExpired(domainId, isRkExpired, lead);
}

void BioCryptorHelper::EraseDecryptData(std::pair<char *, int> &data) noexcept
{
    if (data.first != nullptr) {
        for (auto i = 0; i < data.second; i++) {
            data.first[i] = '\0';
        }
        delete[] data.first;
        data.first = nullptr;
    }
    data.second = 0;
}

BioCryptorHelper::CryptorHelperInitializer::CryptorHelperInitializer(const std::string &m,
    const std::string &s) noexcept
    : initialized{ false }, lockGuard{ globalMutex }
{
    KmcCryptConfigBuilder builder;
    auto key = ftok(m.c_str(), 0);
    if (key < 0) {
        LOG_ERROR("get key failed: " << errno << ":" << strerror(errno));
        return;
    }

    auto config = builder.MasterKsfFile(m)
                      .StandByKsfFile(s)
                      .SemKey(MIN_VALID_KEY + key % (MAX_VALID_KEY - MIN_VALID_KEY))
                      .LogLevel(CryptLogLevel::HSE_CRYPT_LOG_INFO)
                      .Build();
    auto ret = HseCryptor::Initialize(config);
    if (ret != 0) {
        LOG_ERROR("Cryptor init failed: " << ret);
        return;
    }

    LOG_INFO("Cryptor initialize success.");
    initialized = true;
}

BioCryptorHelper::CryptorHelperInitializer::~CryptorHelperInitializer() noexcept
{
    if (initialized) {
        HseCryptor::UnInitialize();
        LOG_INFO("Bio cryptor un-initialized.");
    }
}

bool BioCryptorHelper::CryptorHelperInitializer::Initialized() const noexcept
{
    return initialized;
}
