#!/usr/bin/env python
# coding=utf-8

# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for Huawei boostio to install pkg
# version: 1.0.0
# change log:
# ***********************************************************************
import os
import sys
import logging
import time
import threading
import re
import getpass
import utils
import var


def broad_cast(thread_func, *args):
    thread_list = []
    for node in var.nodes_info:
        args_list = list(args)
        args_list.append(node)
        thread_args = tuple(args_list)
        thread = threading.Thread(target=thread_func, args=thread_args)
        thread.start()
        thread_list.append(thread)
    if wait_for_threads_end(thread_list, thread_func.__name__) != 0:
        logging.error("pid:{0} has some thread with target:{1} failed.".format(var.PID, thread_func.__name__))
        return -1
    logging.info("pid:{0} broad_cast {1} finished.".format(var.PID, thread_func.__name__))
    return 0


# 后续会根据SYSTEMD找到安装路径再进行卸载，目前需要指定应用名。
def uninstall(node):
    if utils.ssh_cmd(node, var.INSTALL_SCRIPT_PATH, "bash install.sh uninstall {0}".
                     format(var.DEFAULT_INSTALL_PATH)) != 0:
        var.failed_times.append(1)
        return -1
    return 0


def install(node):
    ip_str = "ip"
    if utils.ssh_cmd(node, "/home/{0}/{1}/scripts".format(node[ip_str], "boostio"),
                     "bash install.sh install {0} {1} {2}".format(var.DEFAULT_DEPLOY_USER, var.DEFAULT_DEPLOY_GROUP,
                                                                  var.DEFAULT_INSTALL_PATH)) != 0:
        var.failed_times.append(1)
        return -1

    if utils.ssh_cmd(node, utils.home_path_of(node["user"]),
                     "rm -rf /home/{0}/*".format(node[ip_str], node[ip_str])) != 0:
        logging.info("pid:{0} remove install dir /home/{1} fails. The dir not exist".format(var.PID, node[ip_str]))
        return -1

    return 0


def wait_for_threads_end(thread_list, mission):
    # 如果线程未结束，等待5秒再次检查，最大重试次数30次
    retry_time = 1
    while utils.check_threads_alive(thread_list) != 0:
        if retry_time > var.MAX_RETRY_TIMES:
            logging.error("pid:{0} detected unfinished threads  even after {1} retries...".format(var.PID, retry_time))
            return -1
        logging.info("pid:{0} recheck thread list of {1} retries {2}...".format(var.PID, mission, retry_time))
        retry_time = retry_time + 1
        time.sleep(5)
    if len(var.failed_times) > 0:
        logging.error("pid:{0} find more than one thread fail to {1}, failed thread num({2})."
                      .format(var.PID, mission, retry_time))
        return -1
    return 0


def delete_temp_file(node):
    # 判断临时目录不合规，不删除临时目录文件
    if not is_valid_ip_address(node["ip"]):
        return 0
    if utils.ssh_cmd(node, utils.home_path_of(node["user"]), "rm -rf /home/{0}".format(node["ip"])) != 0:
        logging.info("pid:{0} remove temp dir /home/{1} fails.".format(var.PID, node["ip"]))
    return 0


def is_valid_ip_address(ip_address):
    pattern = r'^(\d{1,3}\.){3}\d{1,3}$'
    if re.match(pattern, ip_address):
        octets = ip_address.split('.')
        if all(0 <= int(octet) <= 255 for octet in octets):
            return True
    return False


def install_init(pkg_path):
    # 检查入参合法性并初始化
    if check_package(pkg_path) != 0:
        logging.error("pid:{0} get info from {1} failed.".format(var.PID, pkg_path))
        return -1
    if not os.path.exists(var.HOST_IP_LIST):
        utils.error_log("resource file \"{0}\" not exist!".format(var.HOST_IP_LIST))
        return -1
    if check_resource_list(var.CONFIG_PATH) != 0:
        utils.echo_to_terminal("{0} not exist.".format("bio.conf"))
        return -1

    # 获取节点信息
    if get_nodes_info() != 0:
        logging.error("pid:{0} get_nodes_info \"{1}\" failed".format(var.PID, var.HOST_IP_LIST))
        return -1

    # 检查配置文件并获取必要信息
    if utils.check_config(var.CONFIG_PATH) != 0:
        logging.error("pid:{0} get info from {1} failed.".format(var.PID, var.BOOSTIO_CONF))
        return -1

    logging.info("pid:{0} initialized installation works fine.".format(var.PID))
    return 0


def check_package(pkg_path):
    if os.path.exists(pkg_path):
        var.PACKAGE_PATH = pkg_path
        if utils.check_cms(pkg_path) != 0:
            utils.echo_to_terminal("cms check failed!")
            return -1
        var.PACKAGE_NAME = os.path.basename(pkg_path)
        var.FOLDER_NAME = var.PACKAGE_NAME.replace(var.TAR_SUFFIX, "")
        # 安装包名称校验
        var.APP_NAME = var.FOLDER_NAME.split('_')[0]
        if not var.APP_NAME.startswith("BoostIO"):
            logging.error("pid:{0} did not get package name as expected. ".format(var.PID))
            return -1
    else:
        logging.error("package \"{0}\" not exist!".format(pkg_path))
        return -1
    return 0


def check_resource_list(host_path):
    utils.echo_to_terminal("config path: {0}.".format(host_path))
    if not os.path.exists(host_path):
        logging.error("resource file \"{0}\" not exist!".format(host_path))
        return -1
    return 0


def get_nodes_info():
    user_str = "user"
    password_str = "password"
    with open(var.HOST_IP_LIST) as host_file:
        count = 0
        for temp in host_file:
            ip_disk = temp.split("::")
            host_ip = ip_disk[0].strip()
            net_ip = ip_disk[1].strip()
            disk_path = ip_disk[2].strip()
            utils.echo_to_terminal("node{0} net: {1} disk_path: {2}.".format(count, net_ip, disk_path))
            if not is_valid_ip_address(host_ip):
                utils.error_log("invalid host_ip form {0}.".format(host_ip))
                return -1
            template = {
                "node id": count,
                "ip": host_ip,
                "net": net_ip,
                "port": var.DEFAULT_SSH_PORT,
                "disk": disk_path,
                user_str: var.INSTALL_USER,
                password_str: var.INSTALL_PASSWORD
            }
            count += 1
            var.nodes_info.append(template)
    return 0


def node_basic_info_init():
    if check_resource_list("host_ip_list") != 0:
        logging.error("pid:{0} get info from {1} failed.".format(var.PID, "host_ip_list"))
        return -1
    # 获取节点信息
    if get_nodes_info() != 0:
        logging.error("pid:{0} get_nodes_info \"{0}\" failed".format(var.PID, "host_ip_list"))
        return -1
    logging.info("pid:{0} initialization for installation works fine.".format(var.PID))
    return 0


def install_all_nodes(pkg_path):
    logging.info("pid:{0} start installation.".format(var.PID))
    # 从配置文件初始化需要的信息
    utils.echo_to_terminal("(1/5)----------check the configuration file.")
    if install_init(pkg_path) != 0:
        utils.echo_to_terminal("initialization failed.")
        return -1
    # 分发文件到各个节点
    utils.echo_to_terminal("(2/5)----------send files to all nodes.")
    if broad_cast(utils.send_files_to_node) != 0:
        utils.echo_to_terminal("send files failed.")
        return -1
    # 解压各节点tar包
    utils.echo_to_terminal("(3/5)----------decompress files in all nodes.")
    if broad_cast(utils.decompress_pkg) != 0:
        utils.echo_to_terminal("decompress files failed.")
        return -1
    # 安装
    utils.echo_to_terminal("(4/5)----------install in all nodes.")
    if broad_cast(install) != 0:
        utils.echo_to_terminal("install failed.")
        return -1
    # 各个节点启动进程
    time.sleep(10)

    utils.echo_to_terminal("(5/5)----------success.")

    return 0


def uninstall_all_nodes():
    # 从配置文件初始化需要的信息
    logging.info("pid:{0} start uninstallation.".format(var.PID))
    utils.echo_to_terminal("(1/3)----------check the configuration file.")
    if node_basic_info_init() != 0:
        utils.echo_to_terminal("initialization failed.")
        return -1
    time.sleep(5)
    utils.echo_to_terminal("(2/3)----------uninstall in all nodes.")
    if broad_cast(uninstall) != 0:
        utils.echo_to_terminal("uninstall failed.")
        return -1
    time.sleep(10)
    utils.echo_to_terminal("(3/3)----------success.")
    return 0


def exec_build(args):
    if len(args) < 2:
        utils.help_info()
        return -1
    if args[1] not in ["install", "uninstall"]:
        utils.help_info()
        return -1
    var.INSTALL_USER = input('input login user:')
    user_pattern = r'^[a-zA-Z][a-zA-Z0-9_-]{0,31}$'
    if re.match(user_pattern, var.INSTALL_USER) is None:
        utils.error_log("invalid login username or password.")
        return -1

    # 检查password
    var.INSTALL_PASSWORD = getpass.getpass('input login password:')
    if args[1] == "install":
        if len(args) != 5:
            utils.help_info()
            return -1
        if len(args) == 5:
            var.DEFAULT_DEPLOY_USER = args[3]
            var.DEFAULT_DEPLOY_GROUP = args[4]
        return install_all_nodes(args[2])
    elif args[1] == "uninstall" and len(args) == 2:
        return uninstall_all_nodes()
    else:
        utils.help_info()
        return -1


if __name__ == "__main__":
    ret = exec_build(sys.argv)
    sys.exit(ret)