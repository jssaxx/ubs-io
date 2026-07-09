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

#include "htracer.h"
#include "htracer_c.h"
#include "htracer_service.h"

namespace ock {
namespace htracer {
bool HtracerManager::mEnable = true;

int32_t HTracerInit(const std::string &dumpDir)
{
    auto &service = HTracerService::GetInstance();
    if (service.StartUp(dumpDir) != RET_OK) {
        return RET_ERR;
    }
    return RET_OK;
}

void HTracerExit()
{
    auto &service = HTracerService::GetInstance();
    service.ShutDown();
}

std::string GetTraceInfo()
{
    auto &service = HTracerService::GetInstance();
    return service.GetTraceInfo();
}

void ClearTraceInfo()
{
    auto &service = HTracerService::GetInstance();
    return service.ClearTraceInfo();
}

void HTracerSetEnable(bool isEnable)
{
    HtracerManager::mEnable = isEnable;
}

void HTracerDumpSetEnable(bool isEnable)
{
    HTracerService::mDumpEnable = isEnable;
}

}
}

extern "C" bool HTracerIsEnableC(void)
{
    return ock::htracer::HtracerManager::IsEnable();
}

extern "C" uint64_t HTracerNowNsC(void)
{
    return ock::utils::Monotonic::TimeNs();
}

extern "C" void HTracerDelayBeginC(int32_t tpId, const char *tpName)
{
    if (tpName == nullptr) {
        return;
    }
    std::string name(tpName);
    ock::htracer::HtracerManager::DelayBegin(tpId, name);
}

extern "C" void HTracerDelayEndC(int32_t tpId, uint64_t startNs, int32_t retCode)
{
    if (startNs == 0) {
        return;
    }
    uint64_t endNs = ock::utils::Monotonic::TimeNs();
    ock::htracer::HtracerManager::DelayEnd(tpId, endNs - startNs, retCode);
}
