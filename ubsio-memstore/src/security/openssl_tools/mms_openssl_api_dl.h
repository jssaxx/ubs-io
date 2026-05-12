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

#ifndef MMS_OPENSSL_API_DL_H
#define MMS_OPENSSL_API_DL_H

#include <string>

namespace ock {
namespace mms {
using OPENSSL_INIT_SETTINGS = struct ossl_init_settings_st;
using SSL_METHOD = struct ssl_method_st;
using SSL = struct ssl_st;
using SSL_CTX = struct ssl_ctx_st;
using X509_STORE_CTX = struct x509_store_ctx_st;
using X509_CRL = struct x509_crl;
using X509_REVOKED = struct x509_revoked;
using ENGINE = struct engine_st;
using EVP_CIPHER = struct evp_cipher_st;
using EVP_CIPHER_CTX = struct evp_cipher_ctx_st;
using SSL_CIPHER = struct ssl_cipher_st;
using X509 = struct x509_st;
using BIO = struct bio;
using PEM_PASSWORD_CB = struct pem_password_cb;
using BIO_METHOD = struct bio_method;
using X509_STORE = struct x509_store;
using ASN1_TIME = struct asn1_string_st;
using EVP_PKEY = struct evp_pkey_st;

using MethodInit = int (*)(uint64_t, const OPENSSL_INIT_SETTINGS*);
using MethodOpensslCleanup = void (*)();
using MethodGetMethod = const SSL_METHOD* (*)(void);
using MethodSslOperation = int (*)(SSL*);
using MethodSslFd = int (*)(SSL*, int);
using MethodSslNew = SSL* (*)(SSL_CTX*);
using MethodSslFree = void (*)(SSL*);
using MethodSslCtxNew = SSL_CTX* (*)(const SSL_METHOD*);
using MethodSslCtxFree = void (*)(SSL_CTX*);
using MethodSslWrite = int (*)(SSL*, const void*, int);
using MethodSslRead = int (*)(SSL*, void*, int);
using MethodSslGetError = int (*)(const SSL*, int);

using MethodSetCipherSuites = int (*)(SSL_CTX*, const char*);
using MethodSslCtxCtrl = long (*)(SSL_CTX*, int, long, void*);
using MethodSslGetCurrentCipher = const SSL_CIPHER* (*)(const SSL*);
using MethodSslGetVersion = const char* (*)(const SSL*);

using MethodUsePrivKeyFile = int (*)(SSL_CTX* ctx, const char*, int);
using MethodPemReadX509 = X509* (*)(FILE* fp, X509** x, pem_password_cb* cb, void* u);
using MethodX509Free = X509* (*)(X509* cert);
using MethodAsn1Time2Tm = int (*)(const ASN1_TIME* s, struct tm* tm);
using MethodSslCtxSetVerify = void (*)(SSL_CTX*, int mode, int (*)(int, X509_STORE_CTX*));
using MethodSetDefaultPasswdCbUserdata = void (*)(SSL_CTX*, void*);
using MethodSetCertVerifyCallback = void (*)(SSL_CTX*, int (*cb)(X509_STORE_CTX*, void*), void*);
using MethodLoadVerifyLocations = int (*)(SSL_CTX*, const char*, const char*);
using MethodCheckPrivateKey = int (*)(const SSL_CTX*);
using MethodSslGetVerifyResult = long (*)(const SSL*);
using MethodSslGetPeerCertificate = X509* (*)(const SSL*);

using MethodEvpAesCipher = const EVP_CIPHER* (*)();
using MethodEvpCipherCtxNew = EVP_CIPHER_CTX* (*)();
using MethodEvpCipherCtxFree = void (*)(EVP_CIPHER_CTX*);
using MethodEvpCipherCtxCtrl = int (*)(EVP_CIPHER_CTX*, int, int, void*);
using MethodEvpEncryptInitEx = int (*)(EVP_CIPHER_CTX*, const EVP_CIPHER*, ENGINE*, const unsigned char*,
                                     const unsigned char*);
using MethodEvpEncryptUpdate = int (*)(EVP_CIPHER_CTX*, unsigned char*, int*, const unsigned char*, int);
using MethodEvpEncryptFinalEx = int (*)(EVP_CIPHER_CTX*, unsigned char*, int*);
using MethodEvpDecryptInitEx = MethodEvpEncryptInitEx;
using MethodEvpDecryptUpdate = MethodEvpEncryptUpdate;
using MethodEvpDecryptFinalEx = MethodEvpEncryptFinalEx;

using MethodRandPoll = int (*)(void);
using MethodRandStatus = MethodRandPoll;
using MethodRandBytes = int (*)(unsigned char* buf, int num);
using MethodRandSeed = void (*)(const void*, int);

using MethodX509VerifyCert = int (*)(X509_STORE_CTX* ctx);
using MethodX509VerifyCertErrorString = const char* (*)(long n);
using MethodX509StoreCtxGetError = int (*)(const X509_STORE_CTX* ctx);
using MethodPemReadBioX509Crl = X509_CRL* (*)(BIO* bp, X509_CRL** x, PEM_PASSWORD_CB* cb, void* u);
using MethodBioSFile = const BIO_METHOD* (*)(void);
using MethodBioNew = BIO* (*)(const BIO_METHOD*);
using MethodBioFree = void (*)(BIO* b);
using MethodBioCtrl = long (*)(BIO* bp, int cmd, long larg, void* parg);
using MethodX509StoreCtxGet0Store = X509_STORE* (*)(const X509_STORE_CTX* ctx);
using MethodX509StoreCtxSetFlags = void (*)(X509_STORE_CTX* ctx, unsigned long flags);
using MethodX509StoreAddCrl = int (*)(X509_STORE* xs, X509_CRL* x);
using MethodX509CrlFree = void (*)(X509_CRL* x);

using MethodX509CmpCurrentTime = int (*)(const ASN1_TIME* s);
using MethodX509CrlGet0ByCert = int (*)(X509_CRL* crl, X509_REVOKED** ret, X509* x);
using MethodX509CrlGet0NextUpdate = const ASN1_TIME* (*)(const X509_CRL* crl);
using MethodX509GetNotAfter = ASN1_TIME* (*)(const X509* x);
using MethodX509GetNotBefore = ASN1_TIME* (*)(const X509* x);
using MethodX509GetPubkey = EVP_PKEY* (*)(X509* x);
using MethodEvpPkeyFree = void (*)(EVP_PKEY* pkey);

class DlOpensslApi {
public:
    static MethodInit initSsl;
    static MethodInit initCrypto;
    static MethodOpensslCleanup opensslCleanup;
    static MethodGetMethod tlsServerMethod;
    static MethodGetMethod tlsClientMethod;
    static MethodSslOperation sslShutdown;
    static MethodSslFd sslSetFd;
    static MethodSslNew sslNew;
    static MethodSslFree sslFree;
    static MethodSslCtxNew sslCtxNew;
    static MethodSslCtxFree sslCtxFree;
    static MethodSslWrite sslWrite;
    static MethodSslRead sslRead;
    static MethodSslOperation sslConnect;
    static MethodSslOperation sslAccept;
    static MethodSslGetError sslGetError;
    static MethodSslCtxCtrl sslCtxCtrl;
    static MethodSslGetCurrentCipher sslGetCurrentCipher;
    static MethodSslGetVersion sslGetVersion;
    static MethodSetCipherSuites setCipherSuites;
    static MethodUsePrivKeyFile usePrivKeyFile;
    static MethodPemReadX509 pemReadX509;
    static MethodX509Free x509Free;
    static MethodAsn1Time2Tm asn1Time2Tm;
    static MethodSslCtxSetVerify sslCtxSetVerify;
    static MethodSetDefaultPasswdCbUserdata setDefaultPasswdCbUserdata;
    static MethodSetCertVerifyCallback setCertVerifyCallback;
    static MethodLoadVerifyLocations loadVerifyLocations;
    static MethodCheckPrivateKey checkPrivateKey;
    static MethodSslGetVerifyResult sslGetVerifyResult;
    static MethodSslGetPeerCertificate sslGetPeerCertificate;
    static MethodEvpAesCipher evpAes128Gcm;
    static MethodEvpAesCipher evpAes256Gcm;
    static MethodEvpCipherCtxNew evpCipherCtxNew;
    static MethodEvpCipherCtxFree evpCipherCtxFree;
    static MethodEvpCipherCtxCtrl evpCipherCtxCtrl;
    static MethodEvpEncryptInitEx evpEncryptInitEx;
    static MethodEvpEncryptUpdate evpEncryptUpdate;
    static MethodEvpEncryptFinalEx evpEncryptFinalEx;
    static MethodEvpDecryptInitEx evpDecryptInitEx;
    static MethodEvpDecryptUpdate evpDecryptUpdate;
    static MethodEvpDecryptFinalEx evpDecryptFinalEx;
    static MethodRandPoll randPoll;
    static MethodRandStatus randStatus;
    static MethodRandBytes randBytes;
    static MethodRandBytes randPrivBytes;
    static MethodRandSeed randSeed;
    static MethodX509VerifyCert x509VerifyCert;
    static MethodX509VerifyCertErrorString x509VerifyCertErrorString;
    static MethodX509StoreCtxGetError x509StoreCtxGetError;
    static MethodPemReadBioX509Crl pemReadBioX509Crl;
    static MethodBioSFile bioSFile;
    static MethodBioNew bioNew;
    static MethodBioFree bioFree;
    static MethodBioCtrl bioCtrl;
    static MethodX509StoreCtxGet0Store x509StoreCtxGet0Store;
    static MethodX509StoreCtxSetFlags x509StoreCtxSetFlags;
    static MethodX509StoreAddCrl x509StoreAddCrl;
    static MethodX509CrlFree x509CrlFree;
    static MethodX509CmpCurrentTime x509CmpCurrentTime;
    static MethodX509CrlGet0ByCert x509CrlGet0ByCert;
    static MethodX509CrlGet0NextUpdate x509CrlGet0NextUpdate;
    static MethodX509GetNotAfter x509GetNotAfter;
    static MethodX509GetNotBefore x509GetNotBefore;
    static MethodX509GetPubkey x509GetPubkey;
    static MethodEvpPkeyFree evpPkeyFree;

    static int LoadOpensslApiDl(const std::string &libPath);

private:
    static const char* gOpensslLibSslName;
    static const char* gOpensslLibCryptoName;
    static const char* gSep;
    static bool gStarted;

    static int GetLibPath(std::string dir, std::string &libSslPath, std::string &libCryptoPath);
    static int LoadSSLMethod(void *handle);
    static int LoadCryptoMethod(void *handle);
};
} // namespace mms
} // namespace ock

#endif // MMS_OPENSSL_API_DL_H

