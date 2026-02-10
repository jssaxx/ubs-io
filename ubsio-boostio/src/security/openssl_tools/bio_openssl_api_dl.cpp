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
    int ret = 0;
    ret |= LoadSym(handle, "X509_STORE_CTX_get_error", reinterpret_cast<void**>(&x509StoreCtxGetError));
    ret |= LoadSym(handle, "EVP_DecryptUpdate", reinterpret_cast<void**>(&evpDecryptUpdate));
    ret |= LoadSym(handle, "X509_cmp_current_time", reinterpret_cast<void**>(&x509CmpCurrentTime));
    ret |= LoadSym(handle, "RAND_bytes", reinterpret_cast<void**>(&randBytes));
    ret |= LoadSym(handle, "X509_free", reinterpret_cast<void**>(&x509Free));
    ret |= LoadSym(handle, "EVP_aes_128_gcm", reinterpret_cast<void**>(&evpAes128Gcm));
    ret |= LoadSym(handle, "BIO_free", reinterpret_cast<void**>(&bioFree));
    ret |= LoadSym(handle, "EVP_EncryptFinal_ex", reinterpret_cast<void**>(&evpEncryptFinalEx));
    ret |= LoadSym(handle, "X509_STORE_CTX_set_flags", reinterpret_cast<void**>(&x509StoreCtxSetFlags));
    ret |= LoadSym(handle, "EVP_CIPHER_CTX_new", reinterpret_cast<void**>(&evpCipherCtxNew));
    ret |= LoadSym(handle, "X509_get_pubkey", reinterpret_cast<void**>(&x509GetPubkey));
    ret |= LoadSym(handle, "RAND_poll", reinterpret_cast<void**>(&randPoll));
    ret |= LoadSym(handle, "PEM_read_bio_X509_CRL", reinterpret_cast<void**>(&pemReadBioX509Crl));
    ret |= LoadSym(handle, "EVP_CIPHER_CTX_free", reinterpret_cast<void**>(&evpCipherCtxFree));
    ret |= LoadSym(handle, "X509_STORE_add_crl", reinterpret_cast<void**>(&x509StoreAddCrl));
    ret |= LoadSym(handle, "RAND_seed", reinterpret_cast<void**>(&randSeed));
    ret |= LoadSym(handle, "EVP_aes_256_gcm", reinterpret_cast<void**>(&evpAes256Gcm));
    ret |= LoadSym(handle, "X509_CRL_get0_by_cert", reinterpret_cast<void**>(&x509CrlGet0ByCert));
    ret |= LoadSym(handle, "BIO_new", reinterpret_cast<void**>(&bioNew));
    ret |= LoadSym(handle, "EVP_CIPHER_CTX_ctrl", reinterpret_cast<void**>(&evpCipherCtxCtrl));
    ret |= LoadSym(handle, "EVP_DecryptInit_ex", reinterpret_cast<void**>(&evpDecryptInitEx));
    ret |= LoadSym(handle, "RAND_priv_bytes", reinterpret_cast<void**>(&randPrivBytes));
    ret |= LoadSym(handle, "X509_verify_cert", reinterpret_cast<void**>(&x509VerifyCert));
    ret |= LoadSym(handle, "EVP_EncryptUpdate", reinterpret_cast<void**>(&evpEncryptUpdate));
    ret |= LoadSym(handle, "ASN1_TIME_to_tm", reinterpret_cast<void**>(&asn1Time2Tm));
    ret |= LoadSym(handle, "X509_getm_notAfter", reinterpret_cast<void**>(&x509GetNotAfter));
    ret |= LoadSym(handle, "X509_CRL_get0_nextUpdate", reinterpret_cast<void**>(&x509CrlGet0NextUpdate));
    ret |= LoadSym(handle, "EVP_EncryptInit_ex", reinterpret_cast<void**>(&evpEncryptInitEx));
    ret |= LoadSym(handle, "BIO_s_file", reinterpret_cast<void**>(&bioSFile));
    ret |= LoadSym(handle, "EVP_DecryptFinal_ex", reinterpret_cast<void**>(&evpDecryptFinalEx));
    ret |= LoadSym(handle, "X509_CRL_free", reinterpret_cast<void**>(&x509CrlFree));
    ret |= LoadSym(handle, "BIO_ctrl", reinterpret_cast<void**>(&bioCtrl));
    ret |= LoadSym(handle, "X509_STORE_CTX_get0_store", reinterpret_cast<void**>(&x509StoreCtxGet0Store));
    ret |= LoadSym(handle, "RAND_status", reinterpret_cast<void**>(&randStatus));
    ret |= LoadSym(handle, "X509_verify_cert_error_string", reinterpret_cast<void**>(&x509VerifyCertErrorString));
    ret |= LoadSym(handle, "PEM_read_X509", reinterpret_cast<void**>(&pemReadX509));
    ret |= LoadSym(handle, "X509_getm_notBefore", reinterpret_cast<void**>(&x509GetNotBefore));
    ret |= LoadSym(handle, "EVP_PKEY_free", reinterpret_cast<void**>(&evpPkeyFree));

    return ret;
}

int DlOpensslApi::LoadSSLMethod(void *handle)
{
    int ret = 0;
    ret |= LoadSym(handle, "SSL_new", reinterpret_cast<void**>(&sslNew));
    ret |= LoadSym(handle, "SSL_CTX_free", reinterpret_cast<void**>(&sslCtxFree));
    ret |= LoadSym(handle, "SSL_get_error", reinterpret_cast<void**>(&sslGetError));
    ret |= LoadSym(handle, "SSL_write", reinterpret_cast<void**>(&sslWrite));
    ret |= LoadSym(handle, "SSL_shutdown", reinterpret_cast<void**>(&sslShutdown));
    ret |= LoadSym(handle, "SSL_CTX_set_cert_verify_callback", reinterpret_cast<void**>(&setCertVerifyCallback));
    ret |= LoadSym(handle, "OPENSSL_init_crypto", reinterpret_cast<void**>(&initCrypto));
    ret |= LoadSym(handle, "SSL_read", reinterpret_cast<void**>(&sslRead));
    ret |= LoadSym(handle, "SSL_get_current_cipher", reinterpret_cast<void**>(&sslGetCurrentCipher));
    ret |= LoadSym(handle, "SSL_get_version", reinterpret_cast<void**>(&sslGetVersion));
    ret |= LoadSym(handle, "SSL_free", reinterpret_cast<void**>(&sslFree));
    ret |= LoadSym(handle, "SSL_accept", reinterpret_cast<void**>(&sslAccept));
    ret |= LoadSym(handle, "SSL_CTX_ctrl", reinterpret_cast<void**>(&sslCtxCtrl));
    ret |= LoadSym(handle, "SSL_CTX_new", reinterpret_cast<void**>(&sslCtxNew));
    ret |= LoadSym(handle, "SSL_connect", reinterpret_cast<void**>(&sslConnect));
    ret |= LoadSymWithFallback(handle, "SSL_get_peer_certificate", "SSL_get1_peer_certificate",
        reinterpret_cast<void**>(&sslGetPeerCertificate));
    ret |= LoadSym(handle, "TLS_client_method", reinterpret_cast<void**>(&tlsClientMethod));
    ret |= LoadSym(handle, "OPENSSL_init_ssl", reinterpret_cast<void**>(&initSsl));
    ret |= LoadSym(handle, "SSL_set_fd", reinterpret_cast<void**>(&sslSetFd));
    ret |= LoadSym(handle, "SSL_get_verify_result", reinterpret_cast<void**>(&sslGetVerifyResult));
    ret |= LoadSym(handle, "SSL_CTX_check_private_key", reinterpret_cast<void**>(&checkPrivateKey));
    ret |= LoadSym(handle, "SSL_CTX_load_verify_locations", reinterpret_cast<void**>(&loadVerifyLocations));
    ret |= LoadSym(handle, "SSL_CTX_set_verify", reinterpret_cast<void**>(&sslCtxSetVerify));
    ret |= LoadSym(handle, "OPENSSL_cleanup", reinterpret_cast<void**>(&opensslCleanup));
    ret |= LoadSym(handle, "SSL_CTX_set_ciphersuites", reinterpret_cast<void**>(&setCipherSuites));
    ret |= LoadSym(handle, "SSL_CTX_set_default_passwd_cb_userdata", reinterpret_cast<void**>(&setDefaultPasswdCbUserdata));
    ret |= LoadSym(handle, "SSL_CTX_use_PrivateKey_file", reinterpret_cast<void**>(&usePrivKeyFile));
    ret |= LoadSym(handle, "TLS_server_method", reinterpret_cast<void**>(&tlsServerMethod));

    return ret;
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
