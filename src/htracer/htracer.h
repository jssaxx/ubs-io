/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef HTRACER_H
#define HTRACER_H

#include "common/htracer_monotonic.h"
#include "manager/htracer_manager.h"

namespace ock {
namespace htracer {
enum HResult {
    RET_OK = 0,
    RET_ERR = -1,
    RET_INVALID_PARAM = -2,
};

constexpr int32_t SERVICE_SHIFT = 16;

#define TRACE_DELAY_BEGIN(TP_ID)                                 \
    uint64_t tsBegin##TP_ID = ock::utils::Monotonic::TimeNs();   \
    if (ock::htracer::HtracerManager::IsEnable()) {              \
        ock::htracer::HtracerManager::DelayBegin(TP_ID, #TP_ID); \
    }

#define TRACE_DELAY_END(TP_ID, RET_CODE)                                        \
    if (ock::htracer::HtracerManager::IsEnable()) {                             \
        uint64_t tsEnd##TP_ID = ock::utils::Monotonic::TimeNs();                \
        uint64_t tpDiff##TP_ID = tsEnd##TP_ID - tsBegin##TP_ID;                 \
        ock::htracer::HtracerManager::DelayEnd(TP_ID, tpDiff##TP_ID, RET_CODE); \
    }

#define TRACE_ASYNC_DELAY_BEGIN(TP_ID)                           \
    if (ock::htracer::HtracerManager::IsEnable()) {              \
        ock::htracer::HtracerManager::DelayBegin(TP_ID, #TP_ID); \
    }

#define TRACE_ASYNC_DELAY_END(TP_ID, RET_CODE, START_TIME)                      \
    if (ock::htracer::HtracerManager::IsEnable()) {                             \
        uint64_t tsEnd##TP_ID = ock::utils::Monotonic::TimeNs();                \
        uint64_t tpDiff##TP_ID = tsEnd##TP_ID - (START_TIME);                   \
        ock::htracer::HtracerManager::DelayEnd(TP_ID, tpDiff##TP_ID, RET_CODE); \
    }

int32_t HTracerInit(const std::string &dumpDir);

void HTracerExit();

std::string GetTraceInfo();

void ClearTraceInfo();

void HTracerSetEnable(bool isEnable);

void HTracerDumpSetEnable(bool isEnable);

constexpr int32_t GetTraceId(uint32_t serviceId, uint32_t innerId)
{
    return ((serviceId << SERVICE_SHIFT) | (innerId & 0xFFFF));
}
}
}
#endif // HTRACER_H
