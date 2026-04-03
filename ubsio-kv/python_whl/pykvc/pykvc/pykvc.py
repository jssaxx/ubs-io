#!/usr/bin/env python
# coding: utf-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.

global KvInit, KvPut, KvBatchGet, KvBatchExist, \
    DfcNdsInit, DfcNdsUninit, DfcNdsRegmem, DfcNdsUnregmem, DfcNdsRead, DfcNdsBatchRead
from c2python_sdk import (KvInit, KvPut, KvBatchGet, KvBatchExist,
                          DfcNdsInit, DfcNdsUninit, DfcNdsRegmem, DfcNdsUnregmem, DfcNdsRead, DfcNdsBatchRead)


def initialize(device_id=-1) -> int:
    """
    Initialize client of UBS-IO KV Cache
    :param device_id: device_id
    :return: 0 for success
            -1 for failed
    """
    return KvInit(device_id)


def put(key, value) -> int:
    """
    Put data of object with key into UBS-IO KV Cache
    :param key: key of data, less than 256
    :param value: data to be put
    :return: 0 for success
            -1 for failed
    """
    return KvPut(key, value)


def batch_get(keys, values) -> list:
    """
    Get multiple data objects by keys from UBS-IO KV Cache
    :param keys: list of keys for the data objects
    :param values: list of data values to be gotten
    :return: result list
    """
    return KvBatchGet(keys, values)


def batch_exist(keys) -> list:
    """
    Determine whether the list of keys is within the UBS-IO KV Cache
    :param keys: list of keys for the data objects
    :return: result list
    """
    return KvBatchExist(keys)


def nds_init(device: int) -> int:
    """
    Initialize NDS(NPU Direct Storage).
    :param device: local npu device index.
    :return: 0 for success
            -1 for failed
    """
    return DfcNdsInit(device)


def nds_uninit() -> int:
    """
    UnInitialize NDS(NPU Direct Storage).
    :return: 0 for success
            -1 for failed
    """
    return DfcNdsUninit()


def nds_regmem(addr, length) -> int:
    """
    Register NDS memory.
    :return: 0 for success
            -1 for failed
    """
    return DfcNdsRegmem(addr, length)


def nds_unregmem(addr, length) -> int:
    """
    UnRegister NDS memory.
    :return: 0 for success
            -1 for failed
    """
    return DfcNdsUnregmem(addr, length)


def nds_read(key: str, buffers: list[int], sizes: list[int]) -> int:
    """
    UnRegister NDS memory.
    :return: 0 for success
            -1 for failed
    """
    return DfcNdsRead(key, buffers, sizes)


def nds_batch_read(keys: list[str], buffers: list[list[int]], sizes: list[int]) -> list[int]:
    return DfcNdsBatchRead(keys, buffers, sizes)
