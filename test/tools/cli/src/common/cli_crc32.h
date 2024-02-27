
#include "cli_def.h"

/**
*函   数  名: diag_createCrc32
*功能描述: 计算diagnose消息的crc32
*输入参数: @buffer 指定的diagnose消息buff，length指定输入buff长度
*输出参数: 无
*返   回  值: 返回diagnose消息的crc32校验值
*/
u32 diag_createCrc32(const void *buffer, u32 length);

