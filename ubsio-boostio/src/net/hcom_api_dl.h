/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#ifndef BIO_HCOM_API_DL_H
#define BIO_HCOM_API_DL_H

#include <cstddef>
#include <hcom/capi/hcom_service_c.h>
#include <mutex>

// Only C ABI symbols cross the shared-library boundary. Keep the declarations
// tied to the headers built and installed with the HCOM runtime.
#define BIO_HCOM_API_FUNCTIONS(F) \
    F(ubs_hcom_service_create) \
    F(ubs_hcom_service_destroy) \
    F(ubs_hcom_service_bind) \
    F(ubs_hcom_service_start) \
    F(ubs_hcom_service_connect) \
    F(ubs_hcom_service_add_workergroup) \
    F(ubs_hcom_service_register_broken_handler) \
    F(ubs_hcom_service_register_handler) \
    F(ubs_hcom_service_set_heartbeat_opt) \
    F(ubs_hcom_service_set_max_send_recv_data_cnt) \
    F(ubs_hcom_service_set_sq_size) \
    F(ubs_hcom_service_set_rq_size) \
    F(ubs_hcom_service_set_cq_depth) \
    F(ubs_hcom_service_set_polling_batchsize) \
    F(ubs_hcom_service_set_prepost_size) \
    F(ubs_hcom_service_set_polling_timeoutus) \
    F(ubs_hcom_service_set_max_connection_cnt) \
    F(ubs_hcom_service_set_tcp_usr_timeout) \
    F(ubs_hcom_service_set_ipmask) \
    F(ubs_hcom_service_set_tls_opt) \
    F(ubs_hcom_service_register_memory_region) \
    F(ubs_hcom_service_register_assign_memory_region) \
    F(ubs_hcom_service_destroy_memory_region) \
    F(ubs_hcom_service_get_memory_region_info) \
    F(ubs_hcom_channel_refer) \
    F(ubs_hcom_channel_derefer) \
    F(ubs_hcom_channel_call) \
    F(ubs_hcom_channel_reply) \
    F(ubs_hcom_channel_get) \
    F(ubs_hcom_channel_put) \
    F(ubs_hcom_channel_send_fds) \
    F(ubs_hcom_channel_recv_fds) \
    F(ubs_hcom_channel_set_timeout) \
    F(ubs_hcom_channel_get_id) \
    F(ubs_hcom_channel_set_up_ctx) \
    F(ubs_hcom_channel_get_up_ctx) \
    F(ubs_hcom_context_get_channel) \
    F(ubs_hcom_context_get_opcode) \
    F(ubs_hcom_context_get_data) \
    F(ubs_hcom_context_get_datalen) \
    F(ubs_hcom_context_get_result) \
    F(ubs_hcom_context_get_error_code) \
    F(ubs_hcom_context_get_rspctx) \
    F(ubs_hcom_context_clone) \
    F(ubs_hcom_context_free) \
    F(ubs_hcom_set_log_handler) \
    F(ubs_hcom_err_str)

namespace ock {
namespace bio {
class HcomApi {
public:
    static HcomApi &Instance();
    bool Load();
#define BIO_HCOM_DECLARE(name) decltype(&::name) name = nullptr;
    BIO_HCOM_API_FUNCTIONS(BIO_HCOM_DECLARE)
#undef BIO_HCOM_DECLARE

private:
    HcomApi() = default;
    std::mutex mutex_;
    void *handle_ = nullptr;
};
}
}
#endif
