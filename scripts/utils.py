#!/usr/bin/env python
# coding=utf-8

# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# script for Huawei ubs-io to install pkg
# version: 1.0.0
# ***********************************************************************
import subprocess
import logging
import shutil
import copy
import os
import var


def echo_to_terminal(message):
    lines = message.splitlines()
    for line in lines:
        print(line)


def help_info():
    echo_to_terminal(var.USAGE)
    return


def home_path_of(user):
    return "/home"


def check_threads_alive(thread_list):
    # 如果线程结束便移除线程列表
    for thread in thread_list:
        if not thread.is_alive():
            thread_list.remove(thread)
    return len(thread_list)


def ssh_cmd(node, path, command):
    ip_str = "ip"
    port_str = "port"
    cmd = "bash {0} {1} {2} {3} {4} \"{5}\"" \
        .format(var.SSH_CMD_SHELL_PATH, node[ip_str], node["user"], node[port_str], path, command)
    new_cmd = ["{0}".format(var.SSH_CMD_SHELL_PATH), "{0}".format(node[ip_str]),
               "{0}".format(node["user"]), "{0}".format(node[port_str]), "{0}".format(path), "{0}".format(command)]
    sp_local = subprocess.Popen(new_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                shell=False, universal_newlines=True)
    stdout, stderr = sp_local.communicate(node["password"])
    sp_local.wait()
    result = stdout
    # 日志记录返回结果
    logging.info("pid:{0} node({1}:{2}); cmd({3}):\n {4}".format(var.PID, node[ip_str], node[port_str], cmd, result))
    if "Execute the SSH command successfully" in result:
        logging.info(
            "pid:{0} Execution completed--node({1}:{2});cmd({3})".format(var.PID, node[ip_str],
                                                                         node[port_str], command))
    else:
        logging.error(
            "pid:{0} SSH command run failed.--node({1}:{2});cmd({3})".format(var.PID, node[ip_str],
                                                                             node[port_str], command))
        if "bash install.sh install" in command:
            if "recode: 2" in result:
                echo_to_terminal("{0} The user group:{1} and its user:{2} do not exist.".
                                 format(node[ip_str], var.DEFAULT_DEPLOY_GROUP, var.DEFAULT_DEPLOY_USER))
            elif "recode: 3" in result:
                echo_to_terminal("{0} The specified user:{1} does not exist in the user group:{2}.".
                                 format(node[ip_str], var.DEFAULT_DEPLOY_USER, var.DEFAULT_DEPLOY_GROUP))
            elif "recode: 4" in result:
                echo_to_terminal("{0} erase the keys failed, please check it.".
                                 format(node[ip_str]))
        if "bash install.sh uninstall" in command:
            if "recode: 4" in result:
                echo_to_terminal("{0} erase the keys failed, please check it.".
                                 format(node[ip_str]))
        return -1
    return 0



def send_files_to_node(node):
    ip_str = "ip"
    net_str = "net"
    port_str = "port"
    disk_str = "disk"
    node_id = "node id"
    if ssh_cmd(node, home_path_of(node["user"]),
               "rm -rf /home/{0}/*;".format(node[ip_str])) != 0:
        logging.info("pid:{0} clear /home/{1} fails. ".format(var.PID, node[ip_str]))
    config = copy.deepcopy(var.g_config)
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
    will_sent_files = [var.PACKAGE_PATH, new_config_path]
    for sent_file in will_sent_files:
        echo_to_terminal("node{0}, file:{1}".format(node[node_id], sent_file))
        new_cmd = ["{0}".format(var.SCP_FILE_SHELL_PATH), "{0}".format(node[ip_str]), "{0}".format(node["user"]),
                  "{0}".format(node[port_str]),
                  "{0}".format(sent_file), "{0}".format("/home/{0}".format(node[ip_str]))]
        sp_local = subprocess.Popen(new_cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    shell=False, universal_newlines=True)
        stdout, stderr = sp_local.communicate(node["password"])
        sp_local.wait()
        result = stdout
        logging.info("pid:{0} node({1}:{2}); send file({3})\nresult: {4}".format(var.PID, node[ip_str], node[port_str],
                                                                                 sent_file, result))
        if "Execute SCP successful" in result:
            logging.info(
                "pid:{0} Execution succeeded--node({1}:{2}); send file({3}) success.".format(var.PID, node[ip_str],
                                                                                             node[port_str], sent_file))
        else:
            var.failed_times.append(1)
            cmd = "bash {0} {1} {2} {3} {4} {5} " \
                .format(var.SCP_FILE_SHELL_PATH, node[ip_str], node["user"], node[port_str], sent_file,
                        "/home/{0}".format(node[ip_str]))
            logging.error("pid:{0} Failed to execute SCP. (cmd:{1})".format(var.PID, cmd))
            if os.path.exists(folder_path):
                shutil.rmtree(folder_path)
            return -1
    if os.path.exists(folder_path):
        shutil.rmtree(folder_path)
    return 0


def check_config(conf_path):
    var.g_config.read(conf_path, encoding='utf-8')
    if "bio.log.level" not in var.g_config["bio"]:
        echo_to_terminal("config does not contain bio.log.level.")
        return -1
    else:
        boostio_log_level = var.g_config["bio"]["bio.log.level"]
        if boostio_log_level.strip() == "":
            echo_to_terminal("bio.log.level is null.")
            return -1

    if "bio.cm.pts_count" not in var.g_config["bio"]:
        echo_to_terminal("config does not contain bio.cmc.pts_count.")
        return -1
    else:
        cm_pt_count = int(var.g_config["bio"]["bio.cm.pts_count"])
        if cm_pt_count < 1 or cm_pt_count > 8192:
            echo_to_terminal("max_open_files is not in range 1 - 8192.")
            return -1

    if "bio.cm.register_timeout_sec" not in var.g_config["bio"]:
        echo_to_terminal("config does not contain bio.cm.register_timeout_sec.")
        return -1
    else:
        cm_register_timeout_sec = var.g_config["bio"]["bio.cm.register_timeout_sec"]
        if cm_register_timeout_sec.strip() == "":
            echo_to_terminal("bio.cm.register_timeout_sec is null.")
            return -1

    if "bio.cm.register_perm_timeout_sec" not in var.g_config["bio"]:
        echo_to_terminal("config does not contain bio.cm.register_perm_timeout_sec.")
        return -1
    else:
        cm_register_perm_timeout_sec = var.g_config["bio"]["bio.cm.register_perm_timeout_sec"]
        if cm_register_perm_timeout_sec.strip() == "":
            echo_to_terminal("bio.cm.register_perm_timeout_sec is null.")
            return -1

    if "bio.net.data.protocol" not in var.g_config["bio"]:
        echo_to_terminal("config does not contain bio.net.data.protocol.")
        return -1
    else:
        net_data_protocol = (var.g_config["bio"]["bio.net.data.protocol"])
        if net_data_protocol.strip() != "rdma" and net_data_protocol.strip() != "tcp":
            echo_to_terminal("bio.net.data.protocol is not in rdma/tcp")
            return -1

    if "bio.segment.size_in_mb" not in var.g_config["bio"]:
        echo_to_terminal("config does not bio.segment.size_in_mb.")
        return -1

    if "bio.mem.size_in_gb" not in var.g_config["bio"]:
        echo_to_terminal("config does not contain bio.mem.size_in_gb.")
        return -1
    return 0


def decompress_pkg(node):
    ip_str = "ip"
    if ssh_cmd(node, home_path_of(node["user"]) + "/{0}".format(node[ip_str]),
               "tar -xvf /home/{0}/{1} -C /home/{2};chown -R {3}:{4} /home/{2}/*;chmod -R 700 /home/{2}/*"
               .format(node[ip_str], var.PACKAGE_NAME, node[ip_str], var.INSTALL_USER, var.INSTALL_USER)) != 0:
        logging.error("pid:{0} decompress package fails.".format(var.PID))
        return -1
    return 0


def error_log(string):
    echo_to_terminal(string)
    logging.error(string)


def check_cms(pkg_path):
    if not os.path.exists(pkg_path):
        logging.error("pid:{0} get package failed :{1}".format(var.PID, pkg_path))
    return 0


def key_is_in_dict(input_key, input_dict):
    if input_key not in input_dict:
        return False
    else:
        return True