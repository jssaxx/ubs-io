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

#include <dlfcn.h>
#include <unistd.h>
#include "mms_log.h"
#include "mms_file_util.h"
#include "mms_openssl_api_dl.h"

#define DLSYM_RETURN(handle, sym, type, ptr)              \
    do {                                                  \
        auto ptr1 = dlsym((handle), (sym));               \
        if (ptr1 == nullptr) {                            \
            LOG_ERROR("Failed to load " << (sym));        \
            return -1;                                    \
        }                                                 \
        (ptr) = (type)ptr1;                               \
    } while (0)

#define DLSYM_UPDATE(handle, sym1, sym2, type, ptr)        \
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
namespace mms {
MethodSslWrite DlOpensslApi::sslWrite = nullptr;
MethodX509StoreCtxGetError DlOpensslApi::x509StoreCtxGetError = nullptr;
MethodSslOperation DlOpensslApi::sslShutdown = nullptr;
MethodRandPoll DlOpensslApi::randPoll = nullptr;
MethodSslGetVersion DlOpensslApi::sslGetVersion = nullptr;
MethodEvpEncryptInitEx DlOpensslApi::evpEncryptInitEx = nullptr;
MethodInit DlOpensslApi::initSsl = nullptr;
MethodX509CrlGet0NextUpdate DlOpensslApi::x509CrlGet0NextUpdate = nullptr;
MethodSslCtxFree DlOpensslApi::sslCtxFree = nullptr;
MethodEvpCipherCtxNew DlOpensslApi::evpCipherCtxNew = nullptr;
MethodRandStatus DlOpensslApi::randStatus = nullptr;
MethodSslOperation DlOpensslApi::sslAccept = nullptr;
MethodSetCertVerifyCallback DlOpensslApi::setCertVerifyCallback = nullptr;
MethodX509GetPubkey DlOpensslApi::x509GetPubkey = nullptr;
MethodSslFree DlOpensslApi::sslFree = nullptr;
MethodX509VerifyCert DlOpensslApi::x509VerifyCert = nullptr;
MethodBioFree DlOpensslApi::bioFree = nullptr;
MethodEvpDecryptUpdate DlOpensslApi::evpDecryptUpdate = nullptr;
MethodSetCipherSuites DlOpensslApi::setCipherSuites = nullptr;
MethodRandBytes DlOpensslApi::randPrivBytes = nullptr;
MethodSslCtxNew DlOpensslApi::sslCtxNew = nullptr;
MethodCheckPrivateKey DlOpensslApi::checkPrivateKey = nullptr;
MethodX509CrlFree DlOpensslApi::x509CrlFree = nullptr;
MethodSslGetError DlOpensslApi::sslGetError = nullptr;
MethodSslCtxSetVerify DlOpensslApi::sslCtxSetVerify = nullptr;
MethodX509StoreCtxSetFlags DlOpensslApi::x509StoreCtxSetFlags = nullptr;
MethodEvpDecryptFinalEx DlOpensslApi::evpDecryptFinalEx = nullptr;
MethodRandBytes DlOpensslApi::randBytes = nullptr;
MethodX509GetNotAfter DlOpensslApi::x509GetNotAfter = nullptr;
MethodEvpAesCipher DlOpensslApi::evpAes128Gcm = nullptr;
MethodEvpPkeyFree DlOpensslApi::evpPkeyFree = nullptr;
MethodPemReadBioX509Crl DlOpensslApi::pemReadBioX509Crl = nullptr;
MethodX509CmpCurrentTime DlOpensslApi::x509CmpCurrentTime = nullptr;
MethodEvpCipherCtxCtrl DlOpensslApi::evpCipherCtxCtrl = nullptr;
MethodSslNew DlOpensslApi::sslNew = nullptr;
MethodSslFd DlOpensslApi::sslSetFd = nullptr;
MethodEvpEncryptUpdate DlOpensslApi::evpEncryptUpdate = nullptr;
MethodX509StoreCtxGet0Store DlOpensslApi::x509StoreCtxGet0Store = nullptr;
MethodEvpEncryptFinalEx DlOpensslApi::evpEncryptFinalEx = nullptr;
MethodX509CrlGet0ByCert DlOpensslApi::x509CrlGet0ByCert = nullptr;
MethodEvpCipherCtxFree DlOpensslApi::evpCipherCtxFree = nullptr;
MethodX509GetNotBefore DlOpensslApi::x509GetNotBefore = nullptr;
MethodSslGetPeerCertificate DlOpensslApi::sslGetPeerCertificate = nullptr;
MethodBioSFile DlOpensslApi::bioSFile = nullptr;
MethodRandSeed DlOpensslApi::randSeed = nullptr;
MethodEvpDecryptInitEx DlOpensslApi::evpDecryptInitEx = nullptr;
MethodSslGetVerifyResult DlOpensslApi::sslGetVerifyResult = nullptr;
MethodAsn1Time2Tm DlOpensslApi::asn1Time2Tm = nullptr;
MethodSetDefaultPasswdCbUserdata DlOpensslApi::setDefaultPasswdCbUserdata = nullptr;
MethodX509Free DlOpensslApi::x509Free = nullptr;
MethodX509VerifyCertErrorString DlOpensslApi::x509VerifyCertErrorString = nullptr;
MethodUsePrivKeyFile DlOpensslApi::usePrivKeyFile = nullptr;
MethodSslGetCurrentCipher DlOpensslApi::sslGetCurrentCipher = nullptr;
MethodSslRead DlOpensslApi::sslRead = nullptr;
MethodBioNew DlOpensslApi::bioNew = nullptr;
MethodOpensslCleanup DlOpensslApi::opensslCleanup = nullptr;
MethodSslOperation DlOpensslApi::sslConnect = nullptr;
MethodSslCtxCtrl DlOpensslApi::sslCtxCtrl = nullptr;
MethodX509StoreAddCrl DlOpensslApi::x509StoreAddCrl = nullptr;
MethodGetMethod DlOpensslApi::tlsServerMethod = nullptr;
MethodEvpAesCipher DlOpensslApi::evpAes256Gcm = nullptr;
MethodInit DlOpensslApi::initCrypto = nullptr;
MethodBioCtrl DlOpensslApi::bioCtrl = nullptr;
MethodPemReadX509 DlOpensslApi::pemReadX509 = nullptr;
MethodLoadVerifyLocations DlOpensslApi::loadVerifyLocations = nullptr;
MethodGetMethod DlOpensslApi::tlsClientMethod = nullptr;

bool DlOpensslApi::gStarted = false;
const char *DlOpensslApi::gOpensslLibSslName = "libssl.so";
const char *DlOpensslApi::gOpensslLibCryptoName = "libcrypto.so";
const char *DlOpensslApi::gSep = "/";

int DlOpensslApi::LoadCryptoMethod(void *handle)
{
    DLSYM_RETURN(handle, "X509_STORE_CTX_get_error", MethodX509StoreCtxGetError, x509StoreCtxGetError);
    DLSYM_RETURN(handle, "EVP_DecryptUpdate", MethodEvpDecryptUpdate, evpDecryptUpdate);
    DLSYM_RETURN(handle, "X509_cmp_current_time", MethodX509CmpCurrentTime, x509CmpCurrentTime);
    DLSYM_RETURN(handle, "RAND_bytes", MethodRandBytes, randBytes);
    DLSYM_RETURN(handle, "X509_free", MethodX509Free, x509Free);
    DLSYM_RETURN(handle, "EVP_aes_128_gcm", MethodEvpAesCipher, evpAes128Gcm);
    DLSYM_RETURN(handle, "BIO_free", MethodBioFree, bioFree);
    DLSYM_RETURN(handle, "EVP_EncryptFinal_ex", MethodEvpEncryptFinalEx, evpEncryptFinalEx);
    DLSYM_RETURN(handle, "X509_STORE_CTX_set_flags", MethodX509StoreCtxSetFlags, x509StoreCtxSetFlags);
    DLSYM_RETURN(handle, "EVP_CIPHER_CTX_new", MethodEvpCipherCtxNew, evpCipherCtxNew);
    DLSYM_RETURN(handle, "X509_get_pubkey", MethodX509GetPubkey, x509GetPubkey);
    DLSYM_RETURN(handle, "RAND_poll", MethodRandPoll, randPoll);
    DLSYM_RETURN(handle, "PEM_read_bio_X509_CRL", MethodPemReadBioX509Crl, pemReadBioX509Crl);
    DLSYM_RETURN(handle, "EVP_CIPHER_CTX_free", MethodEvpCipherCtxFree, evpCipherCtxFree);
    DLSYM_RETURN(handle, "X509_STORE_add_crl", MethodX509StoreAddCrl, x509StoreAddCrl);
    DLSYM_RETURN(handle, "RAND_seed", MethodRandSeed, randSeed);
    DLSYM_RETURN(handle, "EVP_aes_256_gcm", MethodEvpAesCipher, evpAes256Gcm);
    DLSYM_RETURN(handle, "X509_CRL_get0_by_cert", MethodX509CrlGet0ByCert, x509CrlGet0ByCert);
    DLSYM_RETURN(handle, "BIO_new", MethodBioNew, bioNew);
    DLSYM_RETURN(handle, "EVP_CIPHER_CTX_ctrl", MethodEvpCipherCtxCtrl, evpCipherCtxCtrl);
    DLSYM_RETURN(handle, "EVP_DecryptInit_ex", MethodEvpDecryptInitEx, evpDecryptInitEx);
    DLSYM_RETURN(handle, "RAND_priv_bytes", MethodRandBytes, randPrivBytes);
    DLSYM_RETURN(handle, "X509_verify_cert", MethodX509VerifyCert, x509VerifyCert);
    DLSYM_RETURN(handle, "EVP_EncryptUpdate", MethodEvpEncryptUpdate, evpEncryptUpdate);
    DLSYM_RETURN(handle, "ASN1_TIME_to_tm", MethodAsn1Time2Tm, asn1Time2Tm);
    DLSYM_RETURN(handle, "X509_getm_notAfter", MethodX509GetNotAfter, x509GetNotAfter);
    DLSYM_RETURN(handle, "X509_CRL_get0_nextUpdate", MethodX509CrlGet0NextUpdate, x509CrlGet0NextUpdate);
    DLSYM_RETURN(handle, "EVP_EncryptInit_ex", MethodEvpEncryptInitEx, evpEncryptInitEx);
    DLSYM_RETURN(handle, "BIO_s_file", MethodBioSFile, bioSFile);
    DLSYM_RETURN(handle, "EVP_DecryptFinal_ex", MethodEvpDecryptFinalEx, evpDecryptFinalEx);
    DLSYM_RETURN(handle, "X509_CRL_free", MethodX509CrlFree, x509CrlFree);
    DLSYM_RETURN(handle, "BIO_ctrl", MethodBioCtrl, bioCtrl);
    DLSYM_RETURN(handle, "X509_STORE_CTX_get0_store", MethodX509StoreCtxGet0Store, x509StoreCtxGet0Store);
    DLSYM_RETURN(handle, "RAND_status", MethodRandStatus, randStatus);
    DLSYM_RETURN(handle, "X509_verify_cert_error_string", MethodX509VerifyCertErrorString, x509VerifyCertErrorString);
    DLSYM_RETURN(handle, "PEM_read_X509", MethodPemReadX509, pemReadX509);
    DLSYM_RETURN(handle, "X509_getm_notBefore", MethodX509GetNotBefore, x509GetNotBefore);
    DLSYM_RETURN(handle, "EVP_PKEY_free", MethodEvpPkeyFree, evpPkeyFree);
    return 0;
}

int DlOpensslApi::LoadSSLMethod(void *handle)
{
    DLSYM_RETURN(handle, "SSL_new", MethodSslNew, sslNew);
    DLSYM_RETURN(handle, "SSL_CTX_free", MethodSslCtxFree, sslCtxFree);
    DLSYM_RETURN(handle, "SSL_get_error", MethodSslGetError, sslGetError);
    DLSYM_RETURN(handle, "SSL_write", MethodSslWrite, sslWrite);
    DLSYM_RETURN(handle, "SSL_shutdown", MethodSslOperation, sslShutdown);
    DLSYM_RETURN(handle, "SSL_CTX_set_cert_verify_callback", MethodSetCertVerifyCallback, setCertVerifyCallback);
    DLSYM_RETURN(handle, "OPENSSL_init_crypto", MethodInit, initCrypto);
    DLSYM_RETURN(handle, "SSL_read", MethodSslRead, sslRead);
    DLSYM_RETURN(handle, "SSL_get_current_cipher", MethodSslGetCurrentCipher, sslGetCurrentCipher);
    DLSYM_RETURN(handle, "SSL_get_version", MethodSslGetVersion, sslGetVersion);
    DLSYM_RETURN(handle, "SSL_free", MethodSslFree, sslFree);
    DLSYM_RETURN(handle, "SSL_accept", MethodSslOperation, sslAccept);
    DLSYM_RETURN(handle, "SSL_CTX_ctrl", MethodSslCtxCtrl, sslCtxCtrl);
    DLSYM_RETURN(handle, "SSL_CTX_new", MethodSslCtxNew, sslCtxNew);
    DLSYM_RETURN(handle, "SSL_connect", MethodSslOperation, sslConnect);
    DLSYM_UPDATE(handle, "SSL_get_peer_certificate", "SSL_get1_peer_certificate", MethodSslGetPeerCertificate,
        sslGetPeerCertificate);
    DLSYM_RETURN(handle, "TLS_client_method", MethodGetMethod, tlsClientMethod);
    DLSYM_RETURN(handle, "OPENSSL_init_ssl", MethodInit, initSsl);
    DLSYM_RETURN(handle, "SSL_set_fd", MethodSslFd, sslSetFd);
    DLSYM_RETURN(handle, "SSL_get_verify_result", MethodSslGetVerifyResult, sslGetVerifyResult);
    DLSYM_RETURN(handle, "SSL_CTX_check_private_key", MethodCheckPrivateKey, checkPrivateKey);
    DLSYM_RETURN(handle, "SSL_CTX_load_verify_locations", MethodLoadVerifyLocations, loadVerifyLocations);
    DLSYM_RETURN(handle, "SSL_CTX_set_verify", MethodSslCtxSetVerify, sslCtxSetVerify);
    DLSYM_RETURN(handle, "OPENSSL_cleanup", MethodOpensslCleanup, opensslCleanup);
    DLSYM_RETURN(handle, "SSL_CTX_set_ciphersuites", MethodSetCipherSuites, setCipherSuites);
    DLSYM_RETURN(handle, "SSL_CTX_set_default_passwd_cb_userdata", MethodSetDefaultPasswdCbUserdata,
        setDefaultPasswdCbUserdata);
    DLSYM_RETURN(handle, "SSL_CTX_use_PrivateKey_file", MethodUsePrivKeyFile, usePrivKeyFile);
    DLSYM_RETURN(handle, "TLS_server_method", MethodGetMethod, tlsServerMethod);

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
    if (gStarted) {
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

    if (LoadCryptoMethod(cryptoHandle) == -1) {
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

    if (LoadSSLMethod(sslHandle) == -1) {
        LOG_ERROR("Failed to dlopen libssl.so err: " << dlerror());
        dlclose(cryptoHandle);
        dlclose(sslHandle);
        return -1;
    }

    gStarted = true;
    return 0;
}
} // namespace bio
} // namespace ock
