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

#include <iostream>
#include "mms_config_instance.h"
#include "expire_checker.h"

namespace ock {
namespace mms {
ExpireChecker::ExpireChecker() : stopFlag(false) {}

ExpireChecker::~ExpireChecker()
{
    StopThread();
}

void ExpireChecker::StopThread()
{
    stopFlag = true;
    cv.notify_all();
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

BResult ExpireChecker::HandleCertExpiredCheck()
{
    auto ret = CertExpiredCheck(mCertPath, "cert");
    if (ret != MMS_OK) {
        return MMS_ERR;
    }

    ret = CertExpiredCheck(mCaPath, "ca");
    if (ret != MMS_OK) {
        return MMS_ERR;
    }

    return ret;
}

BResult ExpireChecker::CertExpiredCheck(const std::string &path, const std::string &type)
{
    FILE *fp = fopen(path.c_str(), "r");
    if (fp == nullptr) {
        LOG_ERROR("check " << type << " expired failed by unable to open cert file");
        return MMS_ERR;
    }

    X509 *cert = OpenSslApiWrapper::PemReadX509(fp, nullptr, nullptr, nullptr);
    fclose(fp);
    if (cert == nullptr) {
        LOG_ERROR("check " << type << " expired failed by unable to parse cert file");
        return MMS_ERR;
    }

    auto result = ValidateCertificateTime(cert, type);
    if (result != MMS_OK) {
        LOG_ERROR("Validate " << type << " time failed!");
    }

    OpenSslApiWrapper::X509Free(cert);
    return MMS_OK;
}

BResult ExpireChecker::ValidateCertificateTime(X509* x509, const std::string &type)
{
    ASN1_TIME* notBefore = OpenSslApiWrapper::X509GetNotBefore(x509);
    ASN1_TIME* notAfter = OpenSslApiWrapper::X509GetNotAfter(x509);
    if (notBefore == nullptr || notAfter == nullptr) {
        LOG_ERROR(type << " certificate time is nullptr.");
        return MMS_ERR;
    }

    time_t now;
    time(&now);
    struct tm tm{};
    time_t notBeforeTime = OpenSslApiWrapper::Asn1Time2Tm(notBefore, &tm) ? mktime(&tm) : -1;
    time_t notAfterTime = OpenSslApiWrapper::Asn1Time2Tm(notAfter, &tm) ? mktime(&tm) : -1;
    if (notBeforeTime == -1 || notAfterTime == -1) {
        LOG_ERROR(type << " Failed to convert certificate times");
        return MMS_ERR;
    }

    if (now < notBeforeTime) {
        LOG_ERROR(type << " is not yet valid");
        return MMS_ERR;
    }

    if (notAfterTime > now) {
        auto timeLeft = difftime(notAfterTime, now);
        if (timeLeft < CHECK_SEVEN_DAY) {
            LOG_WARN(type << " expires in seven days.");
            return MMS_OK;
        }
    }

    if (now > notAfterTime) {
        LOG_ERROR(type << " has expired");
        return MMS_ERR;
    }

    LOG_INFO(type << " is valid");
    return MMS_OK;
}

void ExpireChecker::TimingManagement()
{
    if (workerThread.joinable()) {
        return;
    }

    stopFlag = false;
    workerThread = std::thread([this]() {
        pthread_setname_np(pthread_self(), "expireChecker");
        while (!stopFlag) {
            HandleCertExpiredCheck();

            std::unique_lock<std::mutex> lock(mutex);
            cv.wait_for(lock, std::chrono::seconds(ONE_DAY), [this] {
                return stopFlag.load();
            });
        }
    });
}

BResult ExpireChecker::ExpireCheckerInit(const std::string &caCertPath, const std::string &workCertPath,
                                         const std::string &opensslDir)
{
    std::string sslDir = opensslDir;
    if (!sslDir.empty()) {
        if (!FileUtil::CanonicalPath(sslDir)) {
            LOG_ERROR("Failed to canonical openssl dir: " << sslDir);
            return MMS_ERR;
        }
    }

    auto ret = OpenSslApiWrapper::Load(sslDir);
    if (ret != MMS_OK) {
        LOG_ERROR("Failed to load openssl lib. ret: " << ret << ".");
        return ret;
    }

    mCaPath = caCertPath;
    mCertPath = workCertPath;
    TimingManagement();
    return MMS_OK;
}
} // namespace mms
} // namespace ock
