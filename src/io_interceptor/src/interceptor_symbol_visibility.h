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

#ifndef INTERCEPTOR_SYMBOL_VISIBILITY_H
#define INTERCEPTOR_SYMBOL_VISIBILITY_H

#if defined(__GNUC__)
    #define INTERCEPTOR_API __attribute__((visibility("default")))
    #define INTERCEPTOR_INTERNAL_API __attribute__((visibility("hidden")))
    #define INTERCEPTOR_API_DEPRECATED __attribute__((deprecated, visibility("default")))
    #define INTERCEPTOR_LOCAL  __attribute__((visibility("hidden")))
#endif
#endif // INTERCEPTOR_SYMBOL_VISIBILITY_H