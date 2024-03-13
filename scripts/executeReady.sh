#!/bin/bash

# 尝试执行 ibdev2netdev 命令
output=$(ibdev2netdev 2>&1)
status=$?

# 判断命令是否执行成功
if [ $status -eq 0 ]; then
    echo "命令执行成功。"
    echo "输出结果为："
    echo "$output"
    /bin/bash ../scripts/check_mlx_ecn.sh
else
    echo "命令执行失败。"
    # 输出错误信息
    echo "错误输出为："
    echo "$output"
fi
