#!/bin/bash
CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)
cd "${CURRENT_PATH:?}"

asan="off"
if [[ "$1" == 'asan' ]]; then
    asan="on"
fi

echo ${asan}

hdt build -s ${asan} -c on \
&& cp -r ../../build/* build/ \
&& cp build/3rdparty/secure/huawei_secure_c/lib/* build/ \
&& cp -r ../../configs/ . \
&& hdt run -s ${asan} -c on "--args=\"--gtest_output=xml:report.xml\"" \
&& hdt report
