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

#ifndef BIO_OPENSSL_API_WRAPPER_H
#define BIO_OPENSSL_API_WRAPPER_H

#include "bio_log.h"
#include "bio_err.h"
#include "bio_openssl_api_dl.h"

namespace ock {
namespace bio {
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
        ChkTrue(OpensslApiDl::initSsl != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::initSsl(opts, settings);
    }

    static inline int OpensslInitCrypto(uint64_t opts, const OPENSSL_INIT_SETTINGS *settings)
    {
        ChkTrue(OpensslApiDl::initCrypto != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::initCrypto(opts, settings);
    }

    static inline const SSL_METHOD *TlsClientMethod()
    {
        ChkTrue(OpensslApiDl::tlsClientMethod != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::tlsClientMethod();
    }

    static inline const SSL_METHOD *TlsMethod()
    {
        ChkTrue(OpensslApiDl::tlsMethod != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::tlsMethod();
    }

    static inline const SSL_METHOD *TlsServerMethod()
    {
        ChkTrue(OpensslApiDl::tlsServerMethod != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::tlsServerMethod();
    }

    static inline int SslShutdown(SSL *s)
    {
        ChkTrue(OpensslApiDl::sslShutdown != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslShutdown(s);
    }

    static inline int SslSetFd(SSL *s, int fd)
    {
        ChkTrue(OpensslApiDl::sslSetFd != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslSetFd(s, fd);
    }

    static inline SSL *SslNew(SSL_CTX *ctx)
    {
        ChkTrue(OpensslApiDl::sslNew != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::sslNew(ctx);
    }

    static inline void SslFree(SSL *s)
    {
        ChkTrueVoid(OpensslApiDl::sslFree != nullptr, "openssl handler not loaded.");
        OpensslApiDl::sslFree(s);
    }

    static SSL_CTX *SslCtxNew(const SSL_METHOD *method)
    {
        ChkTrue(OpensslApiDl::sslCtxNew != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::sslCtxNew(method);
    }

    static inline void SslCtxFree(SSL_CTX *ctx)
    {
        ChkTrueVoid(OpensslApiDl::sslCtxFree != nullptr, "openssl handler not loaded.");
        OpensslApiDl::sslCtxFree(ctx);
    }

    static inline int SslWrite(SSL *s, const void *buf, int num)
    {
        ChkTrue(OpensslApiDl::sslWrite != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslWrite(s, buf, num);
    }

    static inline int SslRead(SSL *s, void *buf, int num)
    {
        ChkTrue(OpensslApiDl::sslRead != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslRead(s, buf, num);
    }

    static inline int SslConnect(SSL *s)
    {
        ChkTrue(OpensslApiDl::sslConnect != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslConnect(s);
    }

    static inline int SslConnectState(SSL *s)
    {
        ChkTrue(OpensslApiDl::sslConnectState != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslConnectState(s);
    }

    static inline int SslAccept(SSL *s)
    {
        ChkTrue(OpensslApiDl::sslAccept != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslAccept(s);
    }

    static inline int SslAcceptState(SSL *s)
    {
        ChkTrue(OpensslApiDl::sslAcceptState != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslAcceptState(s);
    }

    static inline int SslGetShutdown(SSL *s)
    {
        ChkTrue(OpensslApiDl::sslGetShutdown != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslGetShutdown(s);
    }

    static inline int SslGetError(const SSL *s, int retCode)
    {
        ChkTrue(OpensslApiDl::sslGetError != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslGetError(s, retCode);
    }

    static inline int SslWriteEx(SSL *s, const void *buf, size_t num, size_t *written)
    {
        ChkTrue(OpensslApiDl::sslWriteEx != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslWriteEx(s, buf, num, written);
    }

    static inline int SslReadEx(SSL *s, void *buf, size_t num, size_t *readBytes)
    {
        ChkTrue(OpensslApiDl::sslReadEx != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslReadEx(s, buf, num, readBytes);
    }

    static inline int SslCtxSetCipherSuites(SSL_CTX *ctx, const char *str)
    {
        ChkTrue(OpensslApiDl::setCipherSuites != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::setCipherSuites(ctx, str);
    }

    static inline long SslCtxCtrl(SSL_CTX *ctx, int cmd, long larg, void *parg)
    {
        ChkTrue(OpensslApiDl::sslCtxCtrl != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslCtxCtrl(ctx, cmd, larg, parg);
    }

    static inline const char *SslGetVersion(const SSL *ssl)
    {
        ChkTrue(OpensslApiDl::sslGetVersion != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::sslGetVersion(ssl);
    }

    static inline int SslIsServer(SSL *ssl)
    {
        ChkTrue(OpensslApiDl::sslIsServer != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslIsServer(ssl);
    }

    static inline void SslCtxSetVerify(SSL_CTX *ctx, int mode, int (*cb)(int, X509_STORE_CTX *))
    {
        ChkTrueVoid(OpensslApiDl::sslCtxSetVerify != nullptr, "openssl handler not loaded.");
        OpensslApiDl::sslCtxSetVerify(ctx, mode, cb);
    }

    static inline int SslCtxUsePrivateKey(SSL_CTX *ctx, EVP_PKEY *pkey)
    {
        ChkTrue(OpensslApiDl::usePrivKey != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::usePrivKey(ctx, pkey);
    }

    static inline int SslCtxUsePrivateKeyFile(SSL_CTX *ctx, const char *file, int type)
    {
        ChkTrue(OpensslApiDl::usePrivKeyFile != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::usePrivKeyFile(ctx, file, type);
    }

    static inline int SslCtxUseCertificateFile(SSL_CTX *ctx, const char *file, int type)
    {
        ChkTrue(OpensslApiDl::useCertFile != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::useCertFile(ctx, file, type);
    }

    static inline X509 *PemReadX509(FILE *fp, X509 **x, PEM_PASSWORD_CB *cb, void *u)
    {
        ChkTrue(OpensslApiDl::pemReadX509 != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::pemReadX509(fp, x, cb, u);
    }

    static inline void X509Free(X509 *cert)
    {
        ChkTrueVoid(OpensslApiDl::x509Free != nullptr, "openssl handler not loaded.");
        OpensslApiDl::x509Free(cert);
    }

    static inline int Asn1Time2Tm(const ASN1_TIME *s, struct tm *tm)
    {
        ChkTrue(OpensslApiDl::asn1Time2Tm != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::asn1Time2Tm(s, tm);
    }

    static inline void SslCtxSetDefaultPasswdCbUserdata(SSL_CTX *ctx, void *u)
    {
        ChkTrueVoid(OpensslApiDl::setDefaultPasswdCbUserdata != nullptr, "openssl handler not loaded.");
        OpensslApiDl::setDefaultPasswdCbUserdata(ctx, u);
    }

    static inline void SslCtxSetCertVerifyCallback(SSL_CTX *ctx, int (*cb)(X509_STORE_CTX *, void *), void *arg)
    {
        ChkTrueVoid(OpensslApiDl::setCertVerifyCallback != nullptr, "openssl handler not loaded.");
        OpensslApiDl::setCertVerifyCallback(ctx, cb, arg);
    }

    static inline int SslCtxLoadVerifyLocations(SSL_CTX *ctx, const char *cafile, const char *capath)
    {
        ChkTrue(OpensslApiDl::loadVerifyLocations != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::loadVerifyLocations(ctx, cafile, capath);
    }

    static inline int SslCtxCheckPrivateKey(const SSL_CTX *ctx)
    {
        ChkTrue(OpensslApiDl::checkPrivateKey != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::checkPrivateKey(ctx);
    }

    static inline X509 *SslGetPeerCertificate(const SSL *ssl)
    {
        ChkTrue(OpensslApiDl::sslGetPeerCertificate != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::sslGetPeerCertificate(ssl);
    }

    static inline X509 *SslCtxGet0Certificate(const SSL_CTX *ctx)
    {
        ChkTrue(OpensslApiDl::ssLCtxGet0Certificate != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::ssLCtxGet0Certificate(ctx);
    }

    static inline long SslGetVerifyResult(const SSL *ssl)
    {
        ChkTrue(OpensslApiDl::sslGetVerifyResult != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::sslGetVerifyResult(ssl);
    }

    static inline const EVP_CIPHER *EvpAes128Gcm()
    {
        ChkTrue(OpensslApiDl::evpAes128Gcm != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::evpAes128Gcm();
    }

    static inline const EVP_CIPHER *EvpAes256Gcm()
    {
        ChkTrue(OpensslApiDl::evpAes256Gcm != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::evpAes256Gcm();
    }

    static inline EVP_CIPHER_CTX *EvpCipherCtxNew()
    {
        ChkTrue(OpensslApiDl::evpCipherCtxNew != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::evpCipherCtxNew();
    }

    static inline void EvpCipherCtxFree(EVP_CIPHER_CTX *ctx)
    {
        ChkTrueVoid(OpensslApiDl::evpCipherCtxFree != nullptr, "openssl handler not loaded.");
        OpensslApiDl::evpCipherCtxFree(ctx);
    }

    static inline int EvpCipherCtxCtrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr)
    {
        ChkTrue(OpensslApiDl::evpCipherCtxCtrl != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::evpCipherCtxCtrl(ctx, type, arg, ptr);
    }

    static inline int EvpEncryptInitEx(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl,
                                       const unsigned char *key, const unsigned char *iv)
    {
        ChkTrue(OpensslApiDl::evpEncryptInitEx != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::evpEncryptInitEx(ctx, cipher, impl, key, iv);
    }

    static inline int EvpEncryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in,
                                       int inl)
    {
        ChkTrue(OpensslApiDl::evpEncryptUpdate != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::evpEncryptUpdate(ctx, out, outl, in, inl);
    }

    static inline int EvpEncryptFinalEx(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
    {
        ChkTrue(OpensslApiDl::evpEncryptFinalEx != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::evpEncryptFinalEx(ctx, out, outl);
    }

    static inline int EvpDecryptInitEx(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher, ENGINE *impl,
                                       const unsigned char *key, const unsigned char *iv)
    {
        ChkTrue(OpensslApiDl::evpDecryptInitEx != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::evpDecryptInitEx(ctx, cipher, impl, key, iv);
    }

    static inline int EvpDecryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl, const unsigned char *in,
                                       int inl)
    {
        ChkTrue(OpensslApiDl::evpDecryptUpdate != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::evpDecryptUpdate(ctx, out, outl, in, inl);
    }

    static inline int EvpDecryptFinalEx(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
    {
        ChkTrue(OpensslApiDl::evpDecryptFinalEx != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::evpDecryptFinalEx(ctx, out, outl);
    }

    static inline int RandPoll()
    {
        ChkTrue(OpensslApiDl::randPoll != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::randPoll();
    }

    static inline int RandStatus()
    {
        ChkTrue(OpensslApiDl::randStatus != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::randStatus();
    }

    static inline int RandPrivBytes(unsigned char *buf, int num)
    {
        ChkTrue(OpensslApiDl::randPrivBytes != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::randPrivBytes(buf, num);
    }

    static inline int X509VerifyCert(X509_STORE_CTX *ctx)
    {
        ChkTrue(OpensslApiDl::x509VerifyCert != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::x509VerifyCert(ctx);
    }

    static inline const char *X509VerifyCertErrorString(long n)
    {
        ChkTrue(OpensslApiDl::x509VerifyCertErrorString != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::x509VerifyCertErrorString(n);
    }

    static inline int X509StoreCtxGetError(X509_STORE_CTX *ctx)
    {
        ChkTrue(OpensslApiDl::x509StoreCtxGetError != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::x509StoreCtxGetError(ctx);
    }

    static inline X509_CRL *PemReadBioX509Crl(BIO *bp, X509_CRL **x, PEM_PASSWORD_CB *cb, void *u)
    {
        ChkTrue(OpensslApiDl::pemReadBioX509Crl != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::pemReadBioX509Crl(bp, x, cb, u);
    }

    static inline const BIO_METHOD *BioSFile()
    {
        ChkTrue(OpensslApiDl::bioSFile != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::bioSFile();
    }

    static inline EVP_PKEY *PemReadBioPk(BIO *bp, EVP_PKEY **x, PEM_PASSWORD_CB *cb, void *u)
    {
        ChkTrue(OpensslApiDl::pemReadBioPk != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::pemReadBioPk(bp, x, cb, u);
    }

    static inline BIO *BioNew(const BIO_METHOD *bioMethod)
    {
        ChkTrue(OpensslApiDl::bioNew != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::bioNew(bioMethod);
    }

    static inline BIO *BioNewMemBuf(const void *buf, int len)
    {
        ChkTrue(OpensslApiDl::bioNewMemBuf != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::bioNewMemBuf(buf, len);
    }

    static inline long BioCtrl(BIO *bp, int cmd, long larg, void *parg)
    {
        ChkTrue(OpensslApiDl::bioCtrl != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::bioCtrl(bp, cmd, larg, parg);
    }

    static inline void BioFree(BIO *b)
    {
        ChkTrueVoid(OpensslApiDl::bioFree != nullptr, "openssl handler not loaded.");
        return OpensslApiDl::bioFree(b);
    }

    static inline X509_STORE *X509StoreCtxGet0Store(const X509_STORE_CTX *ctx)
    {
        ChkTrue(OpensslApiDl::x509StoreCtxGet0Store != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::x509StoreCtxGet0Store(ctx);
    }

    static inline void X509StoreCtxSetFlags(X509_STORE_CTX *ctx, unsigned long flags)
    {
        ChkTrueVoid(OpensslApiDl::x509StoreCtxSetFlags != nullptr, "openssl handler not loaded.");
        return OpensslApiDl::x509StoreCtxSetFlags(ctx, flags);
    }

    static inline int X509StoreAddCrl(X509_STORE *xs, X509_CRL *x)
    {
        ChkTrue(OpensslApiDl::x509StoreAddCrl != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::x509StoreAddCrl(xs, x);
    }

    static inline void X509CrlFree(X509_CRL *x)
    {
        ChkTrueVoid(OpensslApiDl::x509CrlFree != nullptr, "openssl handler not loaded.");
        return OpensslApiDl::x509CrlFree(x);
    }

    static inline int X509CmpCurrentTime(const ASN1_TIME *s)
    {
        ChkTrue(OpensslApiDl::x509CmpCurrentTime != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::x509CmpCurrentTime(s);
    }

    static inline int X509CrlGet0ByCert(X509_CRL *crl, X509_REVOKED **ret, X509 *x)
    {
        ChkTrue(OpensslApiDl::x509CrlGet0ByCert != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::x509CrlGet0ByCert(crl, ret, x);
    }

    static inline const ASN1_TIME *X509CrlGet0NextUpdate(const X509_CRL *crl)
    {
        ChkTrue(OpensslApiDl::x509CrlGet0NextUpdate != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::x509CrlGet0NextUpdate(crl);
    }

    static inline ASN1_TIME *X509GetNotAfter(const X509 *x)
    {
        ChkTrue(OpensslApiDl::x509GetNotAfter != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::x509GetNotAfter(x);
    }

    static inline ASN1_TIME *X509GetNotBefore(const X509 *x)
    {
        ChkTrue(OpensslApiDl::x509GetNotBefore != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::x509GetNotBefore(x);
    }

    static inline EVP_PKEY *X509GetPubkey(X509 *x)
    {
        ChkTrue(OpensslApiDl::x509GetPubkey != nullptr, nullptr, "openssl handler not loaded.");
        return OpensslApiDl::x509GetPubkey(x);
    }

    static inline int EvpPkeyBits(const EVP_PKEY *pkey)
    {
        ChkTrue(OpensslApiDl::evpPkeyBits != nullptr, BIO_ERR, "openssl handler not loaded.");
        return OpensslApiDl::evpPkeyBits(pkey);
    }

    static inline void EvpPkeyFree(EVP_PKEY *pkey)
    {
        ChkTrueVoid(OpensslApiDl::evpPkeyFree != nullptr, "openssl handler not loaded.");
        return OpensslApiDl::evpPkeyFree(pkey);
    }

    static inline int Load(const std::string &libPath)
    {
        return OpensslApiDl::LoadOpensslApiDl(libPath);
    }

    static inline void UnLoad() {}
};
} // namespace bio
} // namespace ock
#endif // BIO_OPENSSL_API_WRAPPER_H
