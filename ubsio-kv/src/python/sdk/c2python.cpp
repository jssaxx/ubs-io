/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <Python.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/pytypes.h>
#include <unistd.h>
#include <utility>
#include "ubsio_kvc_operation.h"
#include "ubsio_kvc_stream_manager.h"
#include "ubsio_nds_manager.h"
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_def.h"
#include "ubsio_kvc_err.h"

using namespace ock::ubsio;
using namespace ock::ubsio::nds;

namespace py = pybind11;

constexpr int MAX_KEY_LENGTH = 255;
constexpr int MAX_BATCH_OP_COUNT = 16 << 10; // 16k
DfcNdsManager &g_ndsManager = DfcNdsManager::Instance();

int32_t PyKvcKvClientInit(int32_t dev_id)
{
    if (KvcOperationInit(dev_id) != 0) {
        return -1;
    }
    return 0;
}

int32_t PyKvcKvPutData(const std::string &key, py::bytes value)
{
    Py_ssize_t size = PyBytes_GET_SIZE(value.ptr());
    if (key.empty() || size == 0) {
        LOG_ERROR("key or value is empty");
        return -1;
    }
    if (key.size() > MAX_KEY_LENGTH || key.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return -1;
    }
    py::buffer_info buf = py::buffer(value).request();
    char *data = static_cast<char *>(buf.ptr);
    size_t len = buf.size;
    if (KvcPutData(key, static_cast<void *>(data), len, 0) != 0) {
        return -1;
    }
    return 0;
}

int32_t PyKvcKvGetData(const std::string &key, py::bytes value)
{
    Py_ssize_t size = PyBytes_GET_SIZE(value.ptr());
    if (key.empty() || size == 0) {
        LOG_ERROR("key or value is empty");
        return -1;
    }
    if (key.size() > MAX_KEY_LENGTH || key.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return -1;
    }
    py::buffer_info buf = py::buffer(value).request();
    char *data = static_cast<char *>(buf.ptr);
    size_t len = buf.size;

    if (KvcGetData(key, static_cast<void *>(data), len, 0) != 0) {
        return -1;
    }
    return 0;
}

bool PyKvcKvExist(const std::string &key)
{
    if (key.empty()) {
        LOG_ERROR("key is empty");
        return false;
    }
    if (key.size() > MAX_KEY_LENGTH || key.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return false;
    }
    if (KvcExistKey(key, 0) != true) {
        return false;
    }
    return true;
}

int32_t PyKvcKvDelete(const std::string &key)
{
    if (key.empty()) {
        LOG_ERROR("key is empty");
        return -1;
    }
    if (key.size() > MAX_KEY_LENGTH || key.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return -1;
    }
    if (KvcDeleteKey(key, 0) != 0) {
        return -1;
    }
    return 0;
}

uint32_t PyKvcKvGetLength(const std::string &key)
{
    if (key.empty()) {
        LOG_ERROR("key is empty");
        return 0;
    }
    if (key.size() > MAX_KEY_LENGTH || key.size() < 1) {
        LOG_ERROR("key length exceeds limit");
        return 0;
    }
    uint32_t len = 0;
    if (KvcGetKeyLength(key, len, 0) != 0) {
        return 0;
    }
    return len;
}

void PyKvcKvExit(void)
{
    KvcExit();
}

std::vector<int> PyKvcKvBatchPutData(const std::vector<std::string> &keys, std::vector<py::bytes> &values)
{
    std::vector<int> results(keys.size(), -1);
    if (keys.empty() || values.empty() || keys.size() != values.size()) {
        LOG_ERROR("keys or values is empty");
        return results;
    }
    if (keys.size() > MAX_BATCH_OP_COUNT || keys.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return results;
    }
    std::vector<void *> data_ptrs;
    std::vector<size_t> lengths;
    data_ptrs.reserve(values.size());
    lengths.reserve(values.size());

    for (auto &buf : values) {
        PyObject *py_obj = buf.ptr();
        char *data;
        Py_ssize_t length;
        if (PyBytes_AsStringAndSize(py_obj, &data, &length) < 0) {
            LOG_ERROR("PyBytes_AsStringAndSize failed");
            return results;
        }
        data_ptrs.emplace_back(static_cast<void *>(data));
        lengths.emplace_back(static_cast<size_t>(length));
    }
    int32_t ret = KvcBatchPutData(keys, data_ptrs, lengths, results, 0);
    if (ret != 0) {
        LOG_ERROR("KvcBatchPutData failed");
    }
    return results;
}

std::vector<int> PyKvcKvBatchDelete(const std::vector<std::string> &keys)
{
    std::vector<int> results(keys.size(), -1);
    if (keys.empty()) {
        LOG_ERROR("keys is empty");
        return results;
    }
    if (keys.size() > MAX_BATCH_OP_COUNT || keys.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return results;
    }
    int32_t ret = KvcBatchDeleteKey(keys, results, 0);
    if (ret != 0) {
        LOG_ERROR("KvcBatchDeleteKey failed");
    }
    return results;
}

std::vector<uint32_t> PyKvcKvBatchGetLength(const std::vector<std::string> &keys)
{
    std::vector<uint32_t> lengths(keys.size(), 0);
    if (keys.empty()) {
        LOG_ERROR("keys or lengths is empty");
        return lengths;
    }
    if (keys.size() > MAX_BATCH_OP_COUNT || keys.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return lengths;
    }
    std::vector<int> results(keys.size(), -1);
    int32_t ret = KvcBatchGetLengthKey(keys, lengths, results, 0);
    if (ret != 0) {
        LOG_ERROR("KvcBatchGetLengthKey failed");
    }
    return lengths;
}

std::vector<int> PyKvcKvBatchGetData(const std::vector<std::string> &keys, std::vector<py::bytes> &values)
{
    int32_t ret = 0;
    std::vector<int> results(keys.size(), -1);
    if (keys.empty() || values.empty() || keys.size() != values.size()) {
        LOG_ERROR("keys or values is empty");
        return results;
    }
    if (keys.size() > MAX_BATCH_OP_COUNT || keys.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return results;
    }
    std::vector<void *> data_ptrs;
    std::vector<size_t> lengths;
    std::vector<void *> getDataPtr(keys.size(), nullptr);
    data_ptrs.reserve(values.size());
    lengths.reserve(values.size());

    for (auto &buf : values) {
        PyObject *py_obj = buf.ptr();
        char *data;
        Py_ssize_t length;
        if (PyBytes_AsStringAndSize(py_obj, &data, &length) < 0) {
            LOG_ERROR("PyBytes_AsStringAndSize failed");
            return results;
        }
        data_ptrs.emplace_back(static_cast<void *>(data));
        lengths.emplace_back(static_cast<size_t>(length));
    }
    ret = KvcBatchGetData(keys, getDataPtr.data(), lengths, results, 0);
    if (ret != 0) {
        LOG_ERROR("KvcBatchGetData failed");
        return results;
    }
    for (uint32_t i = 0; i < keys.size(); i++) {
        std::memcpy(reinterpret_cast<void *>(data_ptrs[i]), static_cast<uint8_t *>(getDataPtr[i]), lengths[i]);
    }
    KvcBatchFreeGetAddress(getDataPtr.data(), keys.size());
    return results;
}

std::vector<bool> PyKvcKvBatchExist(const std::vector<std::string> &keys)
{
    std::vector<bool> result(keys.size(), false);
    if (keys.empty()) {
        LOG_ERROR("keys is empty");
        return result;
    }
    if (keys.size() > MAX_BATCH_OP_COUNT || keys.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return result;
    }
    auto results = new (std::nothrow) bool[keys.size()];
    if (results == nullptr) {
        LOG_ERROR("new bool array failed");
        return result;
    }
    std::unique_ptr<bool[]> resultRelease(results);
    int32_t ret = KvcBatchExistKey(keys, results, 0);
    if (ret != 0) {
        LOG_ERROR("KvcBatchExistKey failed");
    }
    for (auto i = 0; i < keys.size(); i++) {
        result[i] = results[i];
    }
    return result;
}

int PyKvcNdsInit(int device)
{
    return g_ndsManager.Initialize(device);
}

int PyKvcNdsUninit()
{
    return g_ndsManager.UnInitialize();
}

int PyKvcNdsRegmem(uintptr_t addr, size_t length)
{
    return g_ndsManager.RegisterMemory(reinterpret_cast<const void *>(addr), length);
}

int PyKvcNdsUnregmem(uintptr_t addr, size_t length)
{
    return g_ndsManager.UnRegisterMemory(reinterpret_cast<const void *>(addr), length);
}

int PyKvcNdsRead(const std::string &key,
                 const std::vector<uintptr_t> &buffers,
                 const std::vector<size_t> &sizes)
{
    return g_ndsManager.DirectRead(key, buffers, sizes);
}

int PyKvcNdsBatchRead(const std::vector<std::string> &keys,
                      const std::vector<std::vector<uintptr_t>> &buffers,
                      const std::vector<std::vector<size_t>> &sizes)
{
    return g_ndsManager.BatchDirectRead(keys, buffers, sizes);
}

PYBIND11_MODULE(c2python_sdk, m)
{
    m.doc() = "Python/C API for python sdk";
    m.def("KvInit", &PyKvcKvClientInit, py::arg("dev_id"));
    m.def("KvExit", &PyKvcKvExit);
    m.def("KvPut", &PyKvcKvPutData, py::arg("key"), py::arg("value"));
    m.def("KvGet", &PyKvcKvGetData, py::arg("key"), py::arg("value"));
    m.def("KvExist", &PyKvcKvExist, py::arg("key"));
    m.def("KvDelete", &PyKvcKvDelete, py::arg("key"));
    m.def("KvGetLength", &PyKvcKvGetLength, py::arg("key"));
    m.def("KvBatchPut", &PyKvcKvBatchPutData, py::arg("keys"), py::arg("values"));
    m.def("KvBatchGet", &PyKvcKvBatchGetData, py::arg("keys"), py::arg("values"));
    m.def("KvBatchExist", &PyKvcKvBatchExist, py::arg("keys"));
    m.def("KvBatchDelete", &PyKvcKvBatchDelete, py::arg("keys"));
    m.def("KvBatchGetLength", &PyKvcKvBatchGetLength, py::arg("keys"));
    m.def("NdsInit", &PyKvcNdsInit, py::arg("device"));
    m.def("NdsUninit", &PyKvcNdsUninit);
    m.def("NdsRegmem", &PyKvcNdsRegmem, py::arg("addr"), py::arg("length"));
    m.def("NdsUnregmem", &PyKvcNdsUnregmem, py::arg("addr"), py::arg("length"));
    m.def("NdsRead", &PyKvcNdsRead, py::arg("key"), py::arg("buffers"), py::arg("sizes"));
    m.def("NdsBatchRead", &PyKvcNdsBatchRead, py::arg("keys"), py::arg("buffers"), py::arg("sizes"));
}