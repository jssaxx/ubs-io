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
ROOT_PATH=/opt
RUN_USER=hwMms
RUN_GROUP=hwUser
if [ "$1"x = "install"x ] && [ $# -eq 4 ];then
  RUN_USER="$2"
  RUN_GROUP="$3"
  ROOT_PATH="$4"
fi
if [ "$1"x = "uninstall"x ] && [ $# -eq 2 ];then
  ROOT_PATH="$2"
fi

INSTALL_PATH=$ROOT_PATH/mms
BIN_PATH=$INSTALL_PATH/bin

SCRIPTS_PATH=$INSTALL_PATH/scripts
INCLUDE_PATH=$INSTALL_PATH/include
LIB_PATH=$INSTALL_PATH/lib
CONF_PATH=$INSTALL_PATH/conf
LOG_PATH=${INSTALL_PATH}/logs
LOG_FILE=${INSTALL_PATH}/logs/mms_start.log
MMS_HTRACE_LOG_PATH=/var/log/mms

#日志打印
print_log()
{
  local script_name=$(basename "$0")
  local line_number=${BASH_LINENO[0]}
  local timestamp=$(date +"%Y-%m-%d %H:%M:%S")
  local pid=$$

  local log_level="$1"
  local log_message="$2"

  echo "[$timestamp] [$pid] [$log_level] [$script_name:$line_number] $log_message" >> ${LOG_FILE}
  echo "$log_message"
}

#清理环境
clear_env()
{
  set +e
  mms_id=$(ps -ef | grep mmsd  | grep -v grep | awk '{print $2}')
  for id in $mms_id
  do
   kill -9 $id
  done

  mms_console_id=$(ps -ef | grep mms_console  | grep -v grep | awk '{print $2}')
  for id in $mms_console_id
  do
   kill -9 $id
  done

  [ -d "${MMS_HTRACE_LOG_PATH}" ] && rm -rf $MMS_HTRACE_LOG_PATH
  semanage fcontext -D $INSTALL_PATH > /dev/null 2>&1
  [ -d "$INSTALL_PATH" ] && rm -rf $INSTALL_PATH/

  set -e
}

install(){
  install_package
  set_permissions
}

#拷贝软件包
install_package()
{
  DEPRESS_PATH=$(cd $(dirname "$0")/..; pwd)
  mkdir -p $INSTALL_PATH
  rm -rf $INSTALL_PATH/*
  cd $INSTALL_PATH
  mkdir -p $INSTALL_PATH
  mkdir -p $BIN_PATH
  mkdir -p $LIB_PATH
  mkdir -p $INCLUDE_PATH
  mkdir -p $CONF_PATH
  mkdir -p $LOG_PATH
  mkdir -p $SCRIPTS_PATH
  rm -rf $MMS_HTRACE_LOG_PATH
  mkdir -p $MMS_HTRACE_LOG_PATH

  touch $LOG_FILE
  print_log "info" "install_package start."

  cp  -r "$DEPRESS_PATH"/bin/* $BIN_PATH
  cp  -r "$DEPRESS_PATH"/lib/* $LIB_PATH
  cp  -r "$DEPRESS_PATH"/include/* $INCLUDE_PATH
  cp  -r "$DEPRESS_PATH"/scripts/install.sh $SCRIPTS_PATH
  mv  "$DEPRESS_PATH"/../mms.conf $CONF_PATH
  print_log "info" "install_package finish."
}

check_user_group()
{
  if ! getent group "$RUN_GROUP" > /dev/null; then
    echo "The user group:${RUN_GROUP} and its user:${RUN_USER} do not exist."
    exit 2
  fi

  if ! id -nG "$RUN_USER" | grep -qw "$RUN_GROUP"; then
    echo "The specified user:${RUN_USER} does not exist in the user group:${RUN_GROUP}."
    exit 3
  fi
}

set_permissions()
{
  print_log "info" "set_permissions start."
  chown -R $RUN_USER:$RUN_GROUP $INSTALL_PATH
  chown -R $RUN_USER:$RUN_GROUP $MMS_HTRACE_LOG_PATH
  sudo -u $RUN_USER bash << EOF
  chmod -R 750 $INSTALL_PATH
  chmod 550 $CONF_PATH
  chmod 550 $BIN_PATH
  chmod 550 $BIN_PATH/mmsd
  if [ -f "$BIN_PATH/mms_console" ]; then
    chmod 550 "$BIN_PATH/mms_console"
  fi

  if [ -f "$BIN_PATH/cli_client" ]; then
    chmod 550 "$BIN_PATH/cli_client"
  fi

  if [ -f "$BIN_PATH/cli_server" ]; then
    chmod 550 "$BIN_PATH/cli_server"
  fi

  chmod -R 550 $LIB_PATH
  chmod -R 550 $INCLUDE_PATH
  chmod -R 700 $LOG_PATH

  chmod 640 $CONF_PATH/mms.conf

  chmod 750 $MMS_HTRACE_LOG_PATH
  chmod 600 $LOG_FILE
EOF
  print_log "info" "set_permissions finish."
}

case "$1" in
    install)
      install
      ;;
    uninstall)
      clear_env
      ;;
    *)
      echo "unsupported method.
      install.sh [install / uninstall] [user] [group]"
      ;;

esac
exit 0
