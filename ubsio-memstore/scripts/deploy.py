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
import sys
import time
from utils import Logger
import utils
import var
import threading
import json
import getpass
import re


def broad_cast(thread_func, *args: tuple):
    thread_list = []
    for node in var.nodes_info:
        args_list = list(args)
        args_list.append(node)
        thread_args = tuple(args_list)
        thread = threading.Thread(target=thread_func, args=thread_args)
        thread.start()
        thread_list.append(thread)
        time.sleep(3)
    if wait_for_threads_end(thread_list, thread_func.__name__) != 0:
        Logger.error(
            "pid: {0} has some thread with target:{1} failed.".format(
                var.PID, thread_func.__name__
            )
        )
        return -1
    Logger.info(
        "pid: {0} broad_cast {1} finished.".format(var.PID, thread_func.__name__)
    )
    return 0


def wait_for_threads_end(thread_list, mission):
    # 如果线程未结束，等待5秒再次检查，最大重试次数30次
    retry_time = 1
    while utils.check_threads_alive(thread_list) != 0:
        if retry_time > var.MAX_RETRY_TIMES:
            Logger.error(
                "pid: {0} detected unfinished threads  even after {1} retries...".format(
                    var.PID, retry_time
                )
            )
            return -1
        Logger.info(
            "pid: {0} recheck thread list of {1} retries {2}...".format(
                var.PID, mission, retry_time
            )
        )
        retry_time = retry_time + 1
        time.sleep(5)
    if len(var.failed_times) > 0:
        Logger.error(
            "pid:{0} find more than one thread fail to {1}, failed thread num({2}).".format(
                var.PID, mission, retry_time
            )
        )
        return -1
    return 0


def process_nodes_info():
    with open(var.CONFIG_JSON, "r") as host_file:
        count = 0
        var.json_info = json.load(host_file)
        for temp in var.json_info["nodes_info"]:
            host_ip = temp["host_ip"].strip()
            Logger.info("node {0} loaded.".format(count))
            if not utils.is_valid_ip_address(host_ip):
                Logger.error("invalid host ip form {0}.".format(host_ip))
                return -1
            template = {
                "node id": count,
                "ip": host_ip,
                "port": var.DEFAULT_SSH_PORT,
                "user": var.INSTALL_USER,
                "password": var.INSTALL_PASSWORD,
            }
            count += 1
            var.nodes_info.append(template)
    return 0


def check_package(pkg_path):
    if not os.path.exists(pkg_path):
        Logger.error('Artifact "{0}" not exist!'.format(pkg_path))
        return -1

    var.PACKAGE_PATH = pkg_path
    var.PACKAGE_NAME = os.path.basename(pkg_path)
    var.FOLDER_NAME = var.PACKAGE_NAME.replace(var.TAR_SUFFIX, "")
    # 安装包名称校验
    var.APP_NAME = var.FOLDER_NAME.split("_")[0]
    if not var.APP_NAME.startswith(var.ARTIFACT_NAME):
        Logger.error("pid: {0} did not get package name as expected. ".format(var.PID))
        return -1
    return 0


def install_init(pkg_path):
    # 检查入参合法性并初始化
    Logger.info("Locating artifact at " + var.PACKAGE_PATH)
    if check_package(pkg_path) != 0:
        Logger.error("pid: {0} get artifact from {1} failed.".format(var.PID, pkg_path))
        return -1
    var.ARTIFACT_COMPRESSED_NAME = var.PACKAGE_PATH.split("/")[-1]
    Logger.success(
        "Artifact found at {0} with name {1}".format(
            var.PACKAGE_PATH, var.ARTIFACT_COMPRESSED_NAME
        )
    )
    # 写配置
    utils.json_to_conf(var.CONFIG_JSON, var.APP_CONF)
    check_config_exists()
    node_basic_info_init()

    if utils.check_config(var.APP_CONF) != 0:
        Logger.error("pid:{0} get info from {1} failed.".format(var.PID, var.APP_CONF))
        return -1
    return 0


def check_config_exists():
    Logger.info("Locating config file at " + var.APP_CONF)
    if not os.path.exists(var.APP_CONF):
        Logger.error("mms.conf not exist.")
        return -1
    Logger.success(
        "Config file found at {}, validating config now.".format(var.APP_CONF)
    )
    return 0


def node_basic_info_init():
    # 获取节点信息
    if process_nodes_info() != 0:
        Logger.error(
            'pid: {0} get_nodes_info "{1}" failed'.format(var.PID, var.CONFIG_JSON)
        )
        return -1

    Logger.success("pid:{0} initialized installation works fine.".format(var.PID))
    return 0


def install(node):
    if (
        utils.ssh_cmd(
            node,
            "/home/{0}/scripts".format(var.ARTIFACT_NAME),
            "bash install.sh install {0} {1} {2}".format(
                var.DEFAULT_DEPLOY_USER,
                var.DEFAULT_DEPLOY_GROUP,
                var.DEFAULT_INSTALL_PATH,
            ),
        )
        != 0
    ):
        var.failed_times.append(1)
        return -1

    if (
        utils.ssh_cmd(
            node,
            "/home",
            "rm -rf /home/{0} /home/{1}".format(
                var.ARTIFACT_COMPRESSED_NAME, var.ARTIFACT_NAME
            ),
        )
        != 0
    ):
        Logger.info(
            "pid:{0} remove install dir /home/{1} fails. The dir not exist".format(
                var.PID, var.ARTIFACT_COMPRESSED_NAME
            )
        )
        return -1

    return 0


# 后续会根据SYSTEMD找到安装路径再进行卸载，目前需要指定应用名。
def uninstall(node):
    if (
        utils.ssh_cmd(
            node,
            "{0}/mms/scripts".format(var.DEFAULT_INSTALL_PATH),
            "bash install.sh uninstall {0}".format(var.DEFAULT_INSTALL_PATH),
        )
        != 0
    ):
        var.failed_times.append(1)
        return -1
    return 0


def install_all_nodes(pkg_path):
    Logger.info("pid: {0} start installation.".format(var.PID))
    # 从配置文件初始化需要的信息
    Logger.info("(1/4)----------check the configuration file.")
    if install_init(pkg_path) != 0:
        Logger.error("initialization failed.")
        return -1
    # # 分发文件到各个节点
    Logger.info("(2/4)----------send artifact to all nodes.")
    if broad_cast(utils.send_files_to_node) != 0:
        Logger.info("send files failed.")
        return -1
    # 解压各节点tar包
    Logger.info("(3/4)----------decompress files in all nodes.")
    if broad_cast(utils.decompress_pkg) != 0:
        Logger.info("decompress files failed.")
        return -1
    # 安装
    Logger.info("(4/4)----------install in all nodes.")
    if broad_cast(install) != 0:
        Logger.error("install failed.")
        return -1

    Logger.success("----------success------------")

    return 0


def uninstall_all_nodes():
    # 从配置文件初始化需要的信息
    Logger.info("pid:{0} start uninstallation.".format(var.PID))
    Logger.info("(1/2)----------check the configuration file.")
    if node_basic_info_init() != 0:
        Logger.info("initialization failed.")
        return -1
    time.sleep(5)
    Logger.info("(2/2)----------uninstall in all nodes.")
    if broad_cast(uninstall) != 0:
        Logger.info("uninstall failed.")
        return -1
    time.sleep(10)
    Logger.success("----------success------------")
    return 0


def execute(args):
    arg_len = len(args)
    # 1. 检查参数是否合法
    if arg_len < 2 or args[1] not in var.CMD_LIST:
        Logger.error("Unknown Command! {}".format(args))
        print_usage()
        return -1
    var.INSTALL_USER = input("Please input login user:")
    user_pattern = r"^[a-zA-Z][a-zA-Z0-9_-]{0,31}$"
    if re.match(user_pattern, var.INSTALL_USER) is None:
        Logger.error("invalid login username.")
        return -1

    # 检查password
    var.INSTALL_PASSWORD = getpass.getpass("Please input login password:")
    # 2. 判断操作
    operation = args[1]
    # 2.1 部署
    if operation == "install":
        if len(args) != 5:
            Logger.error("install parameter missing")
            print_usage()
            return -1
        else:
            user, group, root_path = args[2:]
            var.DEFAULT_DEPLOY_USER = user
            var.DEFAULT_DEPLOY_GROUP = group
            return install_all_nodes(root_path)
    # 2.2 卸载
    elif operation == "uninstall" and len(args) == 2:
        return uninstall_all_nodes()


def print_usage():
    Logger.info("Usage:")
    Logger.info("Options:")
    Logger.info(" - install [user] [group] [artifact path]")
    Logger.info(" - uninstall")

if __name__ == "__main__":
    ret = execute(sys.argv)
    sys.exit(ret)

