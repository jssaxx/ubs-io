//
// Created by j30026471 on 2024/2/28.
//

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
