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

#ifndef OCK_MMS_EXPIRE_CHECKER_H
#define OCK_MMS_EXPIRE_CHECKER_H

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <memory>
#include "mms_err.h"
#include "mms_ref.h"
#include "mms_openssl_api_wrapper.h"

namespace ock {
namespace mms {
class ExpireChecker;
using ExpireCheckerPtr = Ref<ExpireChecker>;
constexpr int ONE_DAY = 24 * 60 * 60;
constexpr uint32_t CHECK_SEVEN_DAY = 7 * 24 * 60 * 60;

class ExpireChecker {
public:
    ExpireChecker();

    virtual ~ExpireChecker();

    static ExpireCheckerPtr &Instance()
    {
        static auto instance = MakeRef<ExpireChecker>();
        return instance;
    }

    void TimingManagement();

    BResult ExpireCheckerInit(const std::string &caCertPath, const std::string &workCertPath,
                              const std::string &opensslDir);

    DEFINE_REF_COUNT_FUNCTIONS;
private:
    void StopThread();
    BResult HandleCertExpiredCheck();
    BResult CertExpiredCheck(const std::string &path, const std::string &type);
    BResult ValidateCertificateTime(X509* x509, const std::string &type);

private:
    std::string mCaPath;
    std::string mCertPath;
    mutable std::thread workerThread;
    mutable std::atomic<bool> stopFlag;
    mutable std::condition_variable cv;
    mutable std::mutex mutex;
    DEFINE_REF_COUNT_VARIABLE;
};

} // namespace mms
} // namespace ock

#endif // OCK_MMS_EXPIRE_CHECKER_H

