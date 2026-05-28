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

#ifndef BOOSTIO_FILESYSTEM_H
#define BOOSTIO_FILESYSTEM_H

#include <string>
#include <unordered_map>

#include "bio_err.h"
#include "bio_ref.h"

namespace ock {
namespace bio {
constexpr uint64_t IO_MAX_LEN = 4194304;
class FileSystem {
public:
    struct ObjStat {
        uint64_t size;
        time_t time;
    };

    virtual BResult Init() = 0;

    virtual void Stop() = 0;

    virtual BResult Put(const char *key, const char *value, const size_t len) = 0;

    virtual BResult Get(const char *key, char *value, const size_t len, const uint64_t off) = 0;

    virtual BResult Delete(const char *key) = 0;

    virtual BResult Stat(const char *key, ObjStat &objStat) = 0;

    virtual BResult List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) = 0;

protected:
    bool mInited{false};
};
} // namespace bio
} // namespace ock
#endif // BOOSTIO_FILESYSTEM_H
