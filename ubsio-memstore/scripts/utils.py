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

import re
import var
import os
from ftplib import FTP
import subprocess
import json


class Logger:
    @classmethod
    def info(cls, msg: str):
        print("\033[36m[INFO] " + msg + "\033[0m")

    @classmethod
    def debug(cls, msg: str):
        print("\033[33m[DEBUG] " + msg + "\033[0m")

    @classmethod
    def error(cls, msg: str):
        print("\033[31m[ERROR] " + msg + "\033[0m")

    @classmethod
    def success(cls, msg: str):
        print("\033[32m[SUCCESS] " + msg + "\033[0m")


def is_valid_ip_address(ip_address):
    pattern = r"^(\d{1,3}\.){3}\d{1,3}$"
    if re.match(pattern, ip_address):
        octets = ip_address.split(".")
        if all(0 <= int(octet) <= 255 for octet in octets):
            return True
    return False


def check_config(conf_path):
    var.g_config.read(conf_path, encoding="utf-8")
    for k, v in var.CONFIG_RULES.items():
        if k not in var.g_config["mms"]:
            Logger.error("Config for {0} missing!".format(k))
            return -1
        if not re.fullmatch(v, var.g_config["mms"][k]):
            Logger.error(
                "Config for {0} does not match reg expression {1}!".format(k, v)
            )
            return -1
    return 0


def check_threads_alive(thread_list):
    # 如果线程结束便移除线程列表
    for thread in thread_list:
        if not thread.is_alive():
            thread_list.remove(thread)
    return len(thread_list)


def execute_script(cmd):
    sp = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        shell=False,
        universal_newlines=True,
    )
    try:
        stdout, stderr = sp.communicate(var.INSTALL_PASSWORD, timeout=60)
    except subprocess.TimeoutExpired:
        sp.kill()
        stdout, stderr = sp.communicate(timeout=60)
    sp.wait()
    return (stdout, stderr)


def send_files_to_node(node):
    files_path = [var.APP_CONF, var.PACKAGE_PATH]
    for file_path in files_path:
        Logger.info("node{0}, file:{1}".format(node["node id"], file_path))
        ip, user, port = (
            node["ip"],
            node["user"],
            str(node["port"]),
        )
        file_name = file_path.split("/")[-1]
        cmd = [
            "bash",
            var.SCP_FILE_SHELL_PATH,
            ip,
            user,
            port,
            file_path,
            "/home/{0}".format(file_name),
        ]
        result, stderr = execute_script(cmd)
        Logger.info(
            "pid:{0} node({1}:{2}); send file({3})\nresult: {4}".format(
                var.PID, ip, port, file_path, result
            )
        )
        if "Execute SCP successful" in result:
            Logger.success(
                "pid:{0} Execution succeeded--node({1}:{2}); send file({3}) success.".format(
                    var.PID, ip, port, file_path
                )
            )
        else:
            var.failed_times.append(1)
            cmd = "bash {0} {1} {2} {3} {4} {5} ".format(
                var.SCP_FILE_SHELL_PATH,
                ip,
                user,
                port,
                file_path,
                "/home/{0}".format(var.ARTIFACT_COMPRESSED_NAME),
            )
            Logger.error(
                "pid:{0} Failed to execute SCP. (cmd:{1}), reason:{2}".format(
                    var.PID, cmd, stderr
                )
            )
            return -1
    return 0


def ssh_cmd(node, path, command):
    ip, user, port = (
        node["ip"],
        node["user"],
        str(node["port"]),
    )
    cmd = [
        "bash",
        var.SSH_CMD_SHELL_PATH,
        ip,
        user,
        port,
        path,
        command,
    ]
    result, stderr = execute_script(cmd)
    # 日志记录返回结果
    Logger.info(
        "pid:{0} node({1}:{2}); cmd({3}):\n {4}".format(var.PID, ip, port, cmd, result)
    )
    if "Execute the SSH command successfully" in result:
        Logger.success(
            "pid:{0} Execution completed--node({1}:{2});cmd({3})".format(
                var.PID, ip, port, command
            )
        )
    else:
        Logger.error(
            "pid:{0} SSH command run failed.--node({1}:{2});cmd({3}).".format(
                var.PID, ip, port, command
            )
        )
        if "bash install.sh install" in command:
            if "recode: 2" in result:
                Logger.info(
                    "{0} The user group:{1} and its user:{2} do not exist.".format(
                        ip, var.DEFAULT_DEPLOY_GROUP, var.DEFAULT_DEPLOY_USER
                    )
                )
            elif "recode: 3" in result:
                Logger.info(
                    "{0} The specified user:{1} does not exist in the user group:{2}.".format(
                        ip, var.DEFAULT_DEPLOY_USER, var.DEFAULT_DEPLOY_GROUP
                    )
                )
            elif "recode: 4" in result:
                Logger.info("{0} erase the keys failed, please check it.".format(ip))
        if "bash install.sh uninstall" in command:
            if "recode: 4" in result:
                Logger.info("{0} erase the keys failed, please check it.".format(ip))
        return -1
    return 0


def decompress_pkg(node):
    if (
        ssh_cmd(
            node,
            "/home",
            "tar -xvf /home/{0} -C /home".format(var.ARTIFACT_COMPRESSED_NAME),
        )
        != 0
    ):
        Logger.error("pid:{0} decompress package fails.".format(var.PID))
        return -1
    return 0


def write_conf(data, file, section=None):
    """递归写入配置"""
    if section:
        file.write(f"\n[{section}]\n")
    for key, value in data.items():
        if isinstance(value, dict):
            write_conf(value, file, section=key)
        else:
            file.write(f"{key} = {value}\n")


def json_to_conf(json_path, conf_path):
    with open(json_path, "r", encoding="utf-8") as f:
        data = json.load(f)

    with open(conf_path, "w", encoding="utf-8") as f:
        write_conf(data["config_info"], f)

