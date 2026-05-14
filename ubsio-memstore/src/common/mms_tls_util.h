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

#ifndef MMS_TLS_UTIL_H
#define MMS_TLS_UTIL_H

#include <algorithm>
#include <cstdint>
#include <dlfcn.h>

namespace ock {
namespace mms {

using DecryptFunc = int (*)(const char *cipherText, const size_t cipherTextLen, char *plainText, size_t *plainTextLen);

class TlsUtil {
public:
    static inline int32_t DefaultDecrypter(const char *cipherText, const size_t cipherTextLen, char *plainText,
                                           const size_t plainTextLen)
    {
        std::copy_n(cipherText, plainTextLen, plainText);
        return 0;
    }

    static inline void **GetTlsLibHandler()
    {
        static void *decryptLibHandle = nullptr;
        return &decryptLibHandle;
    }

    static inline DecryptFunc LoadDecryptFunction(const char *decrypterLibPath)
    {
        void **decryptLibHandlePtr = GetTlsLibHandler();

        if (*decryptLibHandlePtr == nullptr) {
            *decryptLibHandlePtr = dlopen(decrypterLibPath, RTLD_LAZY);
        }

        if (*decryptLibHandlePtr != nullptr) {
            const auto decryptFunc = (DecryptFunc)dlsym(*decryptLibHandlePtr, "DecryptPassword");
            if (decryptFunc != nullptr) {
                return decryptFunc;
            } else {
                CloseTlsLib();
                return nullptr;
            }
        }

        return nullptr;
    }

    static inline void CloseTlsLib()
    {
        void **decryptLibHandlePtr = GetTlsLibHandler();
        if (*decryptLibHandlePtr != nullptr) {
            dlclose(*decryptLibHandlePtr);
            *decryptLibHandlePtr = nullptr;
        }
    }
};
} // namespace mms
} // namespace ock

#endif // MMS_TLS_UTIL_H

