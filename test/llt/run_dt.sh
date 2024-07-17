#!/bin/bash
CURRENT_PATH=$(cd "$(dirname "$0")"; pwd)
cd "${CURRENT_PATH:?}"

asan="off"
if [[ "$1" == 'asan' ]]; then
    asan="on"
fi

echo "enable asan: ${asan}"

if [[ -n "$2" ]]; then
  sed -i "s#bio.disk.path = .*#bio.disk.path = $2#g" ../../configs/bio.conf
fi

hdt build -s ${asan} -c on \
&& cp -r ../../build/* build/ \
&& cp build/3rdparty/secure/huawei_secure_c/lib/* build/ \
&& cp -r ../../configs/ . \
&& \cp ../../output/boostio/lib/* build/ \
&& hdt run -s ${asan} -c on "--args=\"--gtest_output=xml:report.xml\"" \
&& hdt report \
&& pwd \
&& cp ./build/report.xml ../../build/hdt_report