#!/usr/bin/env python
# coding=utf-8

# ***********************************************************************
# Copyright: (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
# script for Huawei boostio to install pkg
# version: 1.0.0
# change log:
# ***********************************************************************
import subprocess
import logging
import os
import configparser

sp = subprocess.Popen(["/usr/bin/touch", "./boostio_hand_out_py.log"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                      stderr=subprocess.PIPE, shell=False, universal_newlines=True)
sp.communicate()
sp.wait()
sp = subprocess.Popen(["/usr/bin/chmod", "600", "./boostio_hand_out_py.log"], stdin=subprocess.PIPE,
                      stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=False, universal_newlines=True)
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
DEFAULT_HCOM_PORT = 9898
CONFIG_OBJ = {}