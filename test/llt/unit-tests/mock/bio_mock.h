/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef BOOSTIO_BIO_MOCK_H
#define BOOSTIO_BIO_MOCK_H

#include "mockcpp/mockcpp.hpp"

#define MOCKER_CPP(api, TT) MOCKCPP_NS::mockAPI(#api, reinterpret_cast<TT>(api))

#endif // BOOSTIO_BIO_MOCK_H
