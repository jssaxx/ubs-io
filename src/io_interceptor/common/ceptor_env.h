/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2021-2023. All rights reserved.
 */

#ifndef ENV_H
#define ENV_H
#include <string>

namespace ock {
namespace interceptor {
namespace env {
std::string GetEnv(const std::string& key, const std::string& defaultVal);

std::string GetCWD();
}
}
}
#endif // ENV_H