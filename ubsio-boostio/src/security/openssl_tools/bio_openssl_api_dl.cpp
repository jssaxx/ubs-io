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

#define CHECK_LOAD_SYM(handle, symName, targetPtr) \
    do { \
        if (LoadSym(handle, symName, reinterpret_cast<void**>(&targetPtr)) != 0) { \
            LOG_ERROR("Critical symbol missing: " << symName); \
            return -1; \
        } \
    } while (0)

#define CHECK_LOAD_SYM_FALLBACK(handle, sym1, sym2, targetPtr) \
    do { \
        if (LoadSymWithFallback(handle, sym1, sym2, reinterpret_cast<void**>(&targetPtr)) != 0) { \
            LOG_ERROR("Critical symbols missing: " << sym1 << " and " << sym2); \
            return -1; \
        } \
    } while (0)

namespace ock {
namespace bio {

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

static inline int LoadSym(void *handle, const char *sym, void **ptr)
{
    *ptr = dlsym(handle, sym);
    if (*ptr == nullptr) {
        LOG_ERROR("Failed to load symbol: " << sym);
        return -1;
    }
    return 0;
}

static inline int LoadSymWithFallback(void *handle, const char *sym1, const char *sym2, void **ptr)
{
    *ptr = dlsym(handle, sym1);
    if (*ptr == nullptr) {
        *ptr = dlsym(handle, sym2);
        if (*ptr == nullptr) {
            LOG_ERROR("Failed to load symbol: " << sym1 << " or " << sym2);
            return -1;
        }
    }
    return 0;
}

int DlOpensslApi::LoadCryptoMethod(void *handle)
{
    LOAD_SYM_OR_RETURN(handle, "EVP_CIPHER_CTX_new", evpCipherCtxNew);
    LOAD_SYM_OR_RETURN(handle, "EVP_CIPHER_CTX_free", evpCipherCtxFree);
    LOAD_SYM_OR_RETURN(handle, "EVP_CIPHER_CTX_ctrl", evpCipherCtxCtrl);
    LOAD_SYM_OR_RETURN(handle, "EVP_EncryptInit_ex", evpEncryptInitEx);
    LOAD_SYM_OR_RETURN(handle, "EVP_EncryptUpdate", evpEncryptUpdate);
    LOAD_SYM_OR_RETURN(handle, "EVP_EncryptFinal_ex", evpEncryptFinalEx);
    LOAD_SYM_OR_RETURN(handle, "EVP_DecryptInit_ex", evpDecryptInitEx);
    LOAD_SYM_OR_RETURN(handle, "EVP_DecryptUpdate", evpDecryptUpdate);
    LOAD_SYM_OR_RETURN(handle, "EVP_DecryptFinal_ex", evpDecryptFinalEx);
    LOAD_SYM_OR_RETURN(handle, "EVP_aes_128_gcm", evpAes128Gcm);
    LOAD_SYM_OR_RETURN(handle, "EVP_aes_256_gcm", evpAes256Gcm);
    LOAD_SYM_OR_RETURN(handle, "EVP_PKEY_free", evpPkeyFree);
    LOAD_SYM_OR_RETURN(handle, "RAND_poll", randPoll);
    LOAD_SYM_OR_RETURN(handle, "RAND_status", randStatus);
    LOAD_SYM_OR_RETURN(handle, "RAND_bytes", randBytes);
    LOAD_SYM_OR_RETURN(handle, "RAND_priv_bytes", randPrivBytes);
    LOAD_SYM_OR_RETURN(handle, "RAND_seed", randSeed);
    LOAD_SYM_OR_RETURN(handle, "X509_verify_cert", x509VerifyCert);
    LOAD_SYM_OR_RETURN(handle, "X509_verify_cert_error_string", x509VerifyCertErrorString);
    LOAD_SYM_OR_RETURN(handle, "X509_STORE_CTX_get_error", x509StoreCtxGetError);
    LOAD_SYM_OR_RETURN(handle, "X509_STORE_CTX_get0_store", x509StoreCtxGet0Store);
    LOAD_SYM_OR_RETURN(handle, "X509_STORE_CTX_set_flags", x509StoreCtxSetFlags);
    LOAD_SYM_OR_RETURN(handle, "X509_STORE_add_crl", x509StoreAddCrl);
    LOAD_SYM_OR_RETURN(handle, "X509_CRL_free", x509CrlFree);
    LOAD_SYM_OR_RETURN(handle, "X509_cmp_current_time", x509CmpCurrentTime);
    LOAD_SYM_OR_RETURN(handle, "X509_CRL_get0_by_cert", x509CrlGet0ByCert);
    LOAD_SYM_OR_RETURN(handle, "X509_CRL_get0_nextUpdate", x509CrlGet0NextUpdate);
    LOAD_SYM_OR_RETURN(handle, "X509_getm_notAfter", x509GetNotAfter);
    LOAD_SYM_OR_RETURN(handle, "X509_getm_notBefore", x509GetNotBefore);
    LOAD_SYM_OR_RETURN(handle, "X509_get_pubkey", x509GetPubkey);
    LOAD_SYM_OR_RETURN(handle, "X509_free", x509Free);
    LOAD_SYM_OR_RETURN(handle, "ASN1_TIME_to_tm", asn1Time2Tm);
    LOAD_SYM_OR_RETURN(handle, "BIO_s_file", bioSFile);
    LOAD_SYM_OR_RETURN(handle, "BIO_new", bioNew);
    LOAD_SYM_OR_RETURN(handle, "BIO_free", bioFree);
    LOAD_SYM_OR_RETURN(handle, "BIO_ctrl", bioCtrl);
    LOAD_SYM_OR_RETURN(handle, "PEM_read_bio_X509_CRL", pemReadBioX509Crl);
    LOAD_SYM_OR_RETURN(handle, "PEM_read_X509", pemReadX509);

    return 0;
}

int DlOpensslApi::LoadSSLMethod(void *handle)
{
    LOAD_SYM_OR_RETURN(handle, "OPENSSL_init_ssl", initSsl);
    LOAD_SYM_OR_RETURN(handle, "OPENSSL_init_crypto", initCrypto);
    LOAD_SYM_OR_RETURN(handle, "OPENSSL_cleanup", opensslCleanup);
    LOAD_SYM_OR_RETURN(handle, "TLS_server_method", tlsServerMethod);
    LOAD_SYM_OR_RETURN(handle, "TLS_client_method", tlsClientMethod);
    LOAD_SYM_OR_RETURN(handle, "SSL_CTX_new", sslCtxNew);
    LOAD_SYM_OR_RETURN(handle, "SSL_CTX_free", sslCtxFree);
    LOAD_SYM_OR_RETURN(handle, "SSL_CTX_ctrl", sslCtxCtrl);
    LOAD_SYM_OR_RETURN(handle, "SSL_CTX_set_ciphersuites", setCipherSuites);
    LOAD_SYM_OR_RETURN(handle, "SSL_CTX_use_PrivateKey_file", usePrivKeyFile);
    LOAD_SYM_OR_RETURN(handle, "SSL_CTX_set_verify", sslCtxSetVerify);
    LOAD_SYM_OR_RETURN(handle, "SSL_CTX_set_default_passwd_cb_userdata", setDefaultPasswdCbUserdata);
    LOAD_SYM_OR_RETURN(handle, "SSL_CTX_set_cert_verify_callback", setCertVerifyCallback);
    LOAD_SYM_OR_RETURN(handle, "SSL_CTX_load_verify_locations", loadVerifyLocations);
    LOAD_SYM_OR_RETURN(handle, "SSL_CTX_check_private_key", checkPrivateKey);
    LOAD_SYM_OR_RETURN(handle, "SSL_new", sslNew);
    LOAD_SYM_OR_RETURN(handle, "SSL_free", sslFree);
    LOAD_SYM_OR_RETURN(handle, "SSL_set_fd", sslSetFd);
    LOAD_SYM_OR_RETURN(handle, "SSL_shutdown", sslShutdown);
    LOAD_SYM_OR_RETURN(handle, "SSL_connect", sslConnect);
    LOAD_SYM_OR_RETURN(handle, "SSL_accept", sslAccept);
    LOAD_SYM_OR_RETURN(handle, "SSL_read", sslRead);
    LOAD_SYM_OR_RETURN(handle, "SSL_write", sslWrite);
    LOAD_SYM_OR_RETURN(handle, "SSL_get_error", sslGetError);
    LOAD_SYM_OR_RETURN(handle, "SSL_get_version", sslGetVersion);
    LOAD_SYM_OR_RETURN(handle, "SSL_get_current_cipher", sslGetCurrentCipher);
    LOAD_SYM_OR_RETURN(handle, "SSL_get_verify_result", sslGetVerifyResult);
    LOAD_SYM_FALLBACK_OR_RETURN(handle, "SSL_get_peer_certificate", "SSL_get1_peer_certificate", sslGetPeerCertificate);

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
