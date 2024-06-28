/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
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