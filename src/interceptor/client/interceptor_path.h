/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
 */

#ifndef BOOSTIO_INTERCEPTOR_PATH_H
#define BOOSTIO_INTERCEPTOR_PATH_H

#include <string>

namespace ock {
namespace bio {
std::string AddPrefix(const std::string &prefix, const char *rawPath);

std::string GetPathNoPrefix(const std::string &path, const std::string &prefix);

std::string GetCWD();
}
}
#endif // BOOSTIO_INTERCEPTOR_PATH_H
