/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */

#ifndef BOOSTIO_SDK_DIAGNOSE_H
#define BOOSTIO_SDK_DIAGNOSE_H

namespace ock {
    namespace bio {
        namespace diagnose {
            class BioSdkCommand {
            public:
                static int Initialize() noexcept;
                static void Destroy() noexcept;
            };
        }
    }
}

#ifdef __cplusplus
extern "C" {
#endif

extern int SdkDiagnoseInit();

#ifdef __cplusplus
}
#endif

#endif //BOOSTIO_SDK_DIAGNOSE_H
