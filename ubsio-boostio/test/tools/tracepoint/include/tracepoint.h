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

#ifndef BIO_TRACEPOINT_H
#define BIO_TRACEPOINT_H

#include <stdlib.h>

#include "tracepoint_type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BIO_MAX_HOOK_PER_TRACEP  16

#define BIO_TRACEP_STAT_DELETED   0
#define BIO_TRACEP_STAT_ACTIVE    1
#define BIO_TRACEP_STAT_DEACTIVE  2

#define BIO_TRACEP_PARAM_SIZE     32UL
#define BIO_TRACEPOINT_NOT_REGISTERED 1
#define BIO_TRACEPOINT_MODID 381

#ifndef MAX_NAME_LEN
#define MAX_NAME_LEN 128
#endif
#ifndef MAX_DESC_LEN
#define MAX_DESC_LEN 256
#endif

typedef enum TracepointType
{
    BIO_TP_TYPE_CALLBACK = 0,
    BIO_TP_TYPE_RESET,
    BIO_TP_TYPE_PAUSE,
    BIO_TP_TYPE_ABORT,
    BIO_TP_TYPE_BUTT
} BioTpType;

typedef struct
{
    TpChar tpParamData[BIO_TRACEP_PARAM_SIZE];
} BioTracepointParam;

typedef void (*FuncTracepointCommon)(BioTracepointParam *, ...);

typedef struct
{
    TpChar  szName[MAX_NAME_LEN];
    TpChar  szDesc[MAX_DESC_LEN];
    TpInt32   iId;
    TpInt32   iActive;
    TpInt32   iDbgOnly;
    FuncTracepointCommon fnHook;
} BioTracepointHook;

typedef struct
{
    TpChar  szName[MAX_NAME_LEN];
    TpChar  szDesc[MAX_DESC_LEN];
    TpUint32   uiPid;
    TpInt32   iId;
    TpInt32   iActive;
    TpInt32   iDbgOnly;
    BioTracepointParam stParam;
    BioTracepointHook  stHooks[BIO_MAX_HOOK_PER_TRACEP];
} BioTracepoints;

typedef struct TagBioTracepointNews
{
    char szName[MAX_NAME_LEN];
    char szDesc[MAX_DESC_LEN];
    uint32_t uiPid;
    int32_t iActive;
    int32_t type;
    uint32_t timeAlive;
    uint32_t timeCalled;
    FuncTracepointCommon fnHook;
    BioTracepointParam stParam;
} BioTracepointNews;

#define BIO_TRACEP_DEF0(tracep_name) \
    typedef void (*FN_TRACEP_T_##tracep_name)(BioTracepointParam *);

#define BIO_TRACEP_DEFN(tracep_name, ...) \
    typedef void (*FN_TRACEP_T_##tracep_name)(BioTracepointParam *, __VA_ARGS__);

#define BIO_TRACEP_CALL(tracep_name, ...)                                       \
do                                                                              \
{                                                                               \
    static BioTracepoints *_pstTp = NULL;                                       \
    static TpInt32   _iId = 0;                                                  \
    static TpInt32 _tracepointNotRegistered = 0;                                \
    TpUint32 _i;                                                                \
    if (UNLIKELY(NULL == _pstTp || _pstTp->iId != _iId))                        \
    {                                                                           \
        _pstTp = BioFindTracePoint(MY_PID, #tracep_name);                       \
        if (NULL == _pstTp)                                                     \
        {                                                                       \
            if(0 == _tracepointNotRegistered)                                   \
            {                                                                   \
                _tracepointNotRegistered = BIO_TRACEPOINT_NOT_REGISTERED;       \
                BIO_LOG_WARNING(BIO_LOGID_BUTT, "tracepoint `%s` not register", #tracep_name); \
            }                                                                   \
            break;                                                              \
        }                                                                       \
        else                                                                    \
        {                                                                       \
            _iId = _pstTp->iId;                                                 \
        }                                                                       \
    }                                                                           \
    if (_pstTp->iActive != BIO_TRACEP_STAT_ACTIVE)                              \
    {                                                                           \
        break;                                                                  \
    }                                                                           \
    for (_i = 0; _i < BIO_MAX_HOOK_PER_TRACEP; _i++)                            \
    {                                                                           \
        if ((_pstTp->stHooks[_i].iActive == BIO_TRACEP_STAT_ACTIVE)             \
            && (NULL != _pstTp->stHooks[_i].fnHook))                            \
        {                                                                       \
            FN_TRACEP_T_##tracep_name fn = (FN_TRACEP_T_##tracep_name)_pstTp->stHooks[_i].fnHook; \
            fn(__VA_ARGS__);                                                    \
        }                                                                       \
    }                                                                           \
}while(0)

#define BIO_TRACEPHOOK_CALL(tracep_name, fn, ...)                               \
do                                                                              \
{                                                                               \
    static BioTracepoints *_pstTp = NULL;                                       \
    static BioTracepointHook *_pstHook = NULL;                                  \
    static TpInt32   _iId = 0;                                                  \
    static TpInt32   _iHookId = 0;                                              \
    static TpInt32 _tracepointNotRegistered = 0;                                \
    if (UNLIKELY(NULL == _pstTp || _pstTp->iId != _iId))                        \
    {                                                                           \
        _pstTp = BioFindTracePoint(MY_PID, #tracep_name);                       \
        if (NULL == _pstTp)                                                     \
        {                                                                       \
            if(0 == _tracepointNotRegistered)                                   \
            {                                                                   \
                _tracepointNotRegistered = BIO_TRACEPOINT_NOT_REGISTERED;       \
                BIO_LOG_WARNING(BIO_LOGID_BUTT, "tracepoint `%s` not register", #tracep_name); \
            }                                                                   \
            break;                                                              \
        }                                                                       \
        else                                                                    \
        {                                                                       \
            _iId = _pstTp->iId;                                                 \
        }                                                                       \
    }                                                                           \
    if (_pstTp->iActive != BIO_TRACEP_STAT_ACTIVE)                              \
    {                                                                           \
        break;                                                                  \
    }                                                                           \
    if (UNLIKELY(NULL == _pstHook || _pstHook->iId != _iHookId))                \
    {                                                                           \
        _pstHook = BioFindTraceHook(_pstTp, #fn);                               \
        if (NULL == _pstHook)                                                   \
        {                                                                       \
            BIO_LOG_WARNING(BIO_LOGID_BUTT, "hook `%s`not found", #fn);         \
            break;                                                              \
        }                                                                       \
        else                                                                    \
        {                                                                       \
            _iHookId = _pstHook->iId;                                           \
        }                                                                       \
    }                                                                           \
    if ((_pstHook->iActive == BIO_TRACEP_STAT_ACTIVE)                           \
        && (NULL != _pstHook->fnHook))                                          \
    {                                                                           \
        FN_TRACEP_T_##tracep_name _fn = (FN_TRACEP_T_##tracep_name)_pstHook->fnHook; \
        _fn(__VA_ARGS__);                                                       \
    }                                                                           \
}while(0)

#define BIO_TRACEP_CALL0(tracep_name)  BIO_TRACEP_CALL(tracep_name, &_pstTp->stParam)

#define BIO_TRACEP_CALLN(tracep_name, ...)  BIO_TRACEP_CALL(tracep_name, &_pstTp->stParam, __VA_ARGS__)

#define BIO_TRACEPHOOK_CALL0(tracep_name, fn)      BIO_TRACEPHOOK_CALL(tracep_name, fn, &_pstTp->stParam)

#define BIO_TRACEPHOOK_CALLN(tracep_name, fn, ...) BIO_TRACEPHOOK_CALL(tracep_name, fn, &_pstTp->stParam, __VA_ARGS__)

#define BIO_TRACEP_REG_POINT(tracep_name, desc, flag)   BioRegTracePoint(MY_PID, #tracep_name, desc, flag, FALSE)

#define BIO_TRACEP_UNREG_POINT(tracep_name)       BioUnRegTracePoint(MY_PID, #tracep_name)

#define BIO_TRACEP_ACTIVE(tracep_name, flag)      BioActiveTracePoint(MY_PID, #tracep_name, flag)

#define BIO_TRACEP_ADD_HOOK(tracep_name, fn, desc, flag)                              \
    do                                                                                \
    {                                                                                 \
        FN_TRACEP_T_##tracep_name _Hookfn = fn;                                       \
        BioAddTracePointHook(MY_PID, #tracep_name, #fn, (FuncTracepointCommon)_Hookfn, desc, flag, FALSE);\
    } while(0)

#define BIO_TRACEP_DEL_HOOK(tracep_name, fn)                                          \
    do                                                                                \
    {                                                                                 \
        FN_TRACEP_T_##tracep_name _Hookfn = fn;                                       \
        UNREFERENCE_PARAM(_Hookfn);                                                   \
        BioDelTracePointHook(MY_PID, #tracep_name, #fn);                             \
    } while (0)

#define BIO_TRACEP_HOOK_ACTIVE(tracep_name, fn, flag) BioActiveTracePointHook(MY_PID, #tracep_name, #fn, flag)

#define BIO_TRACEP_DEF0_D(tracep_name)                BIO_TRACEP_DEF0(tracep_name)

#define BIO_TRACEP_DEFN_D(tracep_name, ...)           BIO_TRACEP_DEFN(tracep_name, __VA_ARGS__)

#define BIO_TRACEP_CALL0_D(tracep_name)               BIO_TRACEP_CALL0(tracep_name)

#define BIO_TRACEP_CALLN_D(tracep_name, ...)          BIO_TRACEP_CALLN(tracep_name, __VA_ARGS__)

#define BIO_TRACEPHOOK_CALL0_D(tracep_name, fn)       BIO_TRACEPHOOK_CALL0(tracep_name, fn)

#define BIO_TRACEPHOOK_CALLN_D(tracep_name, fn, ...)  BIO_TRACEPHOOK_CALLN(tracep_name, fn, __VA_ARGS__)

#define BIO_TRACEP_REG_POINT_D(tracep_name, desc, flag)     BioRegTracePoint(MY_PID, #tracep_name, desc, flag, TRUE)

#define BIO_TRACEP_UNREG_POINT_D(tracep_name)         BIO_TRACEP_UNREG_POINT(tracep_name)

#define BIO_TRACEP_ACTIVE_D(tracep_name, flag)        BIO_TRACEP_ACTIVE(tracep_name, flag)

#define BIO_TRACEP_ADD_HOOK_D(tracep_name, fn, desc, flag)                                               \
    do                                                                                                    \
    {                                                                                                     \
        FN_TRACEP_T_##tracep_name _Hookfn = fn;                                                           \
        BioAddTracePointHook(MY_PID, #tracep_name, #fn, (FuncTracepointCommon)_Hookfn, desc, flag, TRUE); \
    } while(0)

#define BIO_TRACEP_DEL_HOOK_D(tracep_name, fn)           BIO_TRACEP_DEL_HOOK(tracep_name, fn)

#define BIO_TRACEP_HOOK_ACTIVE_D(tracep_name, fn, flag)  BIO_TRACEP_HOOK_ACTIVE(tracep_name, fn, flag)

#define BIO_TP_REG(name, desc, fn)    BioHvsRegTracePoint(MY_PID, #name, desc, (FuncTracepointCommon)fn)

#define BIO_TP_UNREG(name)            BioHvsUnregTracePoint(PID_OSP_NULL, #name)

#define BIO_TP_START_ALL(name, ...)                                                           \
          do                                                                                  \
          {                                                                                   \
            static BioTracepointNews *_pstTp = NULL;                                          \
            static TpInt32 _tracepointNotRegistered = 0;                                      \
            TpInt32 _ret = BioTpStartStub(#name, &_pstTp, &_tracepointNotRegistered);         \
            if (_ret >= 0 && _pstTp->type != BIO_TP_TYPE_PAUSE) {                             \
                if (_pstTp->type == BIO_TP_TYPE_CALLBACK)                                     \
                {                                                                             \
                    _pstTp->fnHook(&_pstTp->stParam, __VA_ARGS__);                            \
                    _pstTp->timeCalled++;                                                     \
                }                                                                             \
                else if (_pstTp->type == BIO_TP_TYPE_ABORT)                                   \
                {                                                                             \
                    raise(9);                                                                 \
                }                                                                             \
                else if (_pstTp->type == BIO_TP_TYPE_RESET)                                   \
                {                                                                             \
                    DpaxReboot((unsigned long)BSP_SOFT_REBOOT, BIO_TRACEPOINT_MODID, (char *)"tracepoint reboot");    \
                }                                                                             \
                if(_ret == 0) {                                                               \
                    BioHvsDeactiveTracePoint(PID_OSP_NULL, #name);                            \
                }                                                                             \
            }                                                                                 \
            else {                                                                            \
                if (_ret >= 0 && _pstTp->type == BIO_TP_TYPE_PAUSE)                           \
                {                                                                             \
                    BioHvsDoTracepointPause(_pstTp);                                          \
                    _pstTp->timeCalled++;                                                     \
                    if(_ret == 0) {                                                           \
                        BioHvsDeactiveTracePoint(PID_OSP_NULL, #name);                        \
                    }                                                                         \
                }                                                                             \

#define BIO_TP_START(name, ...)                                                               \
          do                                                                                  \
          {                                                                                   \
            static BioTracepointNews *_pstTp = NULL;                                          \
            static TpInt32 _tracepointNotRegistered = 0;                                      \
            TpInt32 _ret = BioTpStartStub(#name, &_pstTp, &_tracepointNotRegistered);         \
            if (_ret >= 0 && _pstTp->type != BIO_TP_TYPE_PAUSE) {                             \
                if (_pstTp->type == BIO_TP_TYPE_CALLBACK)                                     \
                {                                                                             \
                    _pstTp->fnHook(&_pstTp->stParam, __VA_ARGS__);                            \
                    _pstTp->timeCalled++;                                                     \
                }                                                                             \
                if(_ret == 0) {                                                               \
                    BioHvsDeactiveTracePoint(PID_OSP_NULL, #name);                            \
                }                                                                             \
            }                                                                                 \
            else {

#define BIO_TP_NOPARAM_START(name)                                                            \
        do                                                                                    \
        {                                                                                     \
            static BioTracepointNews *_pstTp = NULL;                                          \
            static TpInt32 _tracepointNotRegistered = 0;                                      \
            TpInt32 _ret = BioTpStartStub(#name, &_pstTp, &_tracepointNotRegistered);         \
            if (_ret >= 0 && _pstTp->type != BIO_TP_TYPE_PAUSE) {                             \
                if (_pstTp->type == BIO_TP_TYPE_CALLBACK)                                     \
                {                                                                             \
                    _pstTp->fnHook(&_pstTp->stParam);                                         \
                    _pstTp->timeCalled++;                                                     \
                }                                                                             \
                else if (_pstTp->type == BIO_TP_TYPE_ABORT)                                   \
                {                                                                             \
                    raise(9);                                                                 \
                }                                                                             \
                else if (_pstTp->type == BIO_TP_TYPE_RESET)                                   \
                {                                                                             \
                    DpaxReboot((unsigned long)BSP_SOFT_REBOOT, BIO_TRACEPOINT_MODID, (char *)"tracepoint reboot");    \
                }                                                                             \
                if(_ret == 0) {                                                               \
                    BioHvsDeactiveTracePoint(PID_OSP_NULL, #name);                            \
                }                                                                             \
           }                                                                                  \
           else {                                                                             \
              if (_ret >=0  && _pstTp->type == BIO_TP_TYPE_PAUSE)                             \
              {                                                                               \
                  BioHvsDoTracepointPause(_pstTp);                                            \
                  _pstTp->timeCalled++;                                                       \
                  if(_ret == 0) {                                                             \
                      BioHvsDeactiveTracePoint(PID_OSP_NULL, #name);                          \
                  }                                                                           \
              }                                                                               \

#define BIO_TP_END     \
        }               \
    }while(0);

BioTracepoints *BioFindTracePoint(TpUint32 uiPid, const TpChar *szName);

void BioRegTracePoint(TpUint32 uiPid, const TpChar *szName, const TpChar *szDesc, TpInt32 iInitState,
    TpInt32 iDbgOnly);
void BioUnRegTracePoint(TpUint32 uiPid, const TpChar *szName);

void BioAddTracePointHook(TpUint32 uiPid, const TpChar *szName, const TpChar *szHookName,
    FuncTracepointCommon fnHook, const TpChar *szDesc, TpInt32 iInitState, TpInt32 iDbgOnly);
void BioDelTracePointHook(TpUint32 uiPid, const TpChar *szName, const TpChar *szHookName);

void BioActiveTracePoint(TpUint32 uiPid, const TpChar *szName, TpInt32 iFlag);
void BioActiveTracePointHook(TpUint32 uiPid, const TpChar *szName, const TpChar *szHookName, TpInt32 iFlag);
BioTracepointHook *BioFindTraceHook(BioTracepoints *pstTp, const TpChar *szName);
TpInt32 BioTpStartStub(const char *name, BioTracepointNews **pstTp, TpInt32 *tracepointNotRegistered);


void BioHvsDoTracepointPause(BioTracepointNews *tracepoint);
int32_t BioHvsGetTracePoint(uint32_t pid, const char *name, BioTracepointNews **tracepoint);
int32_t BioHvsRegTracePoint(uint32_t pid, const char *name, const char *desc, FuncTracepointCommon fnHook);
int32_t BioHvsUnregTracePoint(uint32_t pid, const char *name);
int32_t BioHvsActiveTracePoint(uint32_t pid, const char *name, int32_t type, uint32_t time,
    BioTracepointParam userParam);
int32_t BioHvsDeactiveTracePoint(uint32_t pid, const char *name);
int32_t BioHvsDeactiveTracePointAll(void);

void TracePointInit(void);

void TracePointExit(void);

#ifdef __cplusplus
}

#endif

#endif // BIO_TRACEPOINT_H