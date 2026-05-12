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

#ifndef MMS_OPENSSL_API_WRAPPER_H
#define MMS_OPENSSL_API_WRAPPER_H

#include "mms_log.h"
#include "mms_err.h"
#include "mms_openssl_api_dl.h"

namespace ock {
namespace mms {
class OpenSslApiWrapper {
public:
    static const uint32_t SSL_VERIFY_NONE = 0U;
    static const uint32_t SSL_VERIFY_PEER = 1U;
    static const uint32_t SSL_VERIFY_FAIL_IF_NO_PEER_CERT = 2U;
    static const uint32_t SSL_FILETYPE_PEM = 1U;
    static const uint32_t EVP_CTRL_AEAD_SET_IVLEN = 9U;
    static const uint32_t EVP_CTRL_AEAD_GET_TAG = 16U;
    static const uint32_t EVP_CTRL_AEAD_SET_TAG = 17U;
    static const uint32_t OPENSSL_INIT_LOAD_SSL_STRINGS = 2097152U;
    static const uint32_t OPENSSL_INIT_LOAD_CRYPTO_STRINGS = 2U;
    static const uint32_t SSL_CTRL_SET_MIN_PROTO_VERSION = 123U;
    static const uint32_t TLS1_3_VERSION = 772U;
    static const uint32_t SSL_ERROR_WANT_READ = 2U;
    static const uint32_t SSL_ERROR_WANT_WRITE = 3U;
    static const uint32_t SSL_ERROR_ZERO_RETURN = 6U;

    static const uint32_t SSL_SENT_SHUTDOWN = 1U;
    static const uint32_t SSL_RECEIVED_SHUTDOWN = 2U;

    static const uint32_t BIO_C_SET_FILENAME = 108U;
    static const uint32_t BIO_CLOSE = 1U;
    static const uint32_t BIO_FP_READ = 2U;
    static const uint32_t X509_V_FLAG_CRL_CHECK = 4U;
    static const uint32_t X509_V_FLAG_CRL_CHECK_ALL = 8U;

    static int OpensslInitSsl(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings)
    {
        ChkTrue(DlOpensslApi::initSsl != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::initSsl(opts, settings);
    }

    static inline int OpensslInitCrypto(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings)
    {
        ChkTrue(DlOpensslApi::initCrypto != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::initCrypto(opts, settings);
    }

    static inline const SSL_METHOD *TlsClientMethod()
    {
        ChkTrue(DlOpensslApi::tlsClientMethod != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::tlsClientMethod();
    }

    static inline const SSL_METHOD *TlsServerMethod()
    {
        ChkTrue(DlOpensslApi::tlsServerMethod != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::tlsServerMethod();
    }

    static inline int SslShutdown(SSL *s)
    {
        ChkTrue(DlOpensslApi::sslShutdown != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::sslShutdown(s);
    }

    static inline int SslSetFd(SSL *s, int fd)
    {
        ChkTrue(DlOpensslApi::sslSetFd != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::sslSetFd(s, fd);
    }

    static inline SSL *SslNew(SSL_CTX *ctx)
    {
        ChkTrue(DlOpensslApi::sslNew != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::sslNew(ctx);
    }

    static inline void SslFree(SSL *s)
    {
        ChkTrueVoid(DlOpensslApi::sslFree != nullptr, "openssl handler not loaded.");
        DlOpensslApi::sslFree(s);
    }

    static SSL_CTX *SslCtxNew(const SSL_METHOD *method)
    {
        ChkTrue(DlOpensslApi::sslCtxNew != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::sslCtxNew(method);
    }

    static inline void SslCtxFree(SSL_CTX *ctx)
    {
        ChkTrueVoid(DlOpensslApi::sslCtxFree != nullptr, "openssl handler not loaded.");
        DlOpensslApi::sslCtxFree(ctx);
    }

    static inline int SslWrite(SSL *s, const void *buf, int num)
    {
        ChkTrue(DlOpensslApi::sslWrite != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::sslWrite(s, buf, num);
    }

    static inline int SslRead(SSL *s, void *buf, int num)
    {
        ChkTrue(DlOpensslApi::sslRead != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::sslRead(s, buf, num);
    }

    static inline int SslConnect(SSL *s)
    {
        ChkTrue(DlOpensslApi::sslConnect != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::sslConnect(s);
    }

    static inline int SslAccept(SSL *s)
    {
        ChkTrue(DlOpensslApi::sslAccept != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::sslAccept(s);
    }

    static inline int SslGetError(const SSL *s, int retCode)
    {
        ChkTrue(DlOpensslApi::sslGetError != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::sslGetError(s, retCode);
    }

    static inline int SslCtxSetCipherSuites(SSL_CTX *ctx, const char *str)
    {
        ChkTrue(DlOpensslApi::setCipherSuites != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::setCipherSuites(ctx, str);
    }

    static inline long SslCtxCtrl(SSL_CTX *ctx, int cmd, long larg, void *parg)
    {
        ChkTrue(DlOpensslApi::sslCtxCtrl != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::sslCtxCtrl(ctx, cmd, larg, parg);
    }

    static inline const char *SslGetVersion(const SSL *ssl)
    {
        ChkTrue(DlOpensslApi::sslGetVersion != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::sslGetVersion(ssl);
    }

    static inline void SslCtxSetVerify(SSL_CTX *ctx, int mode, int (*cb)(int, X509_STORE_CTX *))
    {
        ChkTrueVoid(DlOpensslApi::sslCtxSetVerify != nullptr, "openssl handler not loaded.");
        DlOpensslApi::sslCtxSetVerify(ctx, mode, cb);
    }

    static inline int SslCtxUsePrivateKeyFile(SSL_CTX *ctx, const char *file, int type)
    {
        ChkTrue(DlOpensslApi::usePrivKeyFile != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::usePrivKeyFile(ctx, file, type);
    }

    static inline X509 *PemReadX509(FILE *fp, X509 **x, PEM_PASSWORD_CB *cb, void *u)
    {
        ChkTrue(DlOpensslApi::pemReadX509 != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::pemReadX509(fp, x, cb, u);
    }

    static inline void X509Free(X509 *cert)
    {
        ChkTrueVoid(DlOpensslApi::x509Free != nullptr, "openssl handler not loaded.");
        DlOpensslApi::x509Free(cert);
    }

    static inline int Asn1Time2Tm(const ASN1_TIME *s, struct tm *tm)
    {
        ChkTrue(DlOpensslApi::asn1Time2Tm != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::asn1Time2Tm(s, tm);
    }

    static inline void SslCtxSetDefaultPasswdCbUserdata(SSL_CTX *ctx, void *u)
    {
        ChkTrueVoid(DlOpensslApi::setDefaultPasswdCbUserdata != nullptr, "openssl handler not loaded.");
        DlOpensslApi::setDefaultPasswdCbUserdata(ctx, u);
    }

    static inline void SslCtxSetCertVerifyCallback(SSL_CTX *ctx, int (*cb)(X509_STORE_CTX *, void *), void *arg)
    {
        ChkTrueVoid(DlOpensslApi::setCertVerifyCallback != nullptr, "openssl handler not loaded.");
        DlOpensslApi::setCertVerifyCallback(ctx, cb, arg);
    }

    static inline int SslCtxLoadVerifyLocations(SSL_CTX *ctx, const char *cafile, const char *capath)
    {
        ChkTrue(DlOpensslApi::loadVerifyLocations != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::loadVerifyLocations(ctx, cafile, capath);
    }

    static inline int SslCtxCheckPrivateKey(const SSL_CTX *ctx)
    {
        ChkTrue(DlOpensslApi::checkPrivateKey != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::checkPrivateKey(ctx);
    }

    static inline X509 *SslGetPeerCertificate(const SSL *ssl)
    {
        ChkTrue(DlOpensslApi::sslGetPeerCertificate != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::sslGetPeerCertificate(ssl);
    }

    static inline long SslGetVerifyResult(const SSL *ssl)
    {
        ChkTrue(DlOpensslApi::sslGetVerifyResult != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::sslGetVerifyResult(ssl);
    }

    static inline const EVP_CIPHER *EvpAes128Gcm()
    {
        ChkTrue(DlOpensslApi::evpAes128Gcm != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::evpAes128Gcm();
    }

    static inline const EVP_CIPHER *EvpAes256Gcm()
    {
        ChkTrue(DlOpensslApi::evpAes256Gcm != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::evpAes256Gcm();
    }

    static inline EVP_CIPHER_CTX *EvpCipherCtxNew()
    {
        ChkTrue(DlOpensslApi::evpCipherCtxNew != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::evpCipherCtxNew();
    }

    static inline void EvpCipherCtxFree(EVP_CIPHER_CTX *ctx)
    {
        ChkTrueVoid(DlOpensslApi::evpCipherCtxFree != nullptr, "openssl handler not loaded.");
        DlOpensslApi::evpCipherCtxFree(ctx);
    }

    static inline int EvpCipherCtxCtrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr)
    {
        ChkTrue(DlOpensslApi::evpCipherCtxCtrl != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::evpCipherCtxCtrl(ctx, type, arg, ptr);
    }

    static inline int EvpEncryptInitEx(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl,
                                       const unsigned char *key, const unsigned char *iv)
    {
        ChkTrue(DlOpensslApi::evpEncryptInitEx != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::evpEncryptInitEx(ctx, cipher, impl, key, iv);
    }

    static inline int EvpEncryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in,
                                       int inl)
    {
        ChkTrue(DlOpensslApi::evpEncryptUpdate != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::evpEncryptUpdate(ctx, out, outl, in, inl);
    }

    static inline int EvpEncryptFinalEx(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
    {
        ChkTrue(DlOpensslApi::evpEncryptFinalEx != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::evpEncryptFinalEx(ctx, out, outl);
    }

    static inline int EvpDecryptInitEx(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl,
                                       const unsigned char *key, const unsigned char *iv)
    {
        ChkTrue(DlOpensslApi::evpDecryptInitEx != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::evpDecryptInitEx(ctx, cipher, impl, key, iv);
    }

    static inline int EvpDecryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in,
                                       int inl)
    {
        ChkTrue(DlOpensslApi::evpDecryptUpdate != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::evpDecryptUpdate(ctx, out, outl, in, inl);
    }

    static inline int EvpDecryptFinalEx(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
    {
        ChkTrue(DlOpensslApi::evpDecryptFinalEx != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::evpDecryptFinalEx(ctx, out, outl);
    }

    static inline int RandPoll()
    {
        ChkTrue(DlOpensslApi::randPoll != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::randPoll();
    }

    static inline int RandStatus()
    {
        ChkTrue(DlOpensslApi::randStatus != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::randStatus();
    }

    static inline int RandPrivBytes(unsigned char *buf, int num)
    {
        ChkTrue(DlOpensslApi::randPrivBytes != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::randPrivBytes(buf, num);
    }

    static inline int X509VerifyCert(X509_STORE_CTX *ctx)
    {
        ChkTrue(DlOpensslApi::x509VerifyCert != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::x509VerifyCert(ctx);
    }

    static inline const char *X509VerifyCertErrorString(long n)
    {
        ChkTrue(DlOpensslApi::x509VerifyCertErrorString != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::x509VerifyCertErrorString(n);
    }

    static inline int X509StoreCtxGetError(X509_STORE_CTX *ctx)
    {
        ChkTrue(DlOpensslApi::x509StoreCtxGetError != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::x509StoreCtxGetError(ctx);
    }

    static inline X509_CRL *PemReadBioX509Crl(BIO *bp, X509_CRL **x, PEM_PASSWORD_CB *cb, void *u)
    {
        ChkTrue(DlOpensslApi::pemReadBioX509Crl != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::pemReadBioX509Crl(bp, x, cb, u);
    }

    static inline const BIO_METHOD *BioSFile()
    {
        ChkTrue(DlOpensslApi::bioSFile != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::bioSFile();
    }

    static inline BIO *BioNew(const BIO_METHOD *bioMethod)
    {
        ChkTrue(DlOpensslApi::bioNew != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::bioNew(bioMethod);
    }

    static inline long BioCtrl(BIO *bp, int cmd, long larg, void *parg)
    {
        ChkTrue(DlOpensslApi::bioCtrl != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::bioCtrl(bp, cmd, larg, parg);
    }

    static inline void BioFree(BIO *b)
    {
        ChkTrueVoid(DlOpensslApi::bioFree != nullptr, "openssl handler not loaded.");
        return DlOpensslApi::bioFree(b);
    }

    static inline X509_STORE *X509StoreCtxGet0Store(const X509_STORE_CTX *ctx)
    {
        ChkTrue(DlOpensslApi::x509StoreCtxGet0Store != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::x509StoreCtxGet0Store(ctx);
    }

    static inline void X509StoreCtxSetFlags(X509_STORE_CTX *ctx, unsigned long flags)
    {
        ChkTrueVoid(DlOpensslApi::x509StoreCtxSetFlags != nullptr, "openssl handler not loaded.");
        return DlOpensslApi::x509StoreCtxSetFlags(ctx, flags);
    }

    static inline int X509StoreAddCrl(X509_STORE *xs, X509_CRL *x)
    {
        ChkTrue(DlOpensslApi::x509StoreAddCrl != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::x509StoreAddCrl(xs, x);
    }

    static inline void X509CrlFree(X509_CRL *x)
    {
        ChkTrueVoid(DlOpensslApi::x509CrlFree != nullptr, "openssl handler not loaded.");
        return DlOpensslApi::x509CrlFree(x);
    }

    static inline int X509CmpCurrentTime(const ASN1_TIME *s)
    {
        ChkTrue(DlOpensslApi::x509CmpCurrentTime != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::x509CmpCurrentTime(s);
    }

    static inline int X509CrlGet0ByCert(X509_CRL *crl, X509_REVOKED **ret, X509 *x)
    {
        ChkTrue(DlOpensslApi::x509CrlGet0ByCert != nullptr, MMS_ERR, "openssl handler not loaded.");
        return DlOpensslApi::x509CrlGet0ByCert(crl, ret, x);
    }

    static inline const ASN1_TIME *X509CrlGet0NextUpdate(const X509_CRL *crl)
    {
        ChkTrue(DlOpensslApi::x509CrlGet0NextUpdate != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::x509CrlGet0NextUpdate(crl);
    }

    static inline ASN1_TIME *X509GetNotAfter(const X509 *x)
    {
        ChkTrue(DlOpensslApi::x509GetNotAfter != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::x509GetNotAfter(x);
    }

    static inline ASN1_TIME *X509GetNotBefore(const X509 *x)
    {
        ChkTrue(DlOpensslApi::x509GetNotBefore != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::x509GetNotBefore(x);
    }

    static inline EVP_PKEY *X509GetPubkey(X509 *x)
    {
        ChkTrue(DlOpensslApi::x509GetPubkey != nullptr, nullptr, "openssl handler not loaded.");
        return DlOpensslApi::x509GetPubkey(x);
    }

    static inline void EvpPkeyFree(EVP_PKEY *pkey)
    {
        ChkTrueVoid(DlOpensslApi::evpPkeyFree != nullptr, "openssl handler not loaded.");
        return DlOpensslApi::evpPkeyFree(pkey);
    }

    static inline int Load(const std::string &libPath)
    {
        return DlOpensslApi::LoadOpensslApiDl(libPath);
    }

    static inline void UnLoad() {}
};
} // namespace bio
} // namespace ock
#endif // MMS_OPENSSL_API_WRAPPER_H

