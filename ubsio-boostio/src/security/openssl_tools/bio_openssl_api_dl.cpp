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

int DlOpensslApi::LoadCryptoMethod(void *handle)
{
    DLSYM_RETURN(handle, MethodX509StoreCtxGetError, x509StoreCtxGetError, "X509_STORE_CTX_get_error");
    DLSYM_RETURN(handle, MethodEvpDecryptUpdate, evpDecryptUpdate, "EVP_DecryptUpdate");
    DLSYM_RETURN(handle, MethodX509CmpCurrentTime, x509CmpCurrentTime, "X509_cmp_current_time");
    DLSYM_RETURN(handle, MethodRandBytes, randBytes, "RAND_bytes");
    DLSYM_RETURN(handle, MethodX509Free, x509Free, "X509_free");
    DLSYM_RETURN(handle, MethodEvpAesCipher, evpAes128Gcm, "EVP_aes_128_gcm");
    DLSYM_RETURN(handle, MethodBioFree, bioFree, "BIO_free");
    DLSYM_RETURN(handle, MethodEvpEncryptFinalEx, evpEncryptFinalEx, "EVP_EncryptFinal_ex");
    DLSYM_RETURN(handle, MethodX509StoreCtxSetFlags, x509StoreCtxSetFlags, "X509_STORE_CTX_set_flags");
    DLSYM_RETURN(handle, MethodEvpCipherCtxNew, evpCipherCtxNew, "EVP_CIPHER_CTX_new");
    DLSYM_RETURN(handle, MethodX509GetPubkey, x509GetPubkey, "X509_get_pubkey");
    DLSYM_RETURN(handle, MethodRandPoll, randPoll, "RAND_poll");
    DLSYM_RETURN(handle, MethodPemReadBioX509Crl, pemReadBioX509Crl, "PEM_read_bio_X509_CRL");
    DLSYM_RETURN(handle, MethodEvpCipherCtxFree, evpCipherCtxFree, "EVP_CIPHER_CTX_free");
    DLSYM_RETURN(handle, MethodX509StoreAddCrl, x509StoreAddCrl, "X509_STORE_add_crl");
    DLSYM_RETURN(handle, MethodRandSeed, randSeed, "RAND_seed");
    DLSYM_RETURN(handle, MethodEvpAesCipher, evpAes256Gcm, "EVP_aes_256_gcm");
    DLSYM_RETURN(handle, MethodX509CrlGet0ByCert, x509CrlGet0ByCert, "X509_CRL_get0_by_cert");
    DLSYM_RETURN(handle, MethodBioNew, bioNew, "BIO_new");
    DLSYM_RETURN(handle, MethodEvpCipherCtxCtrl, evpCipherCtxCtrl, "EVP_CIPHER_CTX_ctrl");
    DLSYM_RETURN(handle, MethodEvpDecryptInitEx, evpDecryptInitEx, "EVP_DecryptInit_ex");
    DLSYM_RETURN(handle, MethodRandBytes, randPrivBytes, "RAND_priv_bytes");
    DLSYM_RETURN(handle, MethodX509VerifyCert, x509VerifyCert, "X509_verify_cert");
    DLSYM_RETURN(handle, MethodEvpEncryptUpdate, evpEncryptUpdate, "EVP_EncryptUpdate");
    DLSYM_RETURN(handle, MethodAsn1Time2Tm, asn1Time2Tm, "ASN1_TIME_to_tm");
    DLSYM_RETURN(handle, MethodX509GetNotAfter, x509GetNotAfter, "X509_getm_notAfter");
    DLSYM_RETURN(handle, MethodX509CrlGet0NextUpdate, x509CrlGet0NextUpdate, "X509_CRL_get0_nextUpdate");
    DLSYM_RETURN(handle, MethodEvpEncryptInitEx, evpEncryptInitEx, "EVP_EncryptInit_ex");
    DLSYM_RETURN(handle, MethodBioSFile, bioSFile, "BIO_s_file");
    DLSYM_RETURN(handle, MethodEvpDecryptFinalEx, evpDecryptFinalEx, "EVP_DecryptFinal_ex");
    DLSYM_RETURN(handle, MethodX509CrlFree, x509CrlFree, "X509_CRL_free");
    DLSYM_RETURN(handle, MethodBioCtrl, bioCtrl, "BIO_ctrl");
    DLSYM_RETURN(handle, MethodX509StoreCtxGet0Store, x509StoreCtxGet0Store, "X509_STORE_CTX_get0_store");
    DLSYM_RETURN(handle, MethodRandStatus, randStatus, "RAND_status");
    DLSYM_RETURN(handle, MethodX509VerifyCertErrorString, x509VerifyCertErrorString, "X509_verify_cert_error_string");
    DLSYM_RETURN(handle, MethodPemReadX509, pemReadX509, "PEM_read_X509");
    DLSYM_RETURN(handle, MethodX509GetNotBefore, x509GetNotBefore, "X509_getm_notBefore");
    DLSYM_RETURN(handle, MethodEvpPkeyFree, evpPkeyFree, "EVP_PKEY_free");
    return 0;
}

int DlOpensslApi::LoadSSLMethod(void *handle)
{
    DLSYM_RETURN(handle, MethodSslNew, sslNew, "SSL_new");
    DLSYM_RETURN(handle, MethodSslCtxFree, sslCtxFree, "SSL_CTX_free");
    DLSYM_RETURN(handle, MethodSslGetError, sslGetError, "SSL_get_error");
    DLSYM_RETURN(handle, MethodSslWrite, sslWrite, "SSL_write");
    DLSYM_RETURN(handle, MethodSslOperation, sslShutdown, "SSL_shutdown");
    DLSYM_RETURN(handle, MethodSetCertVerifyCallback, setCertVerifyCallback, "SSL_CTX_set_cert_verify_callback");
    DLSYM_RETURN(handle, MethodInit, initCrypto, "OPENSSL_init_crypto");
    DLSYM_RETURN(handle, MethodSslRead, sslRead, "SSL_read");
    DLSYM_RETURN(handle, MethodSslGetCurrentCipher, sslGetCurrentCipher, "SSL_get_current_cipher");
    DLSYM_RETURN(handle, MethodSslGetVersion, sslGetVersion, "SSL_get_version");
    DLSYM_RETURN(handle, MethodSslFree, sslFree, "SSL_free");
    DLSYM_RETURN(handle, MethodSslOperation, sslAccept, "SSL_accept");
    DLSYM_RETURN(handle, MethodSslCtxCtrl, sslCtxCtrl, "SSL_CTX_ctrl");
    DLSYM_RETURN(handle, MethodSslCtxNew, sslCtxNew, "SSL_CTX_new");
    DLSYM_RETURN(handle, MethodSslOperation, sslConnect, "SSL_connect");
    DLSYM_UPDATE(handle, MethodSslGetPeerCertificate, sslGetPeerCertificate, "SSL_get_peer_certificate",
        "SSL_get1_peer_certificate");
    DLSYM_RETURN(handle, MethodGetMethod, tlsClientMethod, "TLS_client_method");
    DLSYM_RETURN(handle, MethodInit, initSsl, "OPENSSL_init_ssl");
    DLSYM_RETURN(handle, MethodSslFd, sslSetFd, "SSL_set_fd");
    DLSYM_RETURN(handle, MethodSslGetVerifyResult, sslGetVerifyResult, "SSL_get_verify_result");
    DLSYM_RETURN(handle, MethodCheckPrivateKey, checkPrivateKey, "SSL_CTX_check_private_key");
    DLSYM_RETURN(handle, MethodLoadVerifyLocations, loadVerifyLocations, "SSL_CTX_load_verify_locations");
    DLSYM_RETURN(handle, MethodSslCtxSetVerify, sslCtxSetVerify, "SSL_CTX_set_verify");
    DLSYM_RETURN(handle, MethodOpensslCleanup, opensslCleanup, "OPENSSL_cleanup");
    DLSYM_RETURN(handle, MethodSetCipherSuites, setCipherSuites, "SSL_CTX_set_ciphersuites");
    DLSYM_RETURN(handle, MethodSetDefaultPasswdCbUserdata, setDefaultPasswdCbUserdata,
          "SSL_CTX_set_default_passwd_cb_userdata");
    DLSYM_RETURN(handle, MethodUsePrivKeyFile, usePrivKeyFile, "SSL_CTX_use_PrivateKey_file");
    DLSYM_RETURN(handle, MethodGetMethod, tlsServerMethod, "TLS_server_method");
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