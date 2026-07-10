#ifndef TRACER_H
#define TRACER_H

#include "trace_mark.h"

#define TRACE_DELAY_BEGIN(TP_ID)                                               \
    uint64_t traceMarkBegin##TP_ID = 0;                                        \
    if (ock::tracemark::TraceMark::IsEnable()) {                               \
        ock::tracemark::TraceMark::MarkBegin(TP_ID, #TP_ID);                   \
        traceMarkBegin##TP_ID = ock::tracemark::TraceMark::NowNs();            \
    }

#define TRACE_DELAY_END(TP_ID, RET_CODE)                                       \
    if (ock::tracemark::TraceMark::IsEnable() && traceMarkBegin##TP_ID != 0) { \
        uint64_t traceMarkEnd##TP_ID = ock::tracemark::TraceMark::NowNs();     \
        ock::tracemark::TraceMark::MarkEnd(                                    \
            TP_ID, traceMarkEnd##TP_ID - traceMarkBegin##TP_ID, RET_CODE);     \
    }

#define TRACE_ASYNC_DELAY_BEGIN(TP_ID)                                         \
    if (ock::tracemark::TraceMark::IsEnable()) {                               \
        ock::tracemark::TraceMark::MarkBegin(TP_ID, #TP_ID);                   \
    }

#define TRACE_ASYNC_DELAY_END(TP_ID, RET_CODE, START_TIME)                     \
    if (ock::tracemark::TraceMark::IsEnable()) {                               \
        uint64_t traceMarkEnd##TP_ID = ock::tracemark::TraceMark::NowNs();     \
        ock::tracemark::TraceMark::MarkEnd(                                    \
            TP_ID, traceMarkEnd##TP_ID - (START_TIME), RET_CODE);              \
    }

#endif // TRACER_H