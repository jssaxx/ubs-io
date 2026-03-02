#!/bin/bash

# 切换到脚本所在目录
cd "$(dirname "$0")"

# 创建build目录
mkdir -p build
cd build

# 检查是否设置了LIBURING_HOME环境变量
if [ -n "$LIBURING_HOME" ]; then
    echo "Using custom liburing from: $LIBURING_HOME"
    cmake .. -DCMAKE_PREFIX_PATH=../../dist/bio/lib -DLIBURING_INCLUDE_DIR="$LIBURING_HOME/include" -DLIBURING_LIBRARY_DIR="$LIBURING_HOME/lib"
else
    cmake .. -DCMAKE_PREFIX_PATH=../../dist/bio/lib
fi

# 编译
make

# 显示结果
if [ -f bdm_benchmark ]; then
    echo "编译成功！可执行文件：bdm_benchmark"
else
    echo "编译失败！"
    exit 1
fi
