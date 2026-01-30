#!/bin/bash

mkdir -p build
cd build

cmake ..

make

cp libdecrypt.so ..

echo "Build completed. The libdecrypt.so library is in the current directory."