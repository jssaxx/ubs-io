/******************************************************************************
     版权所有 (C) 2010 - 2010  华为赛门铁克科技有限公司
*******************************************************************************
* 版 本 号: 初稿
* 作    者: 
* 生成日期: 2010年6月21日
* 功能描述: 调试命令行头文件
* 备    注: 
* 修改记录: 
*         1)时间    : 
*          修改人  : 
*          修改内容: 
******************************************************************************/
/**
    \file  cli_def.h
    \brief 调试命令行功能，新的调试命令行机制，替代MML, 规范命令格式，提供交互式输入(确认)功能。
*/



/** @defgroup 调试命令行 */

#ifndef __CLI_DEF_H__
#define __CLI_DEF_H__

#include <unistd.h>
#include <inttypes.h>
#include <libgen.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <endian.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include<semaphore.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#if !defined(_BASIC_TYPEDEF_)
#define _BASIC_TYPEDEF_
typedef char                   s8;
typedef unsigned char          u8;

typedef short                  s16;
typedef unsigned short         u16;

typedef int                    s32;
typedef unsigned int           u32;

typedef int64_t               s64;
typedef uint64_t              u64;
#endif /* _BASIC_TYPEDEF_ */

/**
 * OSP开始的是兼容性定义，后续不要使用
 */
#ifndef _OSP_TYPES_
#define _OSP_TYPES_

/**
 * OSP_VOID类型定义
 */
typedef void            OSP_VOID;
/**
 * OSP_BOOL类型定义
 */
typedef  int            OSP_BOOL;
/**
 * OSP_ULONG类型定义
 * Linux, 在32位下是32位，在64位下是64位
 * Windows 32和64位环境都是32位
 */
typedef unsigned long   OSP_ULONG;
/**
 * OSP_LONG类型定义
 * Linux, 在32位下是32位，在64位下是64位
 * Windows 32和64位环境都是32位
 */
typedef long            OSP_LONG;
/**
 * OSP_CHAR类型定义
 */
typedef char            OSP_CHAR;


typedef int8_t   OSP_S8;
typedef int16_t  OSP_S16;
typedef int32_t  OSP_S32;
typedef int64_t  OSP_S64;

typedef uint8_t   OSP_U8;
typedef uint16_t  OSP_U16;
typedef uint32_t  OSP_U32;
typedef uint64_t  OSP_U64;

typedef int SOCKET;

#define RETURN_OK 0
#define RETURN_ERROR (-1)
#endif

#define MAX_PATH_NAME 256

#define HZ          250

#define DIAG_THRD_PRIO_HIGHEST (-20)

#define diag_msleep(ms) usleep(1000*(ms))

#define diag_sem_t  sem_t

static inline void DIAG_SEM_INIT(diag_sem_t *sem, int i)
{
    int initRet = sem_init(sem, i, 0);
        if (initRet != RETURN_OK) {
            // LOGERROR(0, "Mf init sem fail, ret(%d).", initRet);
    }
}

static inline void DIAG_SEM_DOWN(diag_sem_t *sem)
{
    int downRet;
    downRet = sem_wait(sem);
    while ((downRet != 0) && (errno == EINTR)) {
        downRet = sem_wait(sem);
    }
}

static inline void DIAG_SEM_UP(diag_sem_t *sem)
{
    int upRet = sem_post(sem);
    if (upRet != RETURN_OK) {
        // LOGERROR(0, "Mf up lwtSem failed(%d).", upRet);
    }
}

static inline void DIAG_SEM_DESTROY(diag_sem_t *sem)
{
    int destRet = sem_destroy(sem);
    if (destRet != RETURN_OK) {
        // LOGERROR(0, "Mf destory lwtSem failed(%d).", destRet);
    }
}

typedef pthread_spinlock_t DiagSpinLockType;

#define DIAG_SPIN_LOCK pthread_spin_lock
#define DIAG_SPIN_UNLOCK pthread_spin_unlock
#define DIAG_SPIN_INIT pthread_spin_init

/**
*   无效socket
*/
#define INVALID_SOCKET  (-1)

#ifndef DESC
#define DESC(x) 1 /* 文件分段描述宏  */
#endif

#ifndef UNREFERENCE_PARAM
#define UNREFERENCE_PARAM(x) ((void)(x))
#endif

/************************************原子变量*****************************************/
static inline int64_t DIAG_ATOMIC_INC(int64_t *x)
{
    return __sync_add_and_fetch(x, 1);
}
static inline int64_t DIAG_ATOMIC_DEC(int64_t *x)
{
    return __sync_sub_and_fetch(x, 1);
}
static inline int64_t DIAG_ATOMIC_ADD(int64_t *x, int64_t y)
{
    return __sync_add_and_fetch(x, y);
}
static inline int64_t DIAG_ATOMIC_SUB(int64_t *x, int64_t y)
{
    return __sync_sub_and_fetch(x, y);
}


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif  /*__CLI_DEF_H__*/

