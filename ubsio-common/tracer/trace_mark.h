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

#ifndef TRACEMARK_H
#define TRACEMARK_H

#include <cstdint>
#include <string>

namespace ock {
namespace tracemark {

constexpr uint32_t TRACE_MARK_SERVICE_SHIFT = 16;

constexpr int32_t GetTraceId(uint32_t serviceId, uint32_t innerId)
{
    return static_cast<int32_t>((serviceId << TRACE_MARK_SERVICE_SHIFT) | (innerId & 0xFFFFU));
}

class TraceMark {
public:
    static void Init();
    static void SetDumpFile(std::string &dumpFile, uint16_t interval);
    static std::string GetTraceInfo();
    static void ClearTrace();
    static void SetEnable(bool isEnable);

    static bool IsEnable();
    static uint64_t NowNs();
    static void MarkBegin(int32_t traceId, const char *traceName);
    static void MarkEnd(int32_t traceId, uint64_t latencyNs, int32_t retCode);

private:
    TraceMark() = delete;
    ~TraceMark() = delete;
};

}
}
#endif // TRACEMARK_H
