#!/bin/bash

# 切换到脚本所在目录
cd "$(dirname "$0")"

# 创建build目录
mkdir -p build
cd build

# 运行cmake配置
cmake .. -DCMAKE_PREFIX_PATH=../../dist/bio/lib

# 编译
make

# 显示结果
if [ -f bdm_benchmark ]; then
    echo "编译成功！可执行文件：bdm_benchmark"
else
    echo "编译失败！"
    exit 1
fi
