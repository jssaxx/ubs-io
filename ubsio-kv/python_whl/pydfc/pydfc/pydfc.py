#!/usr/bin/env python
# coding: utf-8
# Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.

global KvInit, KvExit, KvPut, KvGet, KvExist, KvDelete, KvGetLength, \
    KvBatchPut, KvBatchGet, KvBatchExist, KvBatchDelete, KvBatchGetLength, \
    DfcNdsInit, DfcNdsUninit, DfcNdsRegmem, DfcNdsUnregmem, DfcNdsRead, DfcNdsBatchRead
from c2python_sdk import (KvInit, KvExit, KvPut, KvGet, KvExist, KvDelete, KvGetLength, KvBatchPut,
                          KvBatchGet, KvBatchExist, KvBatchDelete, KvBatchGetLength,
                          DfcNdsInit, DfcNdsUninit, DfcNdsRegmem, DfcNdsUnregmem, DfcNdsRead, DfcNdsBatchRead)


def initialize(mode='convergence', device_id=-1) -> int:
    """
    Initialize client of Distributed File Cache
    :param mode: convergence or separates
    :return: 0 for success
            -1 for failed
    """
    return KvInit(mode, device_id)


def exit():
    """
    Dfc exit boostio service
    """
    KvExit()


def put(key, value) -> int:
    """
    Put data of object with key into Distributed File Cache
    :param key: key of data, less than 256
    :param value: data to be put
    :return: 0 for success
            -1 for failed
    """
    return KvPut(key, value)


def get(key, value) -> int:
    """
    Get data of object by key from Distributed File Cache
    :param key: key of data, less than 256
    :param value: data to be gotten
    :return: 0 for success
            -1 for failed
    """
    return KvGet(key, value)


def delete(key) -> int:
    """
    Delete the object with key from Distributed File Cache
    :param key: key of data, less than 256
    :return: 0 for success
            -1 for failed
    """
    return KvDelete(key)


def exist(key) -> int:
    """
    Determine whether the key is within the Distributed File Cache
    :param key: key of data, less than 256
    :return: 0 for success
            -1 for failed
    """
    return KvExist(key)


def get_length(key) -> int:
    """
    Determine whether the key is within the Distributed File Cache
    :param key: key of data, less than 256
    :return: length of key data
             0 for key get length failed
    """
    return KvGetLength(key)


def batch_put(keys, values) -> list:
    """
    Put multiple data objects into Distributed File Cache
    :param keys: list of keys for the data objects
    :param values: list of data values to be put
    :return: result list
             0 for success
            -1 for failed
    """
    return KvBatchPut(keys, values)


def batch_get(keys, values) -> list:
    """
    Get multiple data objects by keys from Distributed File Cache
    :param keys: list of keys for the data objects
    :param values: list of data values to be gotten
    :return: result list
    """
    return KvBatchGet(keys, values)


def batch_exist(keys) -> list:
    """
    Determine whether the list of keys is within the Distributed File Cache
    :param keys: list of keys for the data objects
    :return: result list
    """
    return KvBatchExist(keys)


def batch_delete(keys) -> list:
    """
    Delete multiple keys from the Distributed File Cache
    :param keys: list of keys for the data objects
    :return: result list
    """
    return KvBatchDelete(keys)


def batch_get_length(keys) -> list:
    """
    Get multiple keys length from the Distributed File Cache
    :param keys: list of keys for the data objects
    :return: list of length for key data objects,
            length=0 for key get length failed
    """
    return KvBatchGetLength(keys)


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
