/**
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <Python.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/pytypes.h>
#include <unistd.h>
#include <utility>
#include "dfc_client_operation.h"
#include "dfc_nds_manager.h"
#include "dfc_log.h"
#include "dfc_def.h"
#include "dfc_err.h"

using namespace ock::dfc;
using namespace ock::dfc::nds;

namespace py = pybind11;

constexpr int MAX_KEY_LENGTH = 255;
constexpr int MAX_BATCH_OP_COUNT = 256;
DfcNdsManager &g_ndsManager = DfcNdsManager::Instance();

static KvWorkerMode g_workerMode;
int32_t PyDfcKvClientInit(const std::string &mode, int32_t dev_id)
{
    g_workerMode = mode == "convergence" ? KvWorkerMode::KV_CONVERGENCE : KvWorkerMode::KV_SEPARATES;
    if (DfcKvClientInit(g_workerMode, dev_id) != 0) {
        return -1;
    }
    return 0;
}

int32_t PyDfcKvPutData(const std::string &key, py::bytes value)
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
    if (DfcKvPutData(key, static_cast<void *>(data), len, 0) != 0) {
        return -1;
    }
    return 0;
}

int32_t PyDfcKvGetData(const std::string &key, py::bytes value)
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

    if (DfcKvGetData(key, static_cast<void *>(data), len, 0) != 0) {
        return -1;
    }

    return 0;
}

bool PyDfcKvExist(const std::string &key)
{
    if (key.empty()) {
        LOG_ERROR("key is empty");
        return false;
    }
    if (key.size() > MAX_KEY_LENGTH || key.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return false;
    }
    if (DfcKvExistKey(key, 0) != true) {
        return false;
    }
    return true;
}

int32_t PyDfcKvDelete(const std::string &key)
{
    if (key.empty()) {
        LOG_ERROR("key is empty");
        return -1;
    }
    if (key.size() > MAX_KEY_LENGTH || key.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return -1;
    }
    if (DfcKvDeleteKey(key, 0) != 0) {
        return -1;
    }
    return 0;
}

uint32_t PyDfcKvGetLength(const std::string &key)
{
    if (key.empty()) {
        LOG_ERROR("key is empty");
        return 0;
    }
    if (key.size() > MAX_KEY_LENGTH || key.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return 0;
    }
    uint32_t len = 0;
    if (DfcGetKeyLength(key, len, 0) != 0) {
        return 0;
    }
    return len;
}

void PyDfcKvExit(void)
{
    DfcConBioExit();
}

std::vector<int> PyDfcKvBatchPutData(const std::vector<std::string> &keys, std::vector<py::bytes> &values)
{
    if (keys.empty() || values.empty() || keys.size() != values.size()) {
        LOG_ERROR("keys or values is empty");
        return {};
    }
    if (keys.size() > MAX_BATCH_OP_COUNT || keys.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return {};
    }
    std::vector<void *> data_ptrs;
    std::vector<size_t> lengths;
    std::vector<int> results(keys.size(), -1);
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
    int32_t ret = DfcBatchPutData(keys, data_ptrs, lengths, results, 0);
    if (ret != 0) {
        LOG_ERROR("DfcBatchPutData failed");
    }
    return results;
}

std::vector<int> PyDfcKvBatchGetData(const std::vector<std::string> &keys, std::vector<py::bytes> &values)
{
    int32_t ret = 0;
    if (keys.empty() || values.empty() || keys.size() != values.size()) {
        LOG_ERROR("keys or values is empty");
        return {};
    }
    if (keys.size() > MAX_BATCH_OP_COUNT || keys.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return {};
    }
    std::vector<void *> data_ptrs;
    std::vector<size_t> lengths;
    std::vector<int> results(keys.size(), -1);
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
    if (g_workerMode == KvWorkerMode::KV_SEPARATES) {
        ret = DfcBatchGetData(keys, data_ptrs.data(), lengths, results, 0);
        if (ret != 0) {
            LOG_ERROR("DfcBatchGetData failed");
            return results;
        }
        return results;
    }
    ret = DfcBatchGetData(keys, getDataPtr.data(), lengths, results, 0);
    if (ret != 0) {
        LOG_ERROR("DfcBatchGetData failed");
        return results;
    }
    for (uint32_t i = 0; i < keys.size(); i++) {
        std::memcpy(reinterpret_cast<void *>(data_ptrs[i]), static_cast<uint8_t *>(getDataPtr[i]), lengths[i]);
    }
    DfcBatchFreeGetAddress(getDataPtr.data(), keys.size());
    return results;
}

std::vector<bool> PyDfcKvBatchExist(const std::vector<std::string> &keys)
{
    if (keys.empty()) {
        LOG_ERROR("keys is empty");
        return {};
    }
    if (keys.size() > MAX_BATCH_OP_COUNT || keys.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return {};
    }
    std::vector<bool> result(keys.size(), false);
    auto results = new (std::nothrow) bool[keys.size()];
    if (results == nullptr) {
        LOG_ERROR("new bool array failed");
        return result;
    }
    std::unique_ptr<bool[]> resultRelease(results);
    int32_t ret = DfcBatchExistKey(keys, results, 0);
    if (ret != 0) {
        LOG_ERROR("DfcBatchExistKey failed");
    }
    for (auto i = 0; i < keys.size(); i++) {
        result[i] = results[i];
    }
    return result;
}

std::vector<int> PyDfcKvBatchDelete(const std::vector<std::string> &keys)
{
    if (keys.empty()) {
        LOG_ERROR("keys is empty");
        return {};
    }
    if (keys.size() > MAX_BATCH_OP_COUNT || keys.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return {};
    }
    std::vector<int> results(keys.size(), -1);
    int32_t ret = DfcBatchDeleteKey(keys, results, 0);
    if (ret != 0) {
        LOG_ERROR("DfcBatchDeleteKey failed");
    }
    return results;
}

std::vector<uint32_t> PyDfcKvBatchGetLength(const std::vector<std::string> &keys)
{
    if (keys.empty()) {
        LOG_ERROR("keys or lengths is empty");
        return {};
    }
    if (keys.size() > MAX_BATCH_OP_COUNT || keys.size() < 1) {
        LOG_ERROR("keys length exceeds limit");
        return {};
    }
    std::vector<uint32_t> lengths(keys.size(), 0);
    std::vector<int> results(keys.size(), -1);
    int32_t ret = DfcBatchGetLengthKey(keys, lengths, results, 0);
    if (ret != 0) {
        LOG_ERROR("DfcBatchGetLengthKey failed");
    }
    return lengths;
}

int PyDfcNdsInit(int device)
{
    return g_ndsManager.Initialize(device);
}

int PyDfcNdsUninit()
{
    return g_ndsManager.UnInitialize();
}

int PyDfcNdsRegmem(uintptr_t addr, size_t length)
{
    return g_ndsManager.RegisterMemory(reinterpret_cast<const void *>(addr), length);
}

int PyDfcNdsUnregmem(uintptr_t addr, size_t length)
{
    return g_ndsManager.UnRegisterMemory(reinterpret_cast<const void *>(addr), length);
}

int PyDfcNdsRead(const std::string &key,
                 const std::vector<uintptr_t> &buffers,
                 const std::vector<size_t> &sizes)
{
    return g_ndsManager.DirectRead(key, buffers, sizes);
}

int PyDfcNdsBatchRead(const std::vector<std::string> &keys,
                      const std::vector<std::vector<uintptr_t>> &buffers,
                      const std::vector<std::vector<size_t>> &sizes)
{
    return g_ndsManager.BatchDirectRead(keys, buffers, sizes);
}

PYBIND11_MODULE(c2python_sdk, m)
{
    m.doc() = "Python/C API for python sdk";
    m.def("KvInit", &PyDfcKvClientInit, py::arg("mode"), py::arg("dev_id"));
    m.def("KvExit", &PyDfcKvExit);
    m.def("KvPut", &PyDfcKvPutData, py::arg("key"), py::arg("value"));
    m.def("KvGet", &PyDfcKvGetData, py::arg("key"), py::arg("value"));
    m.def("KvExist", &PyDfcKvExist, py::arg("key"));
    m.def("KvDelete", &PyDfcKvDelete, py::arg("key"));
    m.def("KvGetLength", &PyDfcKvGetLength, py::arg("key"));
    m.def("KvBatchPut", &PyDfcKvBatchPutData, py::arg("keys"), py::arg("values"));
    m.def("KvBatchGet", &PyDfcKvBatchGetData, py::arg("keys"), py::arg("values"));
    m.def("KvBatchExist", &PyDfcKvBatchExist, py::arg("keys"));
    m.def("KvBatchDelete", &PyDfcKvBatchDelete, py::arg("keys"));
    m.def("KvBatchGetLength", &PyDfcKvBatchGetLength, py::arg("keys"));
    m.def("DfcNdsInit", &PyDfcNdsInit, py::arg("device"));
    m.def("DfcNdsUninit", &PyDfcNdsUninit);
    m.def("DfcNdsRegmem", &PyDfcNdsRegmem, py::arg("addr"), py::arg("length"));
    m.def("DfcNdsUnregmem", &PyDfcNdsUnregmem, py::arg("addr"), py::arg("length"));
    m.def("DfcNdsRead", &PyDfcNdsRead, py::arg("key"), py::arg("buffers"), py::arg("sizes"));
    m.def("DfcNdsBatchRead", &PyDfcNdsBatchRead, py::arg("keys"), py::arg("buffers"), py::arg("sizes"));
}