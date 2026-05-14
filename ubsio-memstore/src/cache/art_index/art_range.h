/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef ART_RANGE_H
#define ART_RANGE_H

#include "art.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const unsigned char *key;
    int len;
} art_range_bound;

int art_search_range_external(art_tree *t, const art_range_bound *start_bound, const art_range_bound *end_bound,
                              art_callback cb, void *data);
#ifdef __cplusplus
}

#endif  // __cplusplus
#endif  // ART_RANGE_H
