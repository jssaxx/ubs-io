#!/usr/bin/env python
# coding: utf-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.

import os
import sys

current_path = os.path.abspath(__file__)
current_dir = os.path.dirname(current_path)
sys.path.append(current_dir)

from .pykvc import (initialize, put, batch_get, batch_exist, 
                    nds_init, nds_uninit, nds_regmem, nds_unregmem, nds_read, nds_batch_read)
