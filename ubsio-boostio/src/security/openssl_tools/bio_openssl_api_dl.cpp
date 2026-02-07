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

#include <dlfcn.h>
#include <unistd.h>
#include "bio_log.h"
#include "bio_file_util.h"
#include "bio_openssl_api_dl.h"

#define DLSYM(handle, type, ptr, sym)              \
    do {                                           \
        auto ptr1 = dlsym((handle), (sym));        \
        if (ptr1 == nullptr) {                     \
            LOG_ERROR("Failed to load " << (sym)); \
            return -1;                             \
        }                                          \
        (ptr) = (type)ptr1;                        \
    } while (0)

#define DLSYM_UPDATE(handle, type, ptr, sym1, sym2)        \
    do {                                                   \
        auto ptr1 = dlsym((handle), (sym1));               \
        if (ptr1 == nullptr) {                             \
            ptr1 = dlsym((handle), (sym2));                \
            if (ptr1 == nullptr) {                         \
                LOG_ERROR("Failed to load " << (sym1));    \
                return -1;                                 \
            }                                              \
        }                                                  \
        (ptr) = (type)ptr1;                                \
    } while (0)

namespace ock {
namespace bio {

FuncSslWrite DlOpensslApi::sslWrite = nullptr;
FuncX509StoreCtxGetError DlOpensslApi::x509StoreCtxGetError = nullptr;
FuncSslOperation DlOpensslApi::sslShutdown = nullptr;
FuncRandPoll DlOpensslApi::randPoll = nullptr;
FuncSslGetVersion DlOpensslApi::sslGetVersion = nullptr;
FuncEvpEncryptInitEx DlOpensslApi::evpEncryptInitEx = nullptr;
FuncInit DlOpensslApi::initSsl = nullptr;
FuncX509CrlGet0NextUpdate DlOpensslApi::x509CrlGet0NextUpdate = nullptr;
FuncSslCtxFree DlOpensslApi::sslCtxFree = nullptr;
FuncEvpCipherCtxNew DlOpensslApi::evpCipherCtxNew = nullptr;
FuncRandStatus DlOpensslApi::randStatus = nullptr;
FuncSslOperation DlOpensslApi::sslAccept = nullptr;
FuncSetCertVerifyCallback DlOpensslApi::setCertVerifyCallback = nullptr;
FuncX509GetPubkey DlOpensslApi::x509GetPubkey = nullptr;
FuncSslFree DlOpensslApi::sslFree = nullptr;
FuncX509VerifyCert DlOpensslApi::x509VerifyCert = nullptr;
FuncBioFree DlOpensslApi::bioFree = nullptr;
FuncEvpDecryptUpdate DlOpensslApi::evpDecryptUpdate = nullptr;
FuncSetCipherSuites DlOpensslApi::setCipherSuites = nullptr;
FuncRandBytes DlOpensslApi::randPrivBytes = nullptr;
FuncSslCtxNew DlOpensslApi::sslCtxNew = nullptr;
FuncCheckPrivateKey DlOpensslApi::checkPrivateKey = nullptr;
FuncX509CrlFree DlOpensslApi::x509CrlFree = nullptr;
FuncSslGetError DlOpensslApi::sslGetError = nullptr;
FuncSslCtxSetVerify DlOpensslApi::sslCtxSetVerify = nullptr;
FuncX509StoreCtxSetFlags DlOpensslApi::x509StoreCtxSetFlags = nullptr;
FuncEvpDecryptFinalEx DlOpensslApi::evpDecryptFinalEx = nullptr;
FuncRandBytes DlOpensslApi::randBytes = nullptr;
FuncX509GetNotAfter DlOpensslApi::x509GetNotAfter = nullptr;
FuncEvpAesCipher DlOpensslApi::evpAes128Gcm = nullptr;
FuncEvpPkeyFree DlOpensslApi::evpPkeyFree = nullptr;
FuncPemReadBioX509Crl DlOpensslApi::pemReadBioX509Crl = nullptr;
FuncX509CmpCurrentTime DlOpensslApi::x509CmpCurrentTime = nullptr;
FuncEvpCipherCtxCtrl DlOpensslApi::evpCipherCtxCtrl = nullptr;
FuncSslNew DlOpensslApi::sslNew = nullptr;
FuncSslFd DlOpensslApi::sslSetFd = nullptr;
FuncEvpEncryptUpdate DlOpensslApi::evpEncryptUpdate = nullptr;
FuncX509StoreCtxGet0Store DlOpensslApi::x509StoreCtxGet0Store = nullptr;
FuncEvpEncryptFinalEx DlOpensslApi::evpEncryptFinalEx = nullptr;
FuncX509CrlGet0ByCert DlOpensslApi::x509CrlGet0ByCert = nullptr;
FuncEvpCipherCtxFree DlOpensslApi::evpCipherCtxFree = nullptr;
FuncX509GetNotBefore DlOpensslApi::x509GetNotBefore = nullptr;
FuncSslGetPeerCertificate DlOpensslApi::sslGetPeerCertificate = nullptr;
FuncBioSFile DlOpensslApi::bioSFile = nullptr;
FuncRandSeed DlOpensslApi::randSeed = nullptr;
FuncEvpDecryptInitEx DlOpensslApi::evpDecryptInitEx = nullptr;
FuncSslGetVerifyResult DlOpensslApi::sslGetVerifyResult = nullptr;
FuncAsn1Time2Tm DlOpensslApi::asn1Time2Tm = nullptr;
FuncSetDefaultPasswdCbUserdata DlOpensslApi::setDefaultPasswdCbUserdata = nullptr;
FuncX509Free DlOpensslApi::x509Free = nullptr;
FuncX509VerifyCertErrorString DlOpensslApi::x509VerifyCertErrorString = nullptr;
FuncUsePrivKeyFile DlOpensslApi::usePrivKeyFile = nullptr;
FuncSslGetCurrentCipher DlOpensslApi::sslGetCurrentCipher = nullptr;
FuncSslRead DlOpensslApi::sslRead = nullptr;
FuncBioNew DlOpensslApi::bioNew = nullptr;
FuncOpensslCleanup DlOpensslApi::opensslCleanup = nullptr;
FuncSslOperation DlOpensslApi::sslConnect = nullptr;
FuncSslCtxCtrl DlOpensslApi::sslCtxCtrl = nullptr;
FuncX509StoreAddCrl DlOpensslApi::x509StoreAddCrl = nullptr;
FuncGetMethod DlOpensslApi::tlsServerMethod = nullptr;
FuncEvpAesCipher DlOpensslApi::evpAes256Gcm = nullptr;
FuncInit DlOpensslApi::initCrypto = nullptr;
FuncBioCtrl DlOpensslApi::bioCtrl = nullptr;
FuncPemReadX509 DlOpensslApi::pemReadX509 = nullptr;
FuncLoadVerifyLocations DlOpensslApi::loadVerifyLocations = nullptr;
FuncGetMethod DlOpensslApi::tlsClientMethod = nullptr;

bool DlOpensslApi::gLoaded = false;
const char *DlOpensslApi::gOpensslLibSslName = "libssl.so";
const char *DlOpensslApi::gOpensslLibCryptoName = "libcrypto.so";
const char *DlOpensslApi::gSep = "/";

int DlOpensslApi::LoadSSLSymbols(void *sslHandle)
{
    DLSYM(sslHandle, FuncSslNew, sslNew, "SSL_new");
    DLSYM(sslHandle, FuncSslCtxFree, sslCtxFree, "SSL_CTX_free");
    DLSYM(sslHandle, FuncSslGetError, sslGetError, "SSL_get_error");
    DLSYM(sslHandle, FuncSslWrite, sslWrite, "SSL_write");
    DLSYM(sslHandle, FuncSslOperation, sslShutdown, "SSL_shutdown");
    DLSYM(sslHandle, FuncSetCertVerifyCallback, setCertVerifyCallback, "SSL_CTX_set_cert_verify_callback");
    DLSYM(sslHandle, FuncInit, initCrypto, "OPENSSL_init_crypto");
    DLSYM(sslHandle, FuncSslRead, sslRead, "SSL_read");
    DLSYM(sslHandle, FuncSslGetCurrentCipher, sslGetCurrentCipher, "SSL_get_current_cipher");
    DLSYM(sslHandle, FuncSslGetVersion, sslGetVersion, "SSL_get_version");
    DLSYM(sslHandle, FuncSslFree, sslFree, "SSL_free");
    DLSYM(sslHandle, FuncSslOperation, sslAccept, "SSL_accept");
    DLSYM(sslHandle, FuncSslCtxCtrl, sslCtxCtrl, "SSL_CTX_ctrl");
    DLSYM(sslHandle, FuncSslCtxNew, sslCtxNew, "SSL_CTX_new");
    DLSYM(sslHandle, FuncSslOperation, sslConnect, "SSL_connect");
    DLSYM_UPDATE(sslHandle, FuncSslGetPeerCertificate, sslGetPeerCertificate, "SSL_get_peer_certificate",
        "SSL_get1_peer_certificate");
    DLSYM(sslHandle, FuncGetMethod, tlsClientMethod, "TLS_client_method");
    DLSYM(sslHandle, FuncInit, initSsl, "OPENSSL_init_ssl");
    DLSYM(sslHandle, FuncSslFd, sslSetFd, "SSL_set_fd");
    DLSYM(sslHandle, FuncSslGetVerifyResult, sslGetVerifyResult, "SSL_get_verify_result");
    DLSYM(sslHandle, FuncCheckPrivateKey, checkPrivateKey, "SSL_CTX_check_private_key");
    DLSYM(sslHandle, FuncLoadVerifyLocations, loadVerifyLocations, "SSL_CTX_load_verify_locations");
    DLSYM(sslHandle, FuncSslCtxSetVerify, sslCtxSetVerify, "SSL_CTX_set_verify");
    DLSYM(sslHandle, FuncOpensslCleanup, opensslCleanup, "OPENSSL_cleanup");
    DLSYM(sslHandle, FuncSetCipherSuites, setCipherSuites, "SSL_CTX_set_ciphersuites");
    DLSYM(sslHandle, FuncSetDefaultPasswdCbUserdata, setDefaultPasswdCbUserdata,
          "SSL_CTX_set_default_passwd_cb_userdata");
    DLSYM(sslHandle, FuncUsePrivKeyFile, usePrivKeyFile, "SSL_CTX_use_PrivateKey_file");
    DLSYM(sslHandle, FuncGetMethod, tlsServerMethod, "TLS_server_method");

    return 0;
}

int DlOpensslApi::LoadCryptoSymbols(void *cryptoHandle)
{
    DLSYM(cryptoHandle, FuncX509StoreCtxGetError, x509StoreCtxGetError, "X509_STORE_CTX_get_error");
    DLSYM(cryptoHandle, FuncEvpDecryptUpdate, evpDecryptUpdate, "EVP_DecryptUpdate");
    DLSYM(cryptoHandle, FuncX509CmpCurrentTime, x509CmpCurrentTime, "X509_cmp_current_time");
    DLSYM(cryptoHandle, FuncRandBytes, randBytes, "RAND_bytes");
    DLSYM(cryptoHandle, FuncX509Free, x509Free, "X509_free");
    DLSYM(cryptoHandle, FuncEvpAesCipher, evpAes128Gcm, "EVP_aes_128_gcm");
    DLSYM(cryptoHandle, FuncBioFree, bioFree, "BIO_free");
    DLSYM(cryptoHandle, FuncEvpEncryptFinalEx, evpEncryptFinalEx, "EVP_EncryptFinal_ex");
    DLSYM(cryptoHandle, FuncX509StoreCtxSetFlags, x509StoreCtxSetFlags, "X509_STORE_CTX_set_flags");
    DLSYM(cryptoHandle, FuncEvpCipherCtxNew, evpCipherCtxNew, "EVP_CIPHER_CTX_new");
    DLSYM(cryptoHandle, FuncX509GetPubkey, x509GetPubkey, "X509_get_pubkey");
    DLSYM(cryptoHandle, FuncRandPoll, randPoll, "RAND_poll");
    DLSYM(cryptoHandle, FuncPemReadBioX509Crl, pemReadBioX509Crl, "PEM_read_bio_X509_CRL");
    DLSYM(cryptoHandle, FuncEvpCipherCtxFree, evpCipherCtxFree, "EVP_CIPHER_CTX_free");
    DLSYM(cryptoHandle, FuncX509StoreAddCrl, x509StoreAddCrl, "X509_STORE_add_crl");
    DLSYM(cryptoHandle, FuncRandSeed, randSeed, "RAND_seed");
    DLSYM(cryptoHandle, FuncEvpAesCipher, evpAes256Gcm, "EVP_aes_256_gcm");
    DLSYM(cryptoHandle, FuncX509CrlGet0ByCert, x509CrlGet0ByCert, "X509_CRL_get0_by_cert");
    DLSYM(cryptoHandle, FuncBioNew, bioNew, "BIO_new");
    DLSYM(cryptoHandle, FuncEvpCipherCtxCtrl, evpCipherCtxCtrl, "EVP_CIPHER_CTX_ctrl");
    DLSYM(cryptoHandle, FuncEvpDecryptInitEx, evpDecryptInitEx, "EVP_DecryptInit_ex");
    DLSYM(cryptoHandle, FuncRandBytes, randPrivBytes, "RAND_priv_bytes");
    DLSYM(cryptoHandle, FuncX509VerifyCert, x509VerifyCert, "X509_verify_cert");
    DLSYM(cryptoHandle, FuncEvpEncryptUpdate, evpEncryptUpdate, "EVP_EncryptUpdate");
    DLSYM(cryptoHandle, FuncAsn1Time2Tm, asn1Time2Tm, "ASN1_TIME_to_tm");
    DLSYM(cryptoHandle, FuncX509GetNotAfter, x509GetNotAfter, "X509_getm_notAfter");
    DLSYM(cryptoHandle, FuncX509CrlGet0NextUpdate, x509CrlGet0NextUpdate, "X509_CRL_get0_nextUpdate");
    DLSYM(cryptoHandle, FuncEvpEncryptInitEx, evpEncryptInitEx, "EVP_EncryptInit_ex");
    DLSYM(cryptoHandle, FuncBioSFile, bioSFile, "BIO_s_file");
    DLSYM(cryptoHandle, FuncEvpDecryptFinalEx, evpDecryptFinalEx, "EVP_DecryptFinal_ex");
    DLSYM(cryptoHandle, FuncX509CrlFree, x509CrlFree, "X509_CRL_free");
    DLSYM(cryptoHandle, FuncBioCtrl, bioCtrl, "BIO_ctrl");
    DLSYM(cryptoHandle, FuncX509StoreCtxGet0Store, x509StoreCtxGet0Store, "X509_STORE_CTX_get0_store");
    DLSYM(cryptoHandle, FuncRandStatus, randStatus, "RAND_status");
    DLSYM(cryptoHandle, FuncX509VerifyCertErrorString, x509VerifyCertErrorString, "X509_verify_cert_error_string");
    DLSYM(cryptoHandle, FuncPemReadX509, pemReadX509, "PEM_read_X509");
    DLSYM(cryptoHandle, FuncX509GetNotBefore, x509GetNotBefore, "X509_getm_notBefore");
    DLSYM(cryptoHandle, FuncEvpPkeyFree, evpPkeyFree, "EVP_PKEY_free");

    return 0;
}

int DlOpensslApi::GetLibPath(std::string dir, std::string &sslPath, std::string &cryptoPath)
{
    if (dir.empty()) {
        sslPath = gOpensslLibSslName;
        cryptoPath = gOpensslLibCryptoName;
        return 0;
    }

    if (dir.back() != '/') {
        dir.push_back('/');
    }

    sslPath = dir + gOpensslLibSslName;
    if (::access(sslPath.c_str(), F_OK) != 0) {
        LOG_ERROR("libssl.so path is invalid");
        return -1;
    }

    cryptoPath = dir + gOpensslLibCryptoName;
    if (::access(cryptoPath.c_str(), F_OK) != 0) {
        LOG_ERROR("libcrypto.so path is invalid");
        return -1;
    }

    return 0;
}

int DlOpensslApi::LoadOpensslApiDl(const std::string &libPath)
{
    LOG_INFO("Starting to load openssl api");
    if (gLoaded) {
        return 0;
    }

    std::string sslPath;
    std::string cryptoPath;
    if (GetLibPath(libPath, sslPath, cryptoPath) != 0) {
        return -1;
    }

    auto cryptoHandle = dlopen(cryptoPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (cryptoHandle == nullptr) {
        LOG_ERROR("Failed to dlopen libcrypto.so err: " << dlerror());
        return -1;
    }

    if (LoadCryptoSymbols(cryptoHandle) == -1) {
        LOG_ERROR("Failed to dlopen libcrypto.so err: " << dlerror());
        dlclose(cryptoHandle);
        return -1;
    }

    auto sslHandle = dlopen(sslPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (sslHandle == nullptr) {
        LOG_ERROR("Failed to dlopen libssl.so err: " << dlerror());
        dlclose(cryptoHandle);
        return -1;
    }

    if (LoadSSLSymbols(sslHandle) == -1) {
        LOG_ERROR("Failed to dlopen libssl.so err: " << dlerror());
        dlclose(cryptoHandle);
        dlclose(sslHandle);
        return -1;
    }

    gLoaded = true;
    return 0;
}
} // namespace bio
} // namespace ock