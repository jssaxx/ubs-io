#!/usr/bin/env python3
# -*- coding: utf-8 -*-
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

import os
import configparser

# Supported Operation
CMD_LIST = ["install", "uninstall", "stop", "start"]

# File Path
TOP_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
SSH_CMD_SHELL_PATH = os.path.join(TOP_DIR, "scripts/ssh_cmd.sh")
SCP_FILE_SHELL_PATH = os.path.join(TOP_DIR, "scripts/scp_file.sh")

# Local Machine Info
PID = os.getpid()

# Connection Config
MAX_RETRY_TIMES = 100
DEFAULT_SSH_PORT = 22

# Artifact Info
PACKAGE_PATH = ""
FOLDER_NAME = ""
TAR_SUFFIX = ".tar"
ARTIFACT_NAME = "mmscore"
ARTIFACT_COMPRESSED_NAME = ""

# Nodes Config and Info
CONFIG_JSON = "config.json"
json_info = {}
nodes_info = []
failed_times = []

# Authentication
INSTALL_USER = ""
INSTALL_PASSWORD = ""

# App Config
CONFIG_PATH = os.path.join(TOP_DIR, "conf/mms.conf")
APP_CONF = "mms.conf"
CONFIG_RULES = {
    "mms.log.level": r"^(error|warn|info|debug|trace)$",
    "mms.trace.switch": r"^(true|false)$",
    "mms.multicast.switch": r"^(true|false)$",
    "mms.deployment.mode": r"^(separate|converge)$",
    "mms.mem.numa.id": r"^.+$",
    "mms.mem.numa.size": r"^.+$",
    "mms.mem.value.unit.size": r"^(6[0-4]|[1-5][0-9]|[1-9])$",
    "mms.net.rpc.ip_mask": r"^(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}/(3[0-2]|[12]?\d)$",
    "mms.net.rpc.listen_port": r"^(720[1-9]|72[1-9]\d|7[3-7]\d{2}|7800)$",
    "mms.net.multicast.listen_port": r"^(720[1-9]|72[1-9]\d|7[3-7]\d{2}|7800)$",
    "mms.net.rpc.protocol": r"^(tcp|rdma)$",
    "mms.net.multicast.protocol": r"^(tcp|rdma)$",
    "mms.net.rpc.connect.count": r"^(1[0-6]|[1-9])$",
    "mms.net.rpc.busy_polling_mode": r"^(true|false)$",
    "mms.net.rpc.worker.groups": r"^.+$",
    "mms.net.rpc.worker.groups.cpuset": r"^.+$",
    "mms.net.ipc.busy_polling_mode": r"^(true|false)$",
    "mms.net.ipc.worker.groups": r"^.+$",
    "mms.net.ipc.worker.groups.cpuset": r"^.+$",
    "mms.net.publisher.worker.cpuset": r"^.+$",
    "mms.net.subscriber.worker.cpuset": r"^.+$",
    "mms.net.request.executor.thread.num": r"^(8|9|[1-9]\d|1\d{2}|2[0-4]\d|25[0-5]|256)$",
    "mms.net.request.executor.queue.size": r"^(102[4-9]|10[3-9]\d|1[1-9]\d{2}|[2-9]\d{3}|[1-5]\d{4}|6[0-4]\d{3}|65[0-4]\d{2}|655[0-2]\d|6553[0-5])$",
    "mms.net.message.max_buff_size": r"^[1-9]\d*$",
    "mms.net.tls.enable": r"^(true|false)$",
    "mms.net.tls.certification.path": r"^.*$",
    "mms.net.tls.ca.cert.path": r"^.*$",
    "mms.net.tls.ca.crl.path": r"^.*$",
    "mms.net.tls.private.key.path": r"^.*$",
    "mms.net.tls.private.key.password.path": r"^.*$",
    "mms.net.tls.decrypter.lib.path": r"^.*$",
    "mms.net.tls.openssl.lib.path": r"^.*$",
    "mms.cm.node.num": r"^.+$",
    "mms.cm.register_timeout_sec": r"^(1[0-9]|[2-5][0-9]|60)$",
    "mms.cm.zk_host": r"^.+$",
}
g_config = configparser.ConfigParser()

# Install Config
DEFAULT_INSTALL_PATH = "/opt"
DEFAULT_DEPLOY_USER = ""
DEFAULT_DEPLOY_GROUP = ""
