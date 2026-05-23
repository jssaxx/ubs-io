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

#ifndef BOOSTIO_SERVER_DIAGNOSE_H
#define BOOSTIO_SERVER_DIAGNOSE_H

#include "cli.h"

namespace ock {
namespace bio {
namespace diagnose {

class BioServerCommand {
public:
    static int Initialize() noexcept;
    static void Destroy() noexcept;
    static int32_t LoadSymbols();

    static void BioServerDebugProcess(int argc, char *argv[]) noexcept;
    static void BioServerDebugHelp(char *command, int detail) noexcept;
    static void HandleModifyEvictWaterLevel(uint8_t tier, uint64_t level);
    static void HandleModifyEvictMemQuantity(uint64_t quantity);
    static void HandleModifyEvictDiskQuantity(uint64_t quantity);
    static void HandleModifyMemReadWriteRatio(const std::string &ratios);
    static void HandleModifyDiskReadWriteRatio(const std::string &ratios);
    static void BioServerHandleShow(const std::vector<std::string> &cmds);
    static void HandleServerTrace(const std::vector<std::string> &cmds);
    static void HandleRCachePut(const std::vector<std::string> &cmds);
    static void HandleRCacheGet(const std::vector<std::string> &cmds);
    static void HandleRCacheDelete(const std::vector<std::string> &cmds);

private:
    static bool mInited;
    static void *mHandler;
    static CliRegCmdFuncPtr mRegOp;
    static CliUnRegCmdFuncPtr mUnRegOp;
    static CliPrintBufFuncPtr mPrintOp;
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
