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

#ifndef BOOSTIO_SDK_DIAGNOSE_H
#define BOOSTIO_SDK_DIAGNOSE_H

#include "cli_define.h"

namespace ock {
namespace bio {
namespace diagnose {
class BioSdkCommand {
public:
    static int Initialize() noexcept;
    static void Destroy() noexcept;
    static int32_t LoadSymbols();

    static void BioSdkDebugProcess(int argc, char *argv[]) noexcept;
    static void BioSdkDebugHelp(char *command, int detail) noexcept;
    static void HandleListCache();
    static void HandleCreate(const std::vector<std::string> &cmds);
    static void HandleOpen(const std::vector<std::string> &cmds);
    static void HandleDestroy(const std::vector<std::string> &cmds);
    static void HandlePut(const std::vector<std::string> &cmds);
    static void HandleGet(const std::vector<std::string> &cmds);
    static void HandleList(const std::vector<std::string> &cmds);
    static void HandleStat(const std::vector<std::string> &cmds);
    static void HandleLoad(const std::vector<std::string> &cmds);
    static void HandleDelete(const std::vector<std::string> &cmds);
    static void HandleAddDisk(const std::vector<std::string> &cmds);
    static void HandleShow(const std::vector<std::string> &cmds);
    static void HandleShowCacheHit(const std::vector<std::string> &cmds);
    static void HandleShowCacheResource(const std::vector<std::string> &cmds);
    static void HandleNotifyUpdatePrepare(const std::vector<std::string> &cmds);
    static void HandleNotifyUpdateFinish(const std::vector<std::string> &cmds);
    static void HandleCheckUpdateReady(const std::vector<std::string> &cmds);
    static void HandleSdkTrace(const std::vector<std::string> &cmds);
    static void *PerfTestPutImpl(void *param);
    static void *PerfTestGetImpl(void *param);
    static void HandlePerf(const std::vector<std::string> &cmds);

private:
    static bool mInited;
    static void *mHandler;
    static CliRegCmdFuncPtr mRegOp;
    static CliUnRegCmdFuncPtr mUnRegOp;
    static CliPrintBufFuncPtr mPrintOp;
};
} // namespace diagnose
} // namespace bio
} // namespace ock

#ifdef __cplusplus
extern "C" {
#endif

extern int SdkDiagnoseInit();

#ifdef __cplusplus
}
#endif

#endif // BOOSTIO_SDK_DIAGNOSE_H
