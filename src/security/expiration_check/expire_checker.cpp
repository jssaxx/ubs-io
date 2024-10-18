/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 */

#include "expire_checker.h"
#include <bio_types.h>
#include <iostream>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <ctime>
#include "bio_config_instance.h"
#include "bio_c.h"

namespace ock {
namespace bio {
// 加载证书
X509* LoadCertificate(const char* filename)
{
    FILE* fp = fopen(filename, "r");
    if (!fp) {
        LOG_ERROR("CERT Failed to open certificate file: " << filename);
        return nullptr;
    }

    X509* x509 = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!x509) {
        LOG_ERROR("CERT Failed to read certificate from file: " << filename);
    }

    return x509;
}

// 获取当前时间
time_t GetCurrentTime()
{
    time_t currentTime;
    time(&currentTime);
    return currentTime;
}

// 校验证书的有效时间
ExpireChecker::CertStatus ValidateCertificateTime(X509* x509)
{
    if (!x509) {
        LOG_ERROR("CERT Invalid certificate");
        return ExpireChecker::CertStatus::CERT_FAIL;
    }
    // 获取证书的有效时间
    ASN1_TIME* notBefore = X509_get_notBefore(x509);
    ASN1_TIME* notAfter = X509_get_notAfter(x509);
    // 将 ASN1_TIME 转换为 time_t
    time_t now = GetCurrentTime();
    struct tm tm;
    time_t notBeforeTime = ASN1_TIME_to_tm(notBefore, &tm) ? mktime(&tm) : -1;
    time_t notAfterTime = ASN1_TIME_to_tm(notAfter, &tm) ? mktime(&tm) : -1;
    if (notBeforeTime == -1 || notAfterTime == -1) {
        LOG_ERROR("CERT Failed to convert certificate times");
        return ExpireChecker::CertStatus::CERT_FAIL;
    }
    // 比较当前时间与证书的有效时间
    if (now < notBeforeTime) {
        LOG_ERROR("CERT Certificate is not yet valid");
        return ExpireChecker::CertStatus::CERT_YET_VALID;
    }

    if (notAfterTime > now) {
        auto timeLeft = difftime(notAfterTime, now);
        if (timeLeft < NO_7 * NO_24 * NO_60 * NO_60) {
            LOG_WARN("CERT Certificate expires in seven days.");
            return ExpireChecker::CertStatus::CERT_NEAR_EXPIRE;
        }
    }

    if (now > notAfterTime) {
        LOG_ERROR("CERT Certificate has expired");
        return ExpireChecker::CertStatus::CERT_EXPIRED;
    }

    LOG_INFO("CERT Certificate is valid");
    return ExpireChecker::CertStatus::CERT_SUCCESS;
}

static inline void HandleCheckCert(std::string caCertFile, std::string workCertFile)
{
    // 加载证书
    X509* caX509 = LoadCertificate(caCertFile.c_str());
    if (!caX509) {
        LOG_ERROR("CERT Ca Certificate load fail.");
        return;
    }

    X509* workX509 = LoadCertificate(workCertFile.c_str());
    if (!workX509) {
        LOG_ERROR("CERT Server Certificate load fail.");
        return;
    }
    // 校验证书的有效时间
    auto isValid = ValidateCertificateTime(caX509);
    switch (isValid) {
        case ExpireChecker::CertStatus::CERT_FAIL:LOG_ERROR("CERT Ca Certificate fail.");break;
        case ExpireChecker::CertStatus::CERT_YET_VALID:LOG_WARN("CERT Ca Certificate is not yet valid.");break;
        case ExpireChecker::CertStatus::CERT_NEAR_EXPIRE:LOG_WARN("CERT Ca Certificate expires in seven days.");break;
        case ExpireChecker::CertStatus::CERT_EXPIRED:LOG_WARN("CERT Ca Certificate has expired.");break;
        default:LOG_INFO("CERT Ca Certificate success.");
    }
    isValid = ValidateCertificateTime(workX509);
    switch (isValid) {
        case ExpireChecker::CertStatus::CERT_FAIL:LOG_ERROR("CERT work Certificate fail.");break;
        case ExpireChecker::CertStatus::CERT_YET_VALID:LOG_WARN("CERT work Certificate is not yet valid.");break;
        case ExpireChecker::CertStatus::CERT_NEAR_EXPIRE:
            LOG_WARN("CERT work Certificate expires in seven days.");break;
        case ExpireChecker::CertStatus::CERT_EXPIRED:LOG_WARN("CERT server Certificate has expired.");break;
        default:LOG_INFO("CERT work Certificate success.");
    }

    // 释放证书
    X509_free(caX509);
    X509_free(workX509);
}

void IsExpiredCheck(int period, std::string tlsCaCertFile, std::string tlsWorkCertFile)
{
    while (true) {
        try {
            HandleCheckCert(tlsCaCertFile, tlsWorkCertFile);
            sleep(period);
        } catch (...) {
            std::string mLastErrorMessage = "CERT Unknown exception in cert check.";
            LOG_ERROR(mLastErrorMessage);
        }
    }
    LOG_INFO("CERT Cert check is stopped.");
}

void ExpireChecker::TimingManagement(const std::string caCertFile, const std::string workCertFile, int period) const
{
    LOG_INFO("CERT TimingManagement started to create thread cert time.");
    if (caCertFile.empty()) {
        LOG_WARN("CERT The caCertFile is null!");
        return;
    }
    if (workCertFile.empty()) {
        LOG_WARN("CERT The workCertFile is null!");
        return;
    }
    std::thread checkManager(IsExpiredCheck, period, caCertFile, workCertFile);
    if (pthread_setname_np(checkManager.native_handle(), "expireChecker") != 0) {
        LOG_WARN("CERT Change checkManager thread name failed.");
    }
    checkManager.detach();
}

BResult ExpireChecker::ExpireCheckerInit(std::string caCertPath, std::string workCertPath)
{
    if (!FileUtil::Exist(caCertPath)) {
        LOG_ERROR("CERT ca cert path is not exit");
        return BIO_ERR;
    }
    if (!FileUtil::Exist(workCertPath)) {
        LOG_ERROR("CERT ca cert path is not exit");
        return BIO_ERR;
    }

    TimingManagement(caCertPath, workCertPath, NO_864 * NO_100);
    return BIO_OK;
}
}
}