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
cmd_path=$4
cmd=$5

set +e
read -s pw
set -e

sshcmd()
{
    ip_addr=$1
    username=$2
    read -s password
    ip_port=$4
    command_path=$5
    command=$6
expect <<-EOF
    set timeout 90
    spawn ssh ${username}@${ip_addr} -p${ip_port}
    sleep 1
    expect {
       "*yes/no" { send "no\r"; exit 1;}
       "*password:" { send "${password}\r"; exp_continue }
       "Permission denied, please try again.*" { exit 1;}
       "]*" { send "export HISTFILE=/dev/null\r" }
       "* *#" { send "export HISTFILE=/dev/null\r"}
    }
    expect {
        "]*"; { send "cd ${command_path}\r"}
        "* *#";{ send "cd ${command_path}\r"}
    }
    expect {
        "]*"; { send "${command}\r" }
        "* *#";{ send "${command}\r" }
    }
    expect {
       "*password*" { send "${password}\r" }
       "]*" { send "\r" }
       "* *#" { send "\r"}
    }
    expect {
       "]*"; { send "echo \"recode: \$\?\"\r" }
       "* *#";{ send "echo \"recode: \$\?\"\r" }
    }
    expect {
       "]*"; { send "echo \"recode: \$\?\"\r" }
       "* *#";{ send "echo \"recode: \$\?\"\r" }
    }
    expect {
       "]*"; { send "exit\r" }
       "* *#";{ send "exit\r" }
    }
    expect eof
EOF
unset password
sleep 1
}



CUR_DIR=$(dirname $(readlink -f $0))

sshcmd ${ip} ${user} stdin ${port} ${cmd_path} "${cmd}" <<<${pw} > ${CUR_DIR}/ssh_cmd_${ip}_${port}.log
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

