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

#ifndef BIO_HSECRYPTORHELPER_H
#define BIO_HSECRYPTORHELPER_H
#include <sys/types.h>

#include <cstdint>
#include <string>
#include <mutex>

#include "hse_cryptor.h"

namespace ock {
namespace bio {
class BioCryptorHelper {
public:
    BioCryptorHelper(std::string kfsMaster, std::string kfsStandby) noexcept;

public:
    int Decrypt(int domainId, const std::string &filePath, std::pair<char *, int> &result) noexcept;
    int CheckMasterKeyExpired(int domainId, bool &isRkExpired, uint32_t lead) noexcept;
    static void EraseDecryptData(std::pair<char *, int> &data) noexcept;

private:
    class CryptorHelperInitializer {
    public:
        CryptorHelperInitializer(const std::string &m, const std::string &s) noexcept;
        ~CryptorHelperInitializer() noexcept;

    public:
        bool Initialized() const noexcept;

    private:
        bool initialized;
        std::unique_lock<std::mutex> lockGuard;
    };

private:
    const std::string kfsMasterPath;
    const std::string kfsStandbyPath;
    static std::mutex globalMutex;
};
}
}

#endif // BIO_HSECRYPTORHELPER_H
