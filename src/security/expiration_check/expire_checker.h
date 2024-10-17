/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 */
#ifndef PLATFORM_UTILITIES_PLATFORM_UTILITIES_EXPIRE_CHECKER_H
#define PLATFORM_UTILITIES_PLATFORM_UTILITIES_EXPIRE_CHECKER_H

#include <thread>
#include <string>
#include "bio_err.h"
#include "bio_ref.h"

namespace ock {
namespace bio {
class ExpireChecker;
using ExpireCheckerPtr = Ref<ExpireChecker>;
class ExpireChecker {
public:
    enum class CertStatus {
        CERT_SUCCESS = 0,
        CERT_FAIL = 1,
        CERT_YET_VALID = 2,
        CERT_NEAR_EXPIRE = 3,
        CERT_EXPIRED = 4,
        CERT_STATUS_BUTT
    };

    ExpireChecker() = default;
    virtual ~ExpireChecker() = default;

    static ExpireCheckerPtr &Instance()
    {
        static auto instance = MakeRef<ExpireChecker>();
        return instance;
    }
    void TimingManagement(const std::string caCertFile, const std::string serverCertFile, int period) const;

    BResult ExpireCheckerInit(std::string caCertPath, std::string workCertPath);
    DEFINE_REF_COUNT_FUNCTIONS
private:
    DEFINE_REF_COUNT_VARIABLE
};
}
}

#endif // PLATFORM_UTILITIES_PLATFORM_UTILITIES_EXPIRE_CHECKER_H
