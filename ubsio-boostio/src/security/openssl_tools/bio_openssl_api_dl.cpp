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

namespace ock {
namespace bio {
FuncInit OpensslApiDl::initSsl = nullptr;
FuncInit OpensslApiDl::initCrypto = nullptr;
FuncOpensslCleanup OpensslApiDl::opensslCleanup = nullptr;

FuncGetMethod OpensslApiDl::tlsServerMethod = nullptr;
FuncGetMethod OpensslApiDl::tlsClientMethod = nullptr;
FuncGetMethod OpensslApiDl::tlsMethod = nullptr;
FuncSslOperation OpensslApiDl::sslShutdown = nullptr;
FuncSslFd OpensslApiDl::sslSetFd = nullptr;
FuncSslNew OpensslApiDl::sslNew = nullptr;
FuncSslFree OpensslApiDl::sslFree = nullptr;
FuncSslCtxNew OpensslApiDl::sslCtxNew = nullptr;
FuncSslCtxFree OpensslApiDl::sslCtxFree = nullptr;
FuncSslWrite OpensslApiDl::sslWrite = nullptr;
FuncSslRead OpensslApiDl::sslRead = nullptr;
FuncSslOperation OpensslApiDl::sslConnect = nullptr;
FuncSslOperation OpensslApiDl::sslConnectState = nullptr;
FuncSslOperation OpensslApiDl::sslAccept = nullptr;
FuncSslOperation OpensslApiDl::sslAcceptState = nullptr;
FuncSslOperation OpensslApiDl::sslGetShutdown = nullptr;
FuncSslGetError OpensslApiDl::sslGetError = nullptr;
FuncSslWriteEx OpensslApiDl::sslWriteEx = nullptr;
FuncSslReadEx OpensslApiDl::sslReadEx = nullptr;

FuncSslCtxCtrl OpensslApiDl::sslCtxCtrl = nullptr;
FuncSslGetCurrentCipher OpensslApiDl::sslGetCurrentCipher = nullptr;
FuncSslGetVersion OpensslApiDl::sslGetVersion = nullptr;
FuncSslIsServer OpensslApiDl::sslIsServer = nullptr;
FuncSetCipherSuites OpensslApiDl::setCipherSuites = nullptr;
FuncUsePrivKey OpensslApiDl::usePrivKey = nullptr;
FuncUsePrivKeyFile OpensslApiDl::usePrivKeyFile = nullptr;
FuncUseCertFile OpensslApiDl::useCertFile = nullptr;
FuncSslCtxSetVerify OpensslApiDl::sslCtxSetVerify = nullptr;
FuncSetDefaultPasswdCbUserdata OpensslApiDl::setDefaultPasswdCbUserdata = nullptr;
FuncSetCertVerifyCallback OpensslApiDl::setCertVerifyCallback = nullptr;
FuncLoadVerifyLocations OpensslApiDl::loadVerifyLocations = nullptr;
FuncCheckPrivateKey OpensslApiDl::checkPrivateKey = nullptr;
FuncSslGetVerifyResult OpensslApiDl::sslGetVerifyResult = nullptr;
FuncSslGetPeerCertificate OpensslApiDl::sslGetPeerCertificate = nullptr;
FuncSsLCtxGet0Certificate OpensslApiDl::ssLCtxGet0Certificate = nullptr;

FuncEvpAesCipher OpensslApiDl::evpAes128Gcm = nullptr;
FuncEvpAesCipher OpensslApiDl::evpAes256Gcm = nullptr;

FuncEvpCipherCtxNew OpensslApiDl::evpCipherCtxNew = nullptr;
FuncEvpCipherCtxFree OpensslApiDl::evpCipherCtxFree = nullptr;
FuncEvpCipherCtxCtrl OpensslApiDl::evpCipherCtxCtrl = nullptr;

FuncEvpEncryptInitEx OpensslApiDl::evpEncryptInitEx = nullptr;
FuncEvpEncryptUpdate OpensslApiDl::evpEncryptUpdate = nullptr;
FuncEvpEncryptFinalEx OpensslApiDl::evpEncryptFinalEx = nullptr;
FuncEvpDecryptInitEx OpensslApiDl::evpDecryptInitEx = nullptr;
FuncEvpDecryptUpdate OpensslApiDl::evpDecryptUpdate = nullptr;
FuncEvpDecryptFinalEx OpensslApiDl::evpDecryptFinalEx = nullptr;

FuncRandPoll OpensslApiDl::randPoll = nullptr;
FuncRandStatus OpensslApiDl::randStatus = nullptr;
FuncRandBytes OpensslApiDl::randBytes = nullptr;
FuncRandBytes OpensslApiDl::randPrivBytes = nullptr;
FuncRandSeed OpensslApiDl::randSeed = nullptr;

FuncX509VerifyCert OpensslApiDl::x509VerifyCert = nullptr;
FuncX509VerifyCertErrorString OpensslApiDl::x509VerifyCertErrorString = nullptr;
FuncX509StoreCtxGetError OpensslApiDl::x509StoreCtxGetError = nullptr;
FuncPemReadBioX509Crl OpensslApiDl::pemReadBioX509Crl = nullptr;
FuncPemReadBioPk OpensslApiDl::pemReadBioPk = nullptr;
FuncBioSFile OpensslApiDl::bioSFile = nullptr;
FuncBioNew OpensslApiDl::bioNew = nullptr;
FuncBioNewMemBuf OpensslApiDl::bioNewMemBuf = nullptr;
FuncBioFree OpensslApiDl::bioFree = nullptr;
FuncBioCtrl OpensslApiDl::bioCtrl = nullptr;
FuncX509StoreCtxGet0Store OpensslApiDl::x509StoreCtxGet0Store = nullptr;
FuncX509StoreCtxSetFlags OpensslApiDl::x509StoreCtxSetFlags = nullptr;
FuncX509StoreAddCrl OpensslApiDl::x509StoreAddCrl = nullptr;
FuncX509CrlFree OpensslApiDl::x509CrlFree = nullptr;

FuncX509CmpCurrentTime OpensslApiDl::x509CmpCurrentTime = nullptr;
FuncX509CrlGet0ByCert OpensslApiDl::x509CrlGet0ByCert = nullptr;
FuncX509CrlGet0NextUpdate OpensslApiDl::x509CrlGet0NextUpdate = nullptr;
FuncX509GetNotAfter OpensslApiDl::x509GetNotAfter = nullptr;
FuncX509GetNotBefore OpensslApiDl::x509GetNotBefore = nullptr;
FuncX509GetPubkey OpensslApiDl::x509GetPubkey = nullptr;
FuncEvpPkeyBits OpensslApiDl::evpPkeyBits = nullptr;
FuncEvpPkeyFree OpensslApiDl::evpPkeyFree = nullptr;
FuncPemReadX509 OpensslApiDl::pemReadX509 = nullptr;
FuncX509Free OpensslApiDl::x509Free = nullptr;
FuncAsn1Time2Tm OpensslApiDl::asn1Time2Tm = nullptr;

bool OpensslApiDl::gLoaded = false;
const char *OpensslApiDl::gOpensslLibSslName = "libssl.so";
const char *OpensslApiDl::gOpensslLibCryptoName = "libcrypto.so";
const char *OpensslApiDl::gSep = "/";

int OpensslApiDl::LoadSSLSymbols(void *sslHandle)
{
    DLSYM(sslHandle, FuncInit, initSsl, "OPENSSL_init_ssl");
    DLSYM(sslHandle, FuncInit, initCrypto, "OPENSSL_init_crypto");
    DLSYM(sslHandle, FuncOpensslCleanup, opensslCleanup, "OPENSSL_cleanup");
    DLSYM(sslHandle, FuncGetMethod, tlsServerMethod, "TLS_server_method");
    DLSYM(sslHandle, FuncGetMethod, tlsClientMethod, "TLS_client_method");
    DLSYM(sslHandle, FuncGetMethod, tlsMethod, "TLS_method");
    DLSYM(sslHandle, FuncSslOperation, sslShutdown, "SSL_shutdown");
    DLSYM(sslHandle, FuncSslFd, sslSetFd, "SSL_set_fd");
    DLSYM(sslHandle, FuncSslNew, sslNew, "SSL_new");
    DLSYM(sslHandle, FuncSslFree, sslFree, "SSL_free");
    DLSYM(sslHandle, FuncSslCtxNew, sslCtxNew, "SSL_CTX_new");
    DLSYM(sslHandle, FuncSslCtxFree, sslCtxFree, "SSL_CTX_free");
    DLSYM(sslHandle, FuncSslWrite, sslWrite, "SSL_write");
    DLSYM(sslHandle, FuncSslRead, sslRead, "SSL_read");
    DLSYM(sslHandle, FuncSslOperation, sslConnect, "SSL_connect");
    DLSYM(sslHandle, FuncSslOperation, sslConnectState, "SSL_set_connect_state");
    DLSYM(sslHandle, FuncSslOperation, sslAccept, "SSL_accept");
    DLSYM(sslHandle, FuncSslOperation, sslAcceptState, "SSL_set_accept_state");
    DLSYM(sslHandle, FuncSslOperation, sslGetShutdown, "SSL_get_shutdown");
    DLSYM(sslHandle, FuncSslGetError, sslGetError, "SSL_get_error");
    DLSYM(sslHandle, FuncSetCipherSuites, setCipherSuites, "SSL_CTX_set_ciphersuites");
    DLSYM(sslHandle, FuncSslCtxCtrl, sslCtxCtrl, "SSL_CTX_ctrl");
    DLSYM(sslHandle, FuncSslGetCurrentCipher, sslGetCurrentCipher, "SSL_get_current_cipher");
    DLSYM(sslHandle, FuncSslGetVersion, sslGetVersion, "SSL_get_version");
    DLSYM(sslHandle, FuncUsePrivKey, usePrivKey, "SSL_CTX_use_PrivateKey");
    DLSYM(sslHandle, FuncUsePrivKeyFile, usePrivKeyFile, "SSL_CTX_use_PrivateKey_file");
    DLSYM(sslHandle, FuncUseCertFile, useCertFile, "SSL_CTX_use_certificate_file");
    DLSYM(sslHandle, FuncSslCtxSetVerify, sslCtxSetVerify, "SSL_CTX_set_verify");
    DLSYM(sslHandle, FuncSetDefaultPasswdCbUserdata, setDefaultPasswdCbUserdata,
          "SSL_CTX_set_default_passwd_cb_userdata");
    DLSYM(sslHandle, FuncSetCertVerifyCallback, setCertVerifyCallback, "SSL_CTX_set_cert_verify_callback");
    DLSYM(sslHandle, FuncLoadVerifyLocations, loadVerifyLocations, "SSL_CTX_load_verify_locations");
    DLSYM(sslHandle, FuncCheckPrivateKey, checkPrivateKey, "SSL_CTX_check_private_key");
    DLSYM(sslHandle, FuncSslGetVerifyResult, sslGetVerifyResult, "SSL_get_verify_result");
    DLSYM(sslHandle, FuncSslGetPeerCertificate, sslGetPeerCertificate, "SSL_get1_peer_certificate");
    DLSYM(sslHandle, FuncSsLCtxGet0Certificate, ssLCtxGet0Certificate, "SSL_CTX_get0_certificate");
    DLSYM(sslHandle, FuncSslWriteEx, sslWriteEx, "SSL_write_ex");
    DLSYM(sslHandle, FuncSslReadEx, sslReadEx, "SSL_read_ex");
    DLSYM(sslHandle, FuncSslIsServer, sslIsServer, "SSL_is_server");
    return 0;
}

int OpensslApiDl::LoadCryptoSymbols(void *cryptoHandle)
{
    DLSYM(cryptoHandle, FuncEvpCipherCtxNew, evpCipherCtxNew, "EVP_CIPHER_CTX_new");
    DLSYM(cryptoHandle, FuncEvpCipherCtxFree, evpCipherCtxFree, "EVP_CIPHER_CTX_free");
    DLSYM(cryptoHandle, FuncEvpCipherCtxCtrl, evpCipherCtxCtrl, "EVP_CIPHER_CTX_ctrl");
    DLSYM(cryptoHandle, FuncEvpEncryptInitEx, evpEncryptInitEx, "EVP_EncryptInit_ex");
    DLSYM(cryptoHandle, FuncEvpEncryptUpdate, evpEncryptUpdate, "EVP_EncryptUpdate");
    DLSYM(cryptoHandle, FuncEvpEncryptFinalEx, evpEncryptFinalEx, "EVP_EncryptFinal_ex");
    DLSYM(cryptoHandle, FuncEvpDecryptInitEx, evpDecryptInitEx, "EVP_DecryptInit_ex");
    DLSYM(cryptoHandle, FuncEvpDecryptUpdate, evpDecryptUpdate, "EVP_DecryptUpdate");
    DLSYM(cryptoHandle, FuncEvpDecryptFinalEx, evpDecryptFinalEx, "EVP_DecryptFinal_ex");
    DLSYM(cryptoHandle, FuncEvpAesCipher, evpAes128Gcm, "EVP_aes_128_gcm");
    DLSYM(cryptoHandle, FuncEvpAesCipher, evpAes256Gcm, "EVP_aes_256_gcm");

    DLSYM(cryptoHandle, FuncRandPoll, randPoll, "RAND_poll");
    DLSYM(cryptoHandle, FuncRandStatus, randStatus, "RAND_status");
    DLSYM(cryptoHandle, FuncRandBytes, randBytes, "RAND_bytes");
    DLSYM(cryptoHandle, FuncRandBytes, randPrivBytes, "RAND_priv_bytes");
    DLSYM(cryptoHandle, FuncRandSeed, randSeed, "RAND_seed");

    DLSYM(cryptoHandle, FuncX509VerifyCert, x509VerifyCert, "X509_verify_cert");
    DLSYM(cryptoHandle, FuncX509VerifyCertErrorString, x509VerifyCertErrorString, "X509_verify_cert_error_string");
    DLSYM(cryptoHandle, FuncX509StoreCtxGetError, x509StoreCtxGetError, "X509_STORE_CTX_get_error");
    DLSYM(cryptoHandle, FuncPemReadBioX509Crl, pemReadBioX509Crl, "PEM_read_bio_X509_CRL");
    DLSYM(cryptoHandle, FuncPemReadBioPk, pemReadBioPk, "PEM_read_bio_PrivateKey");
    DLSYM(cryptoHandle, FuncBioSFile, bioSFile, "BIO_s_file");
    DLSYM(cryptoHandle, FuncBioNew, bioNew, "BIO_new");
    DLSYM(cryptoHandle, FuncBioNewMemBuf, bioNewMemBuf, "BIO_new_mem_buf");
    DLSYM(cryptoHandle, FuncBioFree, bioFree, "BIO_free");
    DLSYM(cryptoHandle, FuncBioCtrl, bioCtrl, "BIO_ctrl");
    DLSYM(cryptoHandle, FuncX509StoreCtxGet0Store, x509StoreCtxGet0Store, "X509_STORE_CTX_get0_store");
    DLSYM(cryptoHandle, FuncX509StoreCtxSetFlags, x509StoreCtxSetFlags, "X509_STORE_CTX_set_flags");
    DLSYM(cryptoHandle, FuncX509StoreAddCrl, x509StoreAddCrl, "X509_STORE_add_crl");
    DLSYM(cryptoHandle, FuncX509CrlFree, x509CrlFree, "X509_CRL_free");

    DLSYM(cryptoHandle, FuncX509CmpCurrentTime, x509CmpCurrentTime, "X509_cmp_current_time");
    DLSYM(cryptoHandle, FuncX509CrlGet0ByCert, x509CrlGet0ByCert, "X509_CRL_get0_by_cert");
    DLSYM(cryptoHandle, FuncX509CrlGet0NextUpdate, x509CrlGet0NextUpdate, "X509_CRL_get0_nextUpdate");
    DLSYM(cryptoHandle, FuncX509GetNotAfter, x509GetNotAfter, "X509_getm_notAfter");
    DLSYM(cryptoHandle, FuncX509GetNotBefore, x509GetNotBefore, "X509_getm_notBefore");
    DLSYM(cryptoHandle, FuncX509GetPubkey, x509GetPubkey, "X509_get_pubkey");
    DLSYM(cryptoHandle, FuncEvpPkeyBits, evpPkeyBits, "EVP_PKEY_get_bits");
    DLSYM(cryptoHandle, FuncEvpPkeyFree, evpPkeyFree, "EVP_PKEY_free");
    DLSYM(cryptoHandle, FuncPemReadX509, pemReadX509, "PEM_read_X509");
    DLSYM(cryptoHandle, FuncX509Free, x509Free, "X509_free");
    DLSYM(cryptoHandle, FuncAsn1Time2Tm, asn1Time2Tm, "ASN1_TIME_to_tm");
    return 0;
}

int OpensslApiDl::GetLibPath(std::string libDir, std::string &libSslPath, std::string &libCryptoPath)
{
    if (libDir.empty()) {
        libSslPath = gOpensslLibSslName;
        libCryptoPath = gOpensslLibCryptoName;
        return 0;
    }

    if (libDir.back() != '/') {
        libDir.push_back('/');
    }

    libSslPath = libDir + gOpensslLibSslName;
    if (::access(libSslPath.c_str(), F_OK) != 0) {
        LOG_ERROR("libssl.so path is invalid");
        return -1;
    }

    libCryptoPath = libDir + gOpensslLibCryptoName;
    if (::access(libCryptoPath.c_str(), F_OK) != 0) {
        LOG_ERROR("libcrypto.so path is invalid");
        return -1;
    }

    return 0;
}

int OpensslApiDl::LoadOpensslApiDl(const std::string &libPath)
{
    LOG_INFO("Starting to load openssl api");
    if (gLoaded) {
        return 0;
    }

    std::string libSslPath;
    std::string libCryptoPath;
    if (GetLibPath(libPath, libSslPath, libCryptoPath) != 0) {
        return -1;
    }

    auto cryptoHandle = dlopen(libCryptoPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (cryptoHandle == nullptr) {
        LOG_ERROR("Failed to dlopen libcrypto.so err: " << dlerror());
        return -1;
    }

    if (LoadCryptoSymbols(cryptoHandle) == -1) {
        LOG_ERROR("Failed to dlopen libcrypto.so err: " << dlerror());
        dlclose(cryptoHandle);
        return -1;
    }

    auto sslHandle = dlopen(libSslPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
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