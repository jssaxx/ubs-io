#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
# script for Huawei hyperio to build pkg
# version: 1.0.0
# change log:
# ***********************************************************************

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

CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}")"
source ${PROJ_DIR}/common.sh

set +e
read -s pw
set -e

UPDATE_PASSWORD=$(transfor_special_characters "$pw")
sshcmd()
{
    ip_addr=$1
    username=$2
    ip_port=$4
    file_path=$5
    target_path=$6
expect 2>  /dev/null << EOF
    set timeout 90
    spawn scp -P${ip_port} ${file_path} ${username}@${ip_addr}:${target_path}
    sleep 1
	expect {
       "*yes/no" { send -- "yes\r"; exp_continue }
       "*password:" { send -- "${UPDATE_PASSWORD}\r" }
       "*100%*"
    }
    expect eof
EOF
sleep 1
}

CUR_DIR=$(dirname $(readlink -f $0))

sshcmd ${ip} ${user} stdin ${port} ${file} "${target}" <<<${UPDATE_PASSWORD} > ${CUR_DIR}/scp_file_${ip}_${port}.log
if ! cat ${CUR_DIR}/scp_file_${ip}_${port}.log|grep "100%";then
    echo "node ${ip} Failed to execute SCP, log:${CUR_DIR}/scp_file_${ip}_${port}.log"
    exit 1
else
    rm -rf ${CUR_DIR}/scp_file_${ip}_${port}.log
    echo "Execute SCP successful"
    exit 0
fi