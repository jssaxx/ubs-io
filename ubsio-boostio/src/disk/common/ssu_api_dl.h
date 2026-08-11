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

#ifndef SSU_API_DL_H
#define SSU_API_DL_H

#include <mutex>

#include "ubs_engine_ssu.h"

namespace ock {
namespace bio {

using ubs_ssu_alloc_info_list_fn = int32_t (*)(ubs_ssu_alloc_result_t **results, uint32_t *result_cnt);
using ubs_ssu_alloc_info_list_free_fn = void (*)(ubs_ssu_alloc_result_t **results, uint32_t result_cnt);
using ubs_ssu_ns_dev_paths_free_fn = void (*)(char ***ns_dev_paths, uint32_t ns_dev_path_cnt);
using ubs_ssu_ns_stats_get_fn = int32_t (*)(const char *name, ubs_ssu_ns_stats_t **ns_stats_list,
                                            uint32_t *ns_stats_cnt);
using ubs_ssu_ns_stats_free_fn = void (*)(ubs_ssu_ns_stats_t **ns_stats_list, uint32_t *ns_stats_cnt);
using ubs_ssu_connect_info_get_fn = int32_t (*)(const char *name, ubs_ub_vfe_t *vfe,
                                                ubs_ssu_connect_info_t **connect_info_list,
                                                uint32_t *connect_info_cnt);
using ubs_ssu_connect_info_free_fn = void (*)(ubs_ssu_connect_info_t **connect_info_list,
                                             uint32_t *connect_info_cnt);
using ubs_ssu_space_alloc_fn = int32_t (*)(const ubs_ssu_alloc_space_req_t *req,
                                          ubs_ssu_alloc_result_t **result);
using ubs_ssu_alloc_info_free_fn = void (*)(ubs_ssu_alloc_result_t **result);
using ubs_ssu_space_free_fn = int32_t (*)(const char *name);
using ubs_ssu_access_permission_add_fn = int32_t (*)(const char *name, const char *nqn);
using ubs_ssu_access_permission_remove_fn = int32_t (*)(const char *name, const char *nqn);
using ubs_ssu_space_attach_fn = int32_t (*)(const ubs_ssu_space_req_t *req, char ***ns_dev_paths,
                                           uint32_t *ns_dev_path_cnt);
using ubs_ssu_space_detach_fn = int32_t (*)(const ubs_ssu_space_req_t *req);
using ubs_ssu_linear_space_attach_fn = int32_t (*)(const ubs_ssu_linear_space_req_t *req, char ***ns_dev_paths,
                                                   uint32_t *ns_dev_path_cnt, char *dev_path);
using ubs_ssu_linear_space_detach_fn = int32_t (*)(const ubs_ssu_linear_space_req_t *req);
using ubs_ssu_striped_space_attach_fn = int32_t (*)(const ubs_ssu_striped_space_req_t *req, char ***ns_dev_paths,
                                                    uint32_t *ns_dev_path_cnt, char *dev_path);
using ubs_ssu_striped_space_detach_fn = int32_t (*)(const ubs_ssu_striped_space_req_t *req);
using ubs_ssu_fe_device_list_fn = int32_t (*)(ubs_ub_fe_t **fe_list, uint32_t *fe_cnt);
using ubs_ssu_fe_device_list_free_fn = void (*)(ubs_ub_fe_t **fe_list, uint32_t *fe_cnt);
using ubs_ssu_fe_device_alloc_fn = int32_t (*)(uint32_t upi, const ubs_ub_vfe_t *vfe, uint8_t *bus_instance_guid);
using ubs_ssu_fe_device_free_fn = int32_t (*)(uint32_t upi, const ubs_ub_vfe_t *vfe);

struct SsuApiOps {
    ubs_ssu_alloc_info_list_fn alloc_info_list;
    ubs_ssu_alloc_info_list_free_fn alloc_info_list_free;
    ubs_ssu_ns_dev_paths_free_fn ns_dev_paths_free;
    ubs_ssu_ns_stats_get_fn ns_stats_get;
    ubs_ssu_ns_stats_free_fn ns_stats_free;
    ubs_ssu_connect_info_get_fn connect_info_get;
    ubs_ssu_connect_info_free_fn connect_info_free;
    ubs_ssu_space_alloc_fn space_alloc;
    ubs_ssu_alloc_info_free_fn alloc_info_free;
    ubs_ssu_space_free_fn space_free;
    ubs_ssu_access_permission_add_fn access_permission_add;
    ubs_ssu_access_permission_remove_fn access_permission_remove;
    ubs_ssu_space_attach_fn space_attach;
    ubs_ssu_space_detach_fn space_detach;
    ubs_ssu_linear_space_attach_fn linear_space_attach;
    ubs_ssu_linear_space_detach_fn linear_space_detach;
    ubs_ssu_striped_space_attach_fn striped_space_attach;
    ubs_ssu_striped_space_detach_fn striped_space_detach;
    ubs_ssu_fe_device_list_fn fe_device_list;
    ubs_ssu_fe_device_list_free_fn fe_device_list_free;
    ubs_ssu_fe_device_alloc_fn fe_device_alloc;
    ubs_ssu_fe_device_free_fn fe_device_free;
};

class DlSsuApi {
public:
    static SsuApiOps *apiOps;
    static bool gStarted;
    static int LoadSsuApiDl();
    static int UnloadSsuApiDl();

private:
    static std::mutex sMutex;
    static const char *gSsuLibName;
    static void *gSsuSdk;
};
} // namespace bio
} // namespace ock

#endif // SSU_API_DL_H
