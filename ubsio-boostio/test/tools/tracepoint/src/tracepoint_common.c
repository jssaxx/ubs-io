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

#include <stdio.h>
#include <unistd.h>
#include "tracepoint_common.h"

#define NS_PER_SEC 1E9

static inline unsigned long Rdtsc(void)
{
    unsigned long cntvct = 0;
    asm volatile("mrs %0, cntvct_el0" : "=r" (cntvct));
    return cntvct;
}

TpUint64 DpaxTimeGetnanosec(void)
{
    TpUint64 clkFreq;
    TpUint64 nsPerCycle;

    asm volatile ("mrs %0, cntfrq_el0":"=r"(clkFreq)::"memory");
    nsPerCycle = (uint64_t)NS_PER_SEC/clkFreq;
    return (TpUint64) (Rdtsc() * nsPerCycle);
}

TpUint64 DpaxTimeGetmillisec(void)
{
    return (TpUint64)(DpaxTimeGetnanosec()/1000000);
}

TpInt32 DpaxTimeCharpos(char *pstr, char chr, TpInt32 *sipos)
{
    TpInt32 i = 0;

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

void DpaxFilterNewlinesymbol(char *pstr)
{
    TpInt32 sipos = 0;

    if (UNLIKELY(NULL == pstr))
    {
        return;
    }

    if (RETURN_OK == DpaxTimeCharpos(pstr, '\r', &sipos))
    {
        pstr[sipos] = '\0';
    }
    if (RETURN_OK == DpaxTimeCharpos(pstr, '\n', &sipos))
    {
        pstr[sipos] = '\0';
    }
}

char *DpaxReadLine(TpInt32 siFd, void *pBuf, TpInt32 siMaxSize)
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