/* Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-io is licensed under the Mulan PSL v2. */
#include <cstdlib>
#include "ubsio_kvc_err.h"
#include "ubsio_kvc.h"
#include "bio_c.h"

extern "C" void FakeBioSetUnderFsType(BioUnderFsType type);
#include "stub_control.h"

int main()
{
    if (std::atexit([] {
        if (FakeNdsGetCallCount("nds_io_destroy_ctx") != 1 || FakeNdsGetCallCount("nds_exit") != 1) {
            std::_Exit(1);
        }
    }) != 0) {
        return 1;
    }
    FakeNdsReset();
    FakeBioSetResult("ubsio.nds.enabled", 1);
    FakeBioSetUnderFsType(BIO_UNDERFS_LOCAL);
    const auto ret = UbsioKvCacheInit(0);
    char buffer[512]{};
    if (ret != 0 || UbsioKvCacheRegisterMem(buffer, sizeof(buffer)) != 0 ||
        UbsioKvCacheUnregisterMem(buffer, sizeof(buffer)) != 0) {
        return 1;
    }
    // Deliberately rely on process-exit destruction, not explicit cleanup.
    return FakeNdsGetCallCount("nds_init") == 1 ? 0 : 1;
}
