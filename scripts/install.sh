#!/bin/bash
# Copyright: (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.

ROOT_PATH=/opt
RUN_USER=hwMindX
RUN_GROUP=HwHiAiUser
if [ "$1"x = "install"x ] && [ $# -eq 4 ];then
  RUN_USER="$2"
  RUN_GROUP="$3"
  #/opt
  ROOT_PATH="$4"
fi
if [ "$1"x = "uninstall"x ] && [ $# -eq 2 ];then
  ROOT_PATH="$2"
fi
INSTALL_PATH=$ROOT_PATH/boostio
BIN_PATH=$INSTALL_PATH/bin


SCRIPTS_PATH=$INSTALL_PATH/scripts
INCLUDE_PATH=$INSTALL_PATH/include
LIB_PATH=$INSTALL_PATH/lib
CONF_PATH=$BIN_PATH/conf
SECURITY_PATH=$INSTALL_PATH/security
LOG_PATH=${INSTALL_PATH}/logs
LOG_FILE=${LOG_PATH}/boostio_start.log

DEPRESS_PATH=/home/boostio
SYSTEMD_SERVICE_PATH=/lib/systemd/system/boostio.service
BOOSTIO_HTRACE_LOG_PATH=/var/log/boostio

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
}

#清理环境
clear_env()
{
#  erase_keys
  stop_boostio
  sleep 10

#  if [ "$ZKCLEAN" = "true" ]; then
#      #清理zk
#      echo "清理zk."
#  fi
  BioID=`ps -ef | grep bio_daemon  | grep -v grep | awk '{print $2}'`
  echo $BioID
  for id in $BioID
  do
   kill -9 $id
   echo "kill $id"
  done

  systemctl disable boostio
  sleep 10
  rm -f $SYSTEMD_SERVICE_PATH
  rm -f $BOOSTIO_HTRACE_LOG_PATH
  systemctl daemon-reload
  semanage fcontext -D $INSTALL_PATH > /dev/null 2>&1
  [ -d "$INSTALL_PATH" ] && rm -rf $INSTALL_PATH/
}



install(){
  clear_env
  check_user_group
  install_package
  set_permissions
  register_systemd
}

#拷贝软件包
install_package()
{

  DEPRESS_PATH=$(cd $(dirname "$0")/..; pwd)
  mkdir -p $INSTALL_PATH
  rm -rf $INSTALL_PATH/*
  cd $INSTALL_PATH
  mkdir -p $INSTALL_PATH
  mkdir -p $INSTALL_PATH/tools
  mkdir -p $BIN_PATH
  mkdir -p $LIB_PATH
  mkdir -p $INCLUDE_PATH
  mkdir -p $CONF_PATH
  mkdir -p $LOG_PATH
  mkdir -p $SCRIPTS_PATH
  mkdir -p $SECURITY_PATH/authorization

  touch $LOG_FILE
  print_log "info" "start install_package."


  cp "$DEPRESS_PATH"/../bio.conf $CONF_PATH
  cp  -r "$DEPRESS_PATH"/bin/* $BIN_PATH
  cp  -r "$DEPRESS_PATH"/lib/* $LIB_PATH
  cp  -r "$DEPRESS_PATH"/include/* $INCLUDE_PATH
  cp  -r "$DEPRESS_PATH"/scripts/* $SCRIPTS_PATH
}

register_systemd()
{
  touch $BIN_PATH/executeBio.sh
  cat > $BIN_PATH/executeBio.sh << EOF
export LD_LIBRARY_PATH=$LIB_PATH
setenforce 0
cd $BIN_PATH
./bio_daemon
EOF
  chmod 750 $BIN_PATH/executeBio.sh
  service_config=$SYSTEMD_SERVICE_PATH
  cat > $service_config << EOF
[Unit]
Description=boostio service
After=network.target
[Service]
User=$RUN_USER
Group=$RUN_GROUP
Type=simple
NotifyAccess=main
Environment=LD_LIBRARY_PATH=$LIB_PATH
AmbientCapabilities=CAP_SETUID CAP_SETGID
ExecStart=su - $RUN_USER -c "bash $BIN_PATH/executeBio.sh"
Restart=always
RestartSec=1
[Install]
WantedBy=multi-user.target
EOF
chmod 640 $service_config
systemctl daemon-reload
systemctl enable boostio
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
  chown -R $RUN_USER:$RUN_GROUP $INSTALL_PATH
  chmod -R 755 $INSTALL_PATH
  chmod -R 700 $INSTALL_PATH/tools
  chmod -R 500 $CONF_PATH
  chmod -R 750 $BIN_PATH
  chmod -R 500 $LIB_PATH
  chmod -R 500 $INCLUDE_PATH
  chmod -R 500 $SCRIPTS_PATH
  chmod -R 700 $LOG_PATH
  chmod -R 700 $SECURITY_PATH

  chmod 600 $CONF_PATH/bio.conf

  chown $RUN_USER:$RUN_GROUP  $LOG_FILE
  chmod 600 $LOG_FILE
  chcon -R -t home_root_t $INSTALL_PATH > /dev/null 2>&1
  semanage fcontext -a -t home_root_t $INSTALL_PATH > /dev/null 2>&1

  chown $RUN_USER:$RUN_GROUP $INSTALL_PATH
  chown $RUN_USER:$RUN_GROUP $BIN_PATH
  chown $RUN_USER:$RUN_GROUP $BIN_PATH/bio_daemon
  chown $RUN_USER:$RUN_GROUP $BIN_PATH/bio_console
  chown -R $RUN_USER:$RUN_GROUP $LIB_PATH
  chown -R $RUN_USER:$RUN_GROUP $SCRIPTS_PATH
  chown $RUN_USER:$RUN_GROUP $CONF_PATH
  chown $RUN_USER:$RUN_GROUP $CONF_PATH/bio.conf
  chmod 550 $CONF_PATH
  chmod 640 $CONF_PATH/bio.conf
  chmod 750 $BIN_PATH
  chmod 550 $BIN_PATH/bio_daemon
  chmod 550 $BIN_PATH/bio_console
  chmod -R 550 $LIB_PATH
  chmod -R 550 $SCRIPTS_PATH

  print_log "info" "set logfile permission."
}


#init node
stop_boostio(){
  systemctl stop boostio
  sleep 10
}
 
#init node
start_boostio(){
    # 拉起进程
  systemctl start boostio >> ${LOG_FILE}
}
 

case "$1" in
    install)
      install
      ;;
    start_boostio)
      stop_boostio
      start_boostio
      ;;
    stop_boostio)
      stop_boostio
      ;;
    uninstall)
     clear_env
     ;;
    *)
     echo "unsupported method."
     ;;

esac
exit 0