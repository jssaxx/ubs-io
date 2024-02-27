/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */
#include <stdio.h>
#include <unistd.h>
#include "tracepoint_common.h"

#define NS_PER_SEC 1E9

static inline unsigned long rdtsc(void)
{
    unsigned long cntvct = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r" (cntvct));
    return cntvct;
}

u64 dpax_time_getnanosec(void)
{
    u64 clkFreq;
    u64 nsPerCycle;

    asm volatile ("mrs %0, cntfrq_el0":"=r"(clkFreq)::"memory");
    nsPerCycle = (uint64_t)NS_PER_SEC/clkFreq;
    return (OSP_U64) (rdtsc() * nsPerCycle);
}

u64 dpax_time_getmillisec(void)
{
    return (u64)(dpax_time_getnanosec()/1000000);
}

s32 dpax_get_charpos(char *pstr, char chr, s32 *sipos)
{
    s32 i = 0;

    if (NULL == pstr || NULL == sipos)
    {
        return -1;
    }

    while (pstr[i] != '\0')
    {
        if (chr == pstr[i])
        {
            *sipos = i;
            return RETURN_OK;
        }

        i++;
    }

    return RETURN_ERROR;
}

void dpax_filter_newlinesymbol(char *pstr)
{
    s32 sipos = 0;

    if (unlikely(NULL == pstr))
    {
        return;
    }

    if (RETURN_OK == dpax_get_charpos(pstr, '\r', &sipos))
    {
        pstr[sipos] = '\0';
    }
    if (RETURN_OK == dpax_get_charpos(pstr, '\n', &sipos))
    {
        pstr[sipos] = '\0';
    }
}
char *dpax_readline(s32 siFd, void *pBuf, s32 siMaxSize)
{
    ssize_t ret = -1,total_bytes = 0;
    long i;
    char *buf = pBuf;

    if ((0 >= siMaxSize)||(NULL == pBuf))
    {
        return NULL;
    }
    i = 0;
    while(i < siMaxSize - 1)
    {
        ret = read(siFd, (void *)&buf[i], 1UL);
        if (ret <= 0)
        {
            buf[i] = '\0';
            break;
        }
        if ('\n' == buf[i])
        {
            buf[i+1] = '\0';
            total_bytes++;
            break;
        }

        if ('\r' == buf[i])
        {
            continue;
        }

        total_bytes++;
        i++;
    }

    buf[siMaxSize - 1] = '\0';

    return total_bytes ? buf : NULL;
}
