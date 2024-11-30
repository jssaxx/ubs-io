#!/usr/bin/env python
# coding=utf-8

# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
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
import configparser
import subprocess
import copy
import shutil

sp = subprocess.Popen(["touch", "./boostio_hand_out_py.log"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False, universal_newlines=True)
sp.communicate()
sp.wait()
sp = subprocess.Popen(["chmod", "600", "./boostio_hand_out_py.log"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False, universal_newlines=True)
sp.communicate()
sp.wait()
logging.basicConfig(level=logging.INFO,
                    filename="./boostio_hand_out_py.log",
                    format="[%(asctime)s][%(levelname)s][%(filename)s, %(lineno)d][%(message)s]",
                    datefmt="%Y-%m-%d %H:%M:%S")

DEFAULT_SSH_PORT = 22
MAX_RETRY_TIMES = 100

PID = os.getpid()

USAGE = """
Usage:
{name:s} [option]

Options:
    install    [pkg_path] [user] [group]
    uninstall  
""".format(name=os.path.basename(__file__))

nodes_info = []
configs = []
failed_times = []
under_fs_info = {}
PACKAGE_PATH = ""
PACKAGE_NAME = ""
FOLDER_NAME = ""

TOP_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
SSH_CMD_SHELL_PATH = os.path.join(TOP_DIR, "scripts/ssh_cmd.sh")
SCP_FILE_SHELL_PATH = os.path.join(TOP_DIR, "scripts/scp_file.sh")
CONFIG_PATH = os.path.join(TOP_DIR, "conf/bio.conf")
TAR_SUFFIX = ".tar"
CONTROL_FILE_FOLDER = "scripts"
APP_NAME = ""
BELONGING_USER = ""
STORAGE_DEVICE_TYPE = ""
MEM_FS_CONFIG_FILE = "memfs.conf"
INSTALL_USER = ""
INSTALL_PASSWORD = ""
DEFAULT_DEPLOY_USER = "boostio"
DEFAULT_DEPLOY_GROUP = "boostio"
DEFAULT_INSTALL_PATH = "/opt"
INSTALL_SCRIPT_PATH = "/opt/boostio/scripts"
g_config = configparser.ConfigParser()


BOOSTIO_CONF = "bio.conf"
HOST_IP_LIST = "host_ip_list"
NODE_COUNT=0
DEFAULT_HCOM_PORT = 9898
CONFIG_OBJ = {}

def echo_to_terminal(message):
    lines = message.splitlines()
    for line in lines:
        print(line)


def help_info():
    echo_to_terminal(USAGE)
    return


def home_path_of(user):
    return "/home"


def send_files_to_node(node):
    ip_str = "ip"
    net_str = "net"
    port_str = "port"
    disk_str = "disk"
    node_id = "node id"
    if ssh_cmd(node, home_path_of(node["user"]),
               "rm -rf /home/{0}/*;".format(node[ip_str], node[ip_str])) != 0:
        logging.info("pid:{0} clear /home/{1} fails. ".format(PID, node[ip_str]))
    config = copy.deepcopy(g_config);
    config["bio"]["bio.net.data.ip_mask"] = str(node[net_str]) + "/24"
    config["bio"]["bio.disk.path"] = str(node[disk_str])
    folder_path = os.path.join(os.path.join(os.path.dirname(os.path.realpath(__file__)), "{0}".format(node[node_id])))
    if os.path.exists(folder_path):
        pass
    else:
        os.makedirs(folder_path)
    with open("{0}/bio.conf".format(node[node_id]), 'w') as configfile:
        config.write(configfile)
    os.path.realpath(__file__)
    new_config_path = os.path.join(folder_path, "bio.conf")
    will_sent_files = [PACKAGE_PATH, new_config_path]
    for sent_file in will_sent_files:
        echo_to_terminal("node{0}, file:{1}".format(node[node_id], sent_file))
        cmd = "bash {0} {1} {2} {3} {4} {5}" \
            .format(SCP_FILE_SHELL_PATH, node[ip_str], node["user"], node[port_str], sent_file,
                    "/home/{0}".format(node[ip_str]))
        newcmd = ["{0}".format(SCP_FILE_SHELL_PATH), "{0}".format(node[ip_str]), "{0}".format(node["user"]), "{0}".format(node[port_str]),
                  "{0}".format(sent_file), "{0}".format("/home/{0}".format(node[ip_str]))]
        sp = subprocess.Popen(newcmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False, universal_newlines=True)
        stdout, stderr = sp.communicate(node["password"])
        sp.wait()
        result = stdout
        logging.info("pid:{0} node({1}:{2}); send file({3})\nresult: {4}".format(PID, node[ip_str], node[port_str], sent_file, result))
        if "Execute SCP successful" in result:
            logging.info(
                "pid:{0} Execution succeeded--node({1}:{2}); send file({3}) success.".format(PID, node[ip_str], node[port_str], sent_file))
        else:
            failed_times.append(1)
            cmd = "bash {0} {1} {2} {3} {4} {5} " \
                .format(SCP_FILE_SHELL_PATH, node[ip_str], node["user"], node[port_str], sent_file,
                        "/home/{0}".format(node[ip_str]))
            logging.error("pid:{0} Failed to execute SCP. (cmd:{1})".format(PID, cmd))
            if os.path.exists(folder_path):
                shutil.rmtree(folder_path)
            return -1
    if os.path.exists(folder_path):
        shutil.rmtree(folder_path)
    return 0

def check_threads_alive(thread_list):
    # 如果线程结束便移除线程列表
    for thread in thread_list:
        if not thread.is_alive():
            thread_list.remove(thread)
    return len(thread_list)


def broad_cast(thread_func, *args):
    thread_list = []
    for node in nodes_info:
        args_list = list(args)
        args_list.append(node)
        thread_args = tuple(args_list)
        thread = threading.Thread(target=thread_func, args=thread_args)
        thread.start()
        thread_list.append(thread)
    if wait_for_threads_end(thread_list, thread_func.__name__) != 0:
        logging.error("pid:{0} has some thread with target:{1} failed.".format(PID, thread_func.__name__))
        return -1
    logging.info("pid:{0} broad_cast {1} finished.".format(PID, thread_func.__name__))
    return 0


def ssh_cmd(node, path, command):
    ip_str = "ip"
    port_str = "port"
    cmd = "bash {0} {1} {2} {3} {4} \"{5}\"" \
        .format(SSH_CMD_SHELL_PATH, node[ip_str], node["user"], node[port_str], path, command)
    newcmd = ["{0}".format(SSH_CMD_SHELL_PATH), "{0}".format(node[ip_str]), "{0}".format(node["user"]), "{0}".format(node[port_str]),
              "{0}".format(path), "{0}".format(command)]
    sp = subprocess.Popen(newcmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False, universal_newlines=True)
    stdout, stderr = sp.communicate(node["password"])
    sp.wait()
    result = stdout
    # 日志记录返回结果
    logging.info("pid:{0} node({1}:{2}); cmd({3}):\n {4}".format(PID, node[ip_str], node[port_str], cmd, result))
    if "Execute the SSH command successfully" in result:
        logging.info(
            "pid:{0} Execution completed--node({1}:{2});cmd({3})".format(PID, node[ip_str], node[port_str], command))
    else:
        logging.error(
            "pid:{0} SSH command run failed.--node({1}:{2});cmd({3})".format(PID, node[ip_str], node[port_str], command))
        if "bash install.sh install" in command:
            if "recode: 2" in result:
                echo_to_terminal("{0} The user group:{1} and its user:{2} do not exist.".
                                 format(node[ip_str], DEFAULT_DEPLOY_GROUP, DEFAULT_DEPLOY_USER))
            elif "recode: 3" in result:
                echo_to_terminal("{0} The specified user:{1} does not exist in the user group:{2}.".
                                 format(node[ip_str], DEFAULT_DEPLOY_USER, DEFAULT_DEPLOY_GROUP))
            elif "recode: 4" in result:
                echo_to_terminal("{0} erase the keys failed, please check it.".
                                 format(node[ip_str]))
        if "bash install.sh uninstall" in command:
            if "recode: 4" in result:
                echo_to_terminal("{0} erase the keys failed, please check it.".
                                 format(node[ip_str]))
        return -1
    return 0


def decompress_pkg(node):
    ip_str = "ip"
    if ssh_cmd(node, home_path_of(node["user"]) + "/{0}".format(node[ip_str]),
               "tar -xvf /home/{0}/{1} -C /home/{2};chown -R {3}:{4} /home/{2}/*;chmod -R 700 /home/{2}/*".format(node[ip_str],
               PACKAGE_NAME, node[ip_str], INSTALL_USER, INSTALL_USER)) != 0:
        logging.error("pid:{0} decompress package fails.".format(PID))
        return -1
    return 0


# 后续会根据SYSTEMD找到安装路径再进行卸载，目前需要指定应用名。
def uninstall(node):
    if ssh_cmd(node, INSTALL_SCRIPT_PATH, "bash install.sh uninstall {0}".format(DEFAULT_INSTALL_PATH)) != 0:
        failed_times.append(1)
        return -1
    return 0

def install(node):
    ip_str = "ip"
    if ssh_cmd(node, "/home/{0}/{1}/scripts".format(node[ip_str], "boostio"),
               "bash install.sh install {0} {1} {2}".format(DEFAULT_DEPLOY_USER, DEFAULT_DEPLOY_GROUP,
                                                            DEFAULT_INSTALL_PATH)) != 0:
        failed_times.append(1)
        return -1
    if ssh_cmd(node, home_path_of(node["user"]),
               "rm -rf /home/{0}/*".format(node[ip_str], node[ip_str])) != 0:
        logging.info("pid:{0} remove install dir /home/{1} fails. The dir not exist".format(PID, node[ip_str]))



def wait_for_threads_end(thread_list, mission):
    # 如果线程未结束，等待5秒再次检查，最大重试次数30次
    retry_time = 1
    while check_threads_alive(thread_list) != 0:
        if retry_time > MAX_RETRY_TIMES:
            logging.error("pid:{0} detected unfinished threads  even after {1} retries...".format(PID, retry_time))
            return -1
        logging.info("pid:{0} recheck thread list of {1} retries {2}...".format(PID, mission, retry_time))
        retry_time = retry_time + 1
        time.sleep(5)
    if len(failed_times) > 0:
        logging.error("pid:{0} find more than one thread fail to {1}, failed thread num({2})."
                      .format(PID, mission, retry_time))
        return -1
    return 0


def delete_temp_file(node):
    # 判断临时目录不合规，不删除临时目录文件
    if not is_valid_ip_address(node["ip"]):
        return 0
    if ssh_cmd(node, home_path_of(node["user"]), "rm -rf /home/{0}".format(node["ip"])) != 0:
        logging.info("pid:{0} remove temp dir /home/{1} fails.".format(PID, node["ip"]))
    return 0


def error_log(string):
    echo_to_terminal(string)
    logging.error(string)

def is_valid_ip_address(ip_address):
    pattern = r'^(\d{1,3}\.){3}\d{1,3}$'
    if re.match(pattern, ip_address):
        octets = ip_address.split('.')
        if all(0 <= int(octet) <= 255 for octet in octets):
            return True
    return False


def key_is_in_dict(input_key, input_dict):
    if input_key not in input_dict:
        return False
    else:
        return True


def check_cms(pkg_path):
    if not os.path.exists(pkg_path):
        logging.error("pid:{0} get package failed :{1}".format(PID, pkg_path))
    return 0


def install_init(pkg_path):
    # 检查入参合法性并初始化
    if check_package(pkg_path) != 0:
        logging.error("pid:{0} get info from {1} failed.".format(PID, pkg_path))
        return -1
    if not os.path.exists(HOST_IP_LIST):
        error_log("resource file \"{0}\" not exist!".format(HOST_IP_LIST))
        return -1
    if check_resource_list(CONFIG_PATH) != 0:
        echo_to_terminal("{0} not exist.".format("bio.conf"))
        return -1

    # 获取节点信息
    if get_nodes_info() != 0:
        logging.error("pid:{0} get_nodes_info \"{1}\" failed".format(PID, HOST_IP_LIST))
        return -1

    # 检查配置文件并获取必要信息
    if check_config(CONFIG_PATH) != 0:
        logging.error("pid:{0} get info from {1} failed.".format(PID, BOOSTIO_CONF))
        return -1

    logging.info("pid:{0} initialized installation works fine.".format(PID))
    return 0


def check_package(pkg_path):
    if os.path.exists(pkg_path):
        global PACKAGE_PATH
        PACKAGE_PATH = pkg_path
        if check_cms(pkg_path) != 0:
            echo_to_terminal("cms check failed!")
            return -1
        global PACKAGE_NAME
        PACKAGE_NAME = os.path.basename(pkg_path)
        global FOLDER_NAME
        FOLDER_NAME = PACKAGE_NAME.replace(TAR_SUFFIX, "")
        global APP_NAME
        # 安装包名称校验
        APP_NAME = FOLDER_NAME.split('_')[0]
        if not APP_NAME.startswith("BoostIO"):
            logging.error("pid:{0} did not get package name as expected. ".format(PID))
            return -1
    else:
        logging.error("package \"{0}\" not exist!".format(pkg_path))
        return -1
    return 0


def check_resource_list(host_path):
    echo_to_terminal("config path: {0}.".format(host_path))
    if not os.path.exists(host_path):
        logging.error("resource file \"{0}\" not exist!".format(host_path))
        return -1
    return 0


def check_config(conf_path):
    g_config.read(conf_path, encoding='utf-8')
    if "bio.log.level" not in g_config["bio"]:
        echo_to_terminal("config does not contain bio.log.level.")
        return -1
    else:
        boostio_log_level = g_config["bio"]["bio.log.level"]
        if boostio_log_level.strip() == "":
            echo_to_terminal("bio.log.level is null.")
            return -1

    if "bio.cm.pts_count" not in g_config["bio"]:
        echo_to_terminal("config does not contain bio.cmc.pts_count.")
        return -1
    else:
        cm_pt_count = int(g_config["bio"]["bio.cm.pts_count"])
        if cm_pt_count < 1 or cm_pt_count > 8192:
            echo_to_terminal("max_open_files is not in range 1 - 8192.")
            return -1

    if "bio.cm.register_timeout_sec" not in g_config["bio"]:
        echo_to_terminal("config does not contain bio.cm.register_timeout_sec.")
        return -1
    else:
        cm_register_timeout_sec = g_config["bio"]["bio.cm.register_timeout_sec"]
        if cm_register_timeout_sec.strip() == "":
            echo_to_terminal("bio.cm.register_timeout_sec is null.")
            return -1

    if "bio.cm.register_perm_timeout_sec" not in g_config["bio"]:
        echo_to_terminal("config does not contain bio.cm.register_perm_timeout_sec.")
        return -1
    else:
        cm_register_perm_timeout_sec = g_config["bio"]["bio.cm.register_perm_timeout_sec"]
        if cm_register_perm_timeout_sec.strip() == "":
            echo_to_terminal("bio.cm.register_perm_timeout_sec is null.")
            return -1

    if "bio.net.data.protocol" not in g_config["bio"]:
        echo_to_terminal("config does not contain bio.net.data.protocol.")
        return -1
    else:
        net_data_protocol = (g_config["bio"]["bio.net.data.protocol"])
        if net_data_protocol.strip() != "rdma" and net_data_protocol.strip() != "tcp":
            echo_to_terminal("bio.net.data.protocol is not in rdma/tcp")
            return -1

    if "bio.segment.size_in_mb" not in g_config["bio"]:
        echo_to_terminal("config does not bio.segment.size_in_mb.")
        return -1

    if "bio.mem.size_in_gb" not in g_config["bio"]:
        echo_to_terminal("config does not contain bio.mem.size_in_gb.")
        return -1
    return 0

def get_nodes_info_no_conf():
    user_str = "user"
    password_str = "password"
    global NODE_COUNT
    with open(HOST_IP_LIST) as host_file:
        count = 0
        for temp in host_file:
            ip_disk = temp.split("::")
            host_ip = ip_disk[0].strip()
            net_ip = ip_disk[1].strip()
            disk_path = ip_disk[2].strip()
            echo_to_terminal("node{0} net: {1} disk_path: {2}.".format(count, net_ip, disk_path))
            if not is_valid_ip_address(host_ip):
                error_log("invalid host_ip form {0}.".format(host_ip))
                return False
            template = {
                "node id": count,
                "ip": host_ip,
                "net": net_ip,
                "port": DEFAULT_SSH_PORT,
                "disk": disk_path,
                user_str: INSTALL_USER,
                password_str: INSTALL_PASSWORD
            }
            count += 1
            nodes_info.append(template)
    return 0

def get_nodes_info():
    user_str = "user"
    password_str = "password"
    global NODE_COUNT
    with open(HOST_IP_LIST) as host_file:
        count = 0
        for temp in host_file:
            ip_disk = temp.split("::")
            host_ip = ip_disk[0].strip()
            net_ip = ip_disk[1].strip()
            disk_path = ip_disk[2].strip()
            echo_to_terminal("node{0} net: {1} disk_path: {2}.".format(count, net_ip, disk_path))
            if not is_valid_ip_address(host_ip):
                error_log("invalid host_ip form {0}.".format(host_ip))
                return False
            template = {
                "node id": count,
                "ip": host_ip,
                "net": net_ip,
                "port": DEFAULT_SSH_PORT,
                "disk": disk_path,
                user_str: INSTALL_USER,
                password_str: INSTALL_PASSWORD
            }
            count += 1
            nodes_info.append(template)
        NODE_COUNT = count
    return 0


def node_basic_info_init():
    if check_resource_list("host_ip_list") != 0:
        logging.error("pid:{0} get info from {1} failed.".format(PID, "host_ip_list"))
        return -1
    # 获取节点信息
    if get_nodes_info_no_conf() != 0:
        logging.error("pid:{0} get_nodes_info \"{0}\" failed".format(PID, "host_ip_list"))
        return -1
    logging.info("pid:{0} initialization for installation works fine.".format(PID))
    return 0


def install_all_nodes(pkg_path):
    logging.info("pid:{0} start installation.".format(PID))
    # 从配置文件初始化需要的信息
    echo_to_terminal("(1/5)----------check the configuration file.")
    if install_init(pkg_path) != 0:
        echo_to_terminal("initialization failed.")
        return -1
    # 分发文件到各个节点
    echo_to_terminal("(2/5)----------send files to all nodes.")
    if broad_cast(send_files_to_node) != 0:
        echo_to_terminal("send files failed.")
        return -1
    # 解压各节点tar包
    echo_to_terminal("(3/5)----------decompress files in all nodes.")
    if broad_cast(decompress_pkg) != 0:
        echo_to_terminal("decompress files failed.")
        return -1
    # 安装
    echo_to_terminal("(4/5)----------install in all nodes.")
    if broad_cast(install) != 0:
        echo_to_terminal("install failed.")
        return -1
    # 各个节点启动进程
    time.sleep(10)

    echo_to_terminal("(5/5)----------success.")

    return 0


def uninstall_all_nodes():
    # 从配置文件初始化需要的信息
    logging.info("pid:{0} start uninstallation.".format(PID))
    echo_to_terminal("(1/3)----------check the configuration file.")
    if node_basic_info_init() != 0:
        echo_to_terminal("initialization failed.")
        return -1
    time.sleep(5)
    echo_to_terminal("(2/3)----------uninstall in all nodes.")
    if broad_cast(uninstall) != 0:
        echo_to_terminal("uninstall failed.")
        return -1
    time.sleep(10)
    echo_to_terminal("(3/3)----------success.")
    return 0

def exec_build(args):
    if len(args) < 2:
        help_info()
        return -1
    if args[1] not in ["install", "uninstall"]:
        help_info()
        return -1
    global INSTALL_USER
    INSTALL_USER = input('input login user:')
    user_pattern = r'^[a-zA-Z][a-zA-Z0-9_-]{0,31}$'
    if re.match(user_pattern, INSTALL_USER) is None:
        error_log("invalid login username or password.")
        return False

    # 检查password
    global INSTALL_PASSWORD
    INSTALL_PASSWORD = getpass.getpass('input login password:')
    global DEFAULT_INSTALL_PATH
    if args[1] == "install":
        if len(args) != 5:
            help_info()
            return -1
        if len(args) == 5:
            global DEFAULT_DEPLOY_USER
            DEFAULT_DEPLOY_USER = args[3]
            global DEFAULT_DEPLOY_GROUP
            DEFAULT_DEPLOY_GROUP = args[4]
        return install_all_nodes(args[2])
    elif args[1] == "uninstall" and len(args) == 2:
        return uninstall_all_nodes()
    else:
        help_info()
        return -1


if __name__ == "__main__":
    ret = exec_build(sys.argv)
    sys.exit(ret)
