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

#ifndef BOOSTIO_SERVER_DIAGNOSE_H
#define BOOSTIO_SERVER_DIAGNOSE_H

namespace ock {
    namespace bio {
        namespace diagnose {
            class BioServerCommand {
            public:
                static int Initialize() noexcept;
                static void Destroy() noexcept;
            };
        }
    }
}

#ifdef __cplusplus
extern "C" {
#endif

extern int ServerDiagnoseInit();

#ifdef __cplusplus
}
#endif

#endif //BOOSTIO_SERVER_DIAGNOSE_H