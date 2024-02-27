/******************************************************************************
     版权所有 (C) 2010 - 2010  华为赛门铁克科技有限公司
*******************************************************************************
* 版 本 号: 初稿
* 作    者: x00001559
* 生成日期: 2010年6月30日
* 功能描述: tracepoint功能内部头文件
* 备    注: 
* 修改记录: 
*         1)时间    : 
*          修改人  : 
*          修改内容: 
******************************************************************************/
#ifndef __LVOS_TRACEPOINT_PUB_H__
#define __LVOS_TRACEPOINT_PUB_H__
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define LVOS_MAX_TRACEP_NUM   1024

#define LVOS_STR_TO_KEY_BASE_NUM   31

/* 计算Hash表大小时使用的移位次数 */
#define LVOS_MAX_TP_HASH_SHIFT 11

/* Hash表内最多包含多少Chunk, 即Hash表的大小 */
#define LVOS_MAX_TP_HASH_SIZE (1 << LVOS_MAX_TP_HASH_SHIFT)

#define LVOS_TP_LOGID    (0)

#define PID_DEBUG 100
#define PID_OSP_NULL 0
/* 用于增加hash算法的随机分布性 */
#define LVOS_GOLDEN_RATIO_PRIME 0x9e37fffffffc0001ULL
#define LVOS_MAX_HT_BITS     64

/*激活NULL回调函数时的返回值*/
#define LVOS_TP_RETURN_CALLBACK_NULL         2

/*找不到tracepoint时的返回值*/
#define LVOS_TP_RETURN_NOT_FOUND                3

/*激活配置文件*/
#if defined(__KERNEL__) || defined(__KAPI__)
#define LVOS_TP_CONFIG_FILE     "/OSM/conf/tracepoint.ini"
#define LVOS_TP_CONFIG_TMP_FILE     "/OSM/conf/tracepoint_tmp.ini"
#else
#define LVOS_TP_CONFIG_FILE     "./conf/tracepoint.ini"
#define LVOS_TP_CONFIG_TMP_FILE     "./conf/tracepoint_tmp.ini"
#endif /*__KERNEL__*/

/*配置文件一行的最大字符数*/
#define LVOS_TP_MAX_CFG_LINE   256

/*最大的初始激活Tracepoint数量*/
#define LVOS_TP_MAX_INIT_ACTIVE_NUM           512

/*初始激活配置文件顺序*/
typedef enum tagLVOS_TP_INIT_ACTIVE_E
{
	LVOS_TP_INIT_ACTIVE_NAME = 0,
	LVOS_TP_INIT_ACTIVE_TYPE = 1,
	LVOS_TP_INIT_ACTIVE_ALIVE = 2,
	LVOS_TP_INIT_ACTIVE_PARAM = 3,
	LVOS_TP_INIT_ACTIVE_BUTT = 4
}LVOS_TP_INIT_ACTIVE_E;

/*初始激活配置文件内容*/
typedef struct tagLVOS_TP_INIT_ACTIVE_S
{
    int32_t type;
    uint32_t timeAlive;
    uint64_t name;
    char param[LVOS_TRACEP_PARAM_SIZE];
}LVOS_TP_INIT_ACTIVE_S;

typedef struct tagLVOS_TP_HASH_S
{
    struct list_head stConflictList; /* 用于挂靠冲突链 */
    uint64_t ullKey2;   /* hash Key2 */
    uint32_t uiKey1;    /* hash Key1 */
    LVOS_TRACEP_NEW_S stTP;
}LVOS_TP_HASH_S;



/****************Hash表******************/
typedef struct tagLVOS_HASH_ITEM_S
{
    struct list_head stConflictList; /* 用于挂靠冲突链 */
    uint64_t ullKey2;   /* hash Key */
    uint32_t uiKey1;    /* hash Key */
}LVOS_HASH_ITEM_S;

typedef struct tagLVOS_HASH_LIST_S
{
    struct list_head  stBucketList; /* Hash表项的冲突链 */
}LVOS_HASH_LIST_S;

typedef struct tagHASH_TABLE_S
{
    LVOS_HASH_LIST_S  *pstHashElem;/* Hash表项 */
    uint32_t uiTableSize;           /* Hash表的容量 */
    uint32_t uiBits;                /* Hash算法参数 */
}LVOS_HT_S;

typedef struct tagKEY_S
{
    union   /* 该联合用于hash算法 */
    {
        uint64_t ull64bits;
        uint32_t ui32bits[2];
    }unKey64;
}KEY_S;

//extern LVOS_HT_S *g_TPHt;    /* 移走，放到使用的c文件中去 */

/* 供命令行使用 */
void LVOS_WalkTracePoint(OSP_S32 (*v_pfn)(LVOS_TRACEP_S *, void *), void *v_pParam);
void LVOS_WalkTraceHook(LVOS_TRACEP_S *v_pstTp, OSP_S32 (*v_pfn)(LVOS_TRACEP_HOOK_S *, void *), void *v_pParam);

/*HVS新框架*/
/* 创建hash表 */
int32_t LVOS_HVS_createHashTable(uint32_t v_uiHashSize, uint32_t v_uiBits, LVOS_HT_S **v_ppstHashTable);

/* 销毁hash表,并将*v_ppstHashTable置为NULL */
void LVOS_HVS_destroyHashTable(LVOS_HT_S **v_ppstHashTable);

/* 在hash表中插入新元素 */
int32_t insertHashItem(LVOS_HT_S *v_pstHashTable, LVOS_HASH_ITEM_S *v_pstData);

/* 删除hash表中指定元素 */
int32_t removeHashItem(LVOS_HT_S *v_pstHashTable, uint32_t v_uiKey1, uint64_t v_ullKey2);

/* 在hash表中查找指定元素 */
int32_t searchHashItem(LVOS_HT_S *v_pstHashTable, uint32_t v_uiKey1, uint64_t v_ullKey2, 
                                     LVOS_HASH_ITEM_S **v_ppstData);

/*遍历HASH表*/
int32_t LVOS_HVS_travelHashTable(LVOS_HT_S *v_pstHashTable, void (*fn)(LVOS_HASH_ITEM_S *, void *), void *param);

void LVOS_HVS_parseConfigFile(const char *fileName);

void LVOS_HVS_deactiveForTravel(LVOS_HASH_ITEM_S *hashdata, void *param);

void LVOS_HVS_initTracePoint(void);

void LVOS_HVS_exitTracePoint(void);
int32_t LVOS_TpShowTpAll(LVOS_HT_S *v_pstHashTable, void (*showTp)(LVOS_HASH_ITEM_S *, void *, u32), void *param);


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* end of __LVOS_TRACEPOINT_PUB_H__*/
