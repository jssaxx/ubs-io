/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#ifndef BIO_HSECRYPTORHELPER_H
#define BIO_HSECRYPTORHELPER_H
#include <sys/types.h>

#include <cstdint>
#include <string>
#include <mutex>

#include "hse_cryptor.h"
#include "bio_trace.h"
#include "bio_tracepoint_helper.h"

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
