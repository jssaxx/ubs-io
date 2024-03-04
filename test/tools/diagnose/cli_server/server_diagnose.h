/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2020-2020. All rights reserved.
 */

#ifndef BOOSTIO_SERVER_DIAGNOSE_H
#define BOOSTIO_SERVER_DIAGNOSE_H

namespace ock {
    namespace bio {
        namespace diagnose {
            class BioServerCommand {
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

extern int ServerDiagnoseInit();

#ifdef __cplusplus
}
#endif

#endif //BOOSTIO_SERVER_DIAGNOSE_H
