#ifndef _BB_CLI_H_
#define _BB_CLI_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

int32_t CLI_AgentInit(uint32_t uiCurPid, char *pAppName);


/*****************************************************************************
* 功    能: 处理同步状态命令
* 输入参数: 
* 输出参数: 无
* 返回值  : 
* 其 他   : 
*****************************************************************************/
void CLI_AgtSyncStatus(void);

/*****************************************************************************
* 函 数 名: CLI_AgentDestroy
* 功能描述: cli Agent端销毁接口
* 输入参数: @uiCurPid agent端id
* 输出参数: 无
* 返 回 值: 成功返回RETURN_OK，失败返回RETURN_ERROR
* 其    他: 禁止在cli上下文使用
*****************************************************************************/
int32_t CLI_AgentDestroy(uint32_t uiCurPid);

#define CLI_RET_UNKNOWN_ARG   1

#define CLI_MAX_COMMAND_LEN   20
#define CLI_MAX_CMD_DESC_LEN  64

#define CLI_MAX_ERR_MSG_LEN   80

typedef void (*FN_CLI_CMD_PROC)(int32_t v_iArgc, char *v_szArgv[]);
typedef void (*FN_CLI_CMD_HELP_PROC)(char *v_szCommand, int32_t iShowDetail);

/** \ingroup
      调试命令注册结构。
*/
typedef struct
{
    char szCommand[CLI_MAX_COMMAND_LEN];              /**< 命令字, 长度[1,15]。 */
    char szDescription[CLI_MAX_CMD_DESC_LEN];         /**< 命令的简要描述。*/
    FN_CLI_CMD_PROC fnCmdDo;             /**< 命令执行函数，v_szArgv[0]是命令字。 */
    FN_CLI_CMD_HELP_PROC fnPrintCmdHelp; /**< 命令打印帮助的函数。 */
} CLI_CMD_S;

/** \ingroup
     调试命令参数解析时使用的结构。 
	 */
typedef struct
{
    char *szOptArg; /**< 当选项包含参数内容时，该项指向参数内容。 */
    int32_t  iOptIndex; /**< 当前处理的argc索引。 */
    char chOpt;     /**< 当发现无效选项的时候，该值包含无效的选项字符。 */
    char szErrMsg[CLI_MAX_ERR_MSG_LEN]; /**< 出错时传出错误信息。 */
} CLI_OPT_S;


/**
 *@ingroup
 *
 *@par 描述:
 * 调试命令打印输出接口。
 *
 *@attention 
 *1、该函数只能在调试模块调用fnCmdDo线程上下文时调用，不能在其他线程调用。\n
 *2、该函数不能在自旋锁加锁范围中调用，需要在加锁中调用请使用\ref CLI_PrintBuf。\n
 *3、该函数和DBG_Log不一样，不会自动在后面加换行。
 *4、在循环中频繁调用此接口，为防止丢包，建议短暂休眠或者使用\ref CLI_PrintBuf。\n
 *
 *@param v_pchFormat   [IN]  参数输出的格式，同标准库函数printf。举例：“This is a test,the return value is：%s”,$Value1。
 *@param ...   [IN]  输入参数同标准库函数printf。举例：“This is a test,the return value is：%s”,$Value1。
 *

 *
 *@par
 *
 *@since  
 *@see  CLI_PrintBuf
 */
void CLI_Print(const char *v_pchFormat, ...);

/**
 *@ingroup VOS_DIAG
 *
 *@par 描述:
 * 调试命令打印输出接口，与CLI_Print不同的是它输出到临时缓冲区，可以在自旋锁中使用。
 *
 *@attention 
 *1、该函数只能在调试模块调用fnCmdDo线程上下文时调用，不能在其他线程调用。\n
 *2、由于缓冲区有使用限制(小于2K)，超出缓冲区信息就会丢失，需要使用者注意。\n
 *3)调用\ref CLI_Print会自动将缓冲区中的内容传回客户端。
 *4、在频繁打印数据的场景下建议使用。\n
 *@param v_pchFormat   [IN]  参数输出的格式，同标准库函数printf。举例：“This is a test,the return value is：%s”,$Value1。
 *@param ...   [IN]  输入参数同标准库函数printf。举例：“This is a test,the return value is：%s”,$Value1。
 *

 *
 *@par 依赖：
 *
 *@since  
 *@see  CLI_Print
 */
void CLI_PrintBuf(const char *v_pchFormat, ...);

/**
 *@ingroup
 *
 *@par 描述:
 * 将调试打印信息输出到临时缓冲区。
 *
 *@attention 
 *1)该函数只能在调试模块调用fnCmdDo线程上下文时调用，不能在其他线程调用。\n
 *2)该函数不能在自旋锁加锁范围中调用。
 *
 *@param v_pchBuf   [IN]  缓冲区地址，取值：非空。
 *@param v_uiSize   [IN]  缓冲区长度，取值：非0。
 *

 *
 *@par 依赖：
 *
 *@since   
 */
void CLI_SendBuf(const char *v_pchBuf, uint32_t v_uiSize);


/**
 *@ingroup 
 *
 *@par 描述:
 * 当调试命令输入出错时，调用该接口输出简要的错误信息，提示用户。
 *
 *@attention 
 *该函数只能在调试模块调用fnCmdDo线程上下文时调用，不能在其他线程调用。
 *
 *@param v_pchFormat   [IN]   参数输出的格式，同标准库函数printf。pchFormat为NULL时仅输出简要用法信息。举例：“This is a test,the return value is：%s”,$Value1。
 *@param ...   [IN]  输入参数同标准库函数printf。举例：“This is a test,the return value is：%s”,$Value1。
 *

 *
 *@par 依赖：
 *
 *@since  
 */
void CLI_ShowUsageAndErrMsg(const char *v_pchFormat, ...);

/**
 *@ingroup
 *
 *@par 描述:
 * 交互式输入。根据输出的提示信息，用户进行相对应的输入。
 *
 *@attention 
 *1、该函数只能在调试模块调用fnCmdDo线程上下文时调用，不能在其他线程调用。\n
 *2、该函数会一直阻塞直到客户端退出或者有用户输入为止。
 *
 *@param v_szPrompt   [IN]  交互式输入的提示符。取值：最长128字节，包括结束'\0'符。
 *@param v_szInput   [OUT]  保存获取到的交互式输入字符串。取值：非空。
 *@param v_uiMaxInputLen   [IN]  获取交互式输入的缓冲区长度，目前支持的最大长度为63有效字符(不包含'\\0')。
 *
 *@retval RETURN_OK 0 正确获取到用户输入。
 *@retval RETURN_ERROR -1  没有获取到输入(比如客户端退出了)。
 *
 *@par 依赖：
 *
 *@since 
 */
int32_t CLI_GetInput(char *v_szPrompt, char *v_szInput, uint32_t v_uiMaxInputLen);

/**
 *@ingroup
 *
 *@par 描述:
 * 命令注册接口，向cli注册命令。
 *
 *@attention 
 *该函数内部没有做互斥保护，请尽量在init函数中调用，保证是串行调用的。
 *
 *@param v_pstCmd   [IN]  注册命令的描述，取值：非空，参考\ref CLI_CMD_S。
 *
 *@retval RETURN_OK 0 注册成功。
 *@retval RETURN_ERROR -1 注册失败，入参可能为空或者不合法。
 *
 *@par 依赖：
 *
 *@since
 *@see  CLI_UnRegCmd
 */
int32_t CLI_RegCmd(CLI_CMD_S *v_pstCmd);


/**
 *@ingroup
 *
 *@par 描述:
 * 注销cli命令接口，一般在模块卸载时调用。
 *
 *
 *@param v_szCommand [IN] 注销的命令，取值：待注销命令的名字。
 *
 *
 *@par 依赖：
 *
 *@since
 *@see  CLI_RegCmd
 */
void CLI_UnRegCmd(char *v_szCommand);


/**
 *@ingroup
 *
 *@par 描述:
 * 参数解析接口，用户在注册调试接口后，都有对应的命令处理函数。该函数供用户处理解析参数，读取输入字符串信息。
 *
 *@attention 
 *该函数只能在调试模块调用fnCmdDo线程上下文时调用，不能在其他线程调用。
 *不支持合并参数输入格式:如-mnp
 *@param v_iArgc [IN] 传入调用fnCmdDo时的参数个数，取值：\ref int32_t类型整数值，大于等于0。
 *@param v_szArgv [IN] 传入调用fnCmdDo时的参数信息，取值：非空。
 *@param v_szOptString [IN] 合法的选项列表，如果选项需要参数则在后面加':', 如: "a:bi:dl"，表示合法的选项有abidl，其中a和i需要附加的参数值。
 *@param v_pstOpt [OUT] 输出当前解析的附加数据，取值：非空，参考\ref CLI_OPT_S，变量使用前必须初始化。
 *
 *@retval -1 选项处理完成。
 *@retval '?' 当前的选项字符不再合法选项列表中。
 *@retval CLI_RET_UNKNOWN_ARG 发现非选项字符串。
 *@retval 其他 返回当前的选项字符。
 *
 *@par 依赖：
 *
 *@since 
 *@see  CLI_SetOpt
 */
int32_t CLI_GetOpt(int32_t v_iArgc, char *v_szArgv[], const char *v_szOptString, CLI_OPT_S *v_pstOpt);


/**
 *@ingroup
 *
 *@par 描述:
 * cli调试命令中，用于设置参数包含某个参数。
 *
 *
 *@param v_puiOptBits [OUT] 用于保存参数位标识的变量，取值：非空。
 *@param iOpt [IN] 参数只能是小写字母，取值：小写字母，其它字符不做任何处理。
 *
 *
 *@par 依赖：
 *
 *@since
 *@see  CLI_GetOpt
 */
void CLI_SetOpt(uint32_t *v_puiOptBits, int32_t iOpt);

/**
 *@ingroup
 *
 *@par 描述:
 * 测试参数是否包含某个参数，供用户在参数处理函数中调用，与\ref CLI_SetOpt相对应。
 *
 * 
 *
 *@param uiOptBits [OUT] 用于保存参数位标识的变量，取值：无符号整数。
 *@param iOpt [IN] 参数只能是小写字母，取值：小写字母“a”到“z”。其它符号直接返回0。
 *
 *@retval  0 表示不包含。
 *@retval  非0 表示包含。
 *
 *@par 依赖：
 *
 *@since
 *@see  CLI_SetOpt
 */
int32_t CLI_TestOpt(uint32_t uiOptBits, int32_t iOpt);

/**
 *@ingroup
 *
 *@par 描述:
 * 把pszParam指向的字符串，转换成\ref uint64_t类型的无符号整数，并将值赋给pullData。
 *
 *@param pszParam [in] 参数字符串的指针，取值：非空。
 *@param pullData [out] 指向转换后整数的指针，取值：非空。
 *
 *@retval  RETURN_OK 0 获取成功。
 *@retval  RETURN_ERROR -1 获取失败，说明入参为空，或者pszParam指向的值不是无符号整数。
 *
 *@par 依赖：
 *
 *@since
 *@see  CLI_GetParamU32
 */
int32_t CLI_GetParamU64(const char *pszParam, uint64_t *pullData);

/**
 *@ingroup
 *
 *@par 描述:
 * 把指针pszParam指向的字符串，转换成\ref uint32_t类型的无符号整数，并将值赋给pullData。
 * 
 *@param pszParam [in] 参数字符串的指针，取值：非空。
 *@param pullData [out] 指向转换后整数的指针，取值：非空。
 *
 *@retval  RETURN_OK 0 获取成功
 *@retval  RETURN_ERROR -1 获取失败，说明入参为空，或者pszParam指向的值不是无符号整数。
 *
 *@par 依赖：
 *
 *@since
 *@see  CLI_GetParamU64
 */
int32_t CLI_GetParamU32(const char *pszParam, uint32_t *puiData);

/**
 *@ingroup
 *
 *@par 描述:
 * 获取指针参数，根据pszParam指向的值，转换成指针地址，传给ppPointer。
 *
 *@param pszParam [in] 参数字符串，取值：非空。
 *@param pullData [out] 传出转换以后的值，取值：非空。
 *
 *@retval  RETURN_OK 0 获取成功
 *@retval  RETURN_ERROR -1 获取失败。输入参数为空，或者pszParam无法成功转换。
 *
 *@par 依赖：
 *
 *@since
 *@see  CLI_GetParamU64 CLI_GetParamU32
 */
int32_t CLI_GetParamPointer(const char *pszParam, void **ppPointer);

/**
 *@ingroup
 *
 *@par 描述:
 * 根据打印的地址v_pvAddr和打印长度v_uiLen，通过函数CLI_PrintBuf打印地址上的内容。
 * 
 *
 *@param v_pvAddr [in] 打印的地址，取值：非空。
 *@param v_uiLen [out] 打印的长度，取值：(0,4160]。
 *
 *
 *@par 依赖：
 *
 *@since
 */
void CLI_PrintMemContext(void *v_pvAddr, uint32_t v_uiLen);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */


#endif /* _BB_CLI_H_  */

