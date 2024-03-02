/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 */


#ifndef BOOSTIO_COMMON_HTRACER_DIAGNOSE_H
#define BOOSTIO_COMMON_HTRACER_DIAGNOSE_H

namespace ock {
    namespace htracer {
        namespace diagnose {
            class HtracerCommand {
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

extern int HtracerDiagnoseInit();

#ifdef __cplusplus
}
#endif

#endif // BOOSTIO_COMMON_HTRACER_DIAGNOSE_H
