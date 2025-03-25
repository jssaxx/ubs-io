#!/bin/bash
# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# script for Huawei boostio to build pkg
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
cmd_path=$4
cmd=$5

CURRENT_PATH="$(dirname "${BASH_SOURCE[0]}")"
PROJ_DIR="$(realpath "${CURRENT_PATH}")"
source ${PROJ_DIR}/common.sh

set +e
read -s pw
set -e

UPDATE_PASSWORD=$(transfor_special_characters "$pw")
unset pw

sshcmd()
{
    ip_addr=$1
    username=$2
    ip_port=$4
    command_path=$5
    command=$6
expect 2>  /dev/null << EOF
    set timeout 90
    spawn ssh ${username}@${ip_addr} -p${ip_port}
    sleep 1
        expect {
       "*yes/no" { send -- "yes\r"; exp_continue }
       "*password:" { send -- "${UPDATE_PASSWORD}\r"; exp_continue }
       "Permission denied, please try again.*" { exit 1;}
       "]*" { send -- "export HISTFILE=/dev/null\r" }
       "* *#" { send -- "export HISTFILE=/dev/null\r"}
    }
    expect {
            "]*"; { send -- "cd ${command_path}\r"}
            "* *#";{ send -- "cd ${command_path}\r"}
    }
    expect {
            "]*"; { send -- "${command}\r" }
            "* *#";{ send -- "${command}\r" }
    }
    expect {
       "*password*" { send -- "${password}\r" }
       "]*" { send -- "\r" }
       "* *#" { send -- "\r"}
    }
    expect {
       "]*"; { send -- "echo \"recode: \$\?\"\r" }
       "* *#";{ send -- "echo \"recode: \$\?\"\r" }
    }
    expect {
       "]*"; { send -- "echo \"recode: \$\?\"\r" }
       "* *#";{ send -- "echo \"recode: \$\?\"\r" }
    }
    expect {
       "]*"; { send -- "exit\r" }
       "* *#";{ send -- "exit\r" }
    }
    expect eof
EOF
sleep 1
}



CUR_DIR=$(dirname $(readlink -f $0))

sshcmd ${ip} ${user} stdin ${port} ${cmd_path} "${cmd}" <<<${UPDATE_PASSWORD} > ${CUR_DIR}/ssh_cmd_${ip}_${port}.log
if ! cat ${CUR_DIR}/ssh_cmd_${ip}_${port}.log|grep "recode: 0";then
    echo "----node ${ip} Failed to execute the SSH command:"
    cat ${CUR_DIR}/ssh_cmd_${ip}_${port}.log
    rm -rf ${CUR_DIR}/ssh_cmd_${ip}_${port}.log
    exit 1
else
    rm -rf ${CUR_DIR}/ssh_cmd_${ip}_${port}.log
    echo "Execute the SSH command successfully"
    exit 0
fi
