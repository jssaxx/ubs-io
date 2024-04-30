/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "ut_common.h"
#include "cm_zkadapter.h"

using namespace ock::bio;

namespace ock {
namespace bio {
void ZkFreeParaList()
{
    CmClientZkFreeParaList(0);
}
}
}