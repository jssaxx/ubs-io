#!/bin/bash
#
# Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
#
# ubs-io is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
#

set -e

if [ $# != 5 ];then
    echo "parameters not enough."
    exit 1
fi

ip=$1
user=$2
port=$3
file=$4
target=$5

set +e
read -s pw
set -e

sshcmd()
{
    ip_addr=$1
    username=$2
    read -s "password"
    ip_port=$4
    file_path=$5
    target_path=$6
expect <<-EOF
    set timeout 90
    spawn scp -P${ip_port} ${file_path} ${username}@${ip_addr}:${target_path}
    sleep 1
	expect {
       "*yes/no" { send "no\r"; exit 1; }
       "*password:" { send "${password}\r" }
       "*100%*"
    }
    expect eof
EOF
unset password
sleep 1
}

CUR_DIR=$(dirname $(readlink -f $0))

sshcmd ${ip} ${user} stdin ${port} ${file} "${target}" <<<${pw} > ${CUR_DIR}/scp_file_${ip}_${port}.log
if ! cat ${CUR_DIR}/scp_file_${ip}_${port}.log|grep "100%";then
    echo "node ${ip} Failed to execute SCP, log:${CUR_DIR}/scp_file_${ip}_${port}.log"
    exit 1
else
    rm -rf ${CUR_DIR}/scp_file_${ip}_${port}.log
    echo "Execute SCP successful"
    exit 0
fi
