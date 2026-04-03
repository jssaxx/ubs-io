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
#include "ubsio_kvc_log.h"
#include "ubsio_kvc_def.h"
#include "ubsio_kvc_err.h"

using namespace ock::ubsio;
using namespace ock::ubsio::nds;

namespace py = pybind11;

constexpr int MAX_KEY_LENGTH = 255;
constexpr int MAX_BATCH_OP_COUNT = 256;
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

std::vector<int> PyKvcKvBatchGetData(const std::vector<std::string> &keys, std::vector<py::bytes> &values)
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
    int32_t ret = KvcBatchExistKey(keys, results, 0);
    if (ret != 0) {
        LOG_ERROR("KvcBatchExistKey failed");
    }
    for (auto i = 0; i < keys.size(); i++) {
        result[i] = results[i];
    }
    return result;
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
    m.def("KvInit", &PyKvcKvClientInit, py::arg("dev_id"));
    m.def("KvPut", &PyKvcKvPutData, py::arg("key"), py::arg("value"));
    m.def("KvBatchGet", &PyKvcKvBatchGetData, py::arg("keys"), py::arg("values"));
    m.def("KvBatchExist", &PyKvcKvBatchExist, py::arg("keys"));
    m.def("DfcNdsInit", &PyDfcNdsInit, py::arg("device"));
    m.def("DfcNdsUninit", &PyDfcNdsUninit);
    m.def("DfcNdsRegmem", &PyDfcNdsRegmem, py::arg("addr"), py::arg("length"));
    m.def("DfcNdsUnregmem", &PyDfcNdsUnregmem, py::arg("addr"), py::arg("length"));
    m.def("DfcNdsRead", &PyDfcNdsRead, py::arg("key"), py::arg("buffers"), py::arg("sizes"));
    m.def("DfcNdsBatchRead", &PyDfcNdsBatchRead, py::arg("keys"), py::arg("buffers"), py::arg("sizes"));
}