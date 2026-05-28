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

#ifndef BOOSTIO_LOCALSYSTEM_H
#define BOOSTIO_LOCALSYSTEM_H

#include "file_system.h"

namespace ock {
namespace bio {
class LocalSystem : public FileSystem {
public:
    BResult Init() override;

    void Stop() override;

    BResult Put(const char *key, const char *value, const size_t len) override;

    BResult Get(const char *key, char *value, const size_t len, const uint64_t off) override;

    BResult Delete(const char *key) override;

    BResult Stat(const char *key, ObjStat &objStat) override;

    BResult List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) override;

private:
    std::string mEmulationCephPath;
};
} // namespace bio
} // namespace ock

#endif // BOOSTIO_LOCALSYSTEM_H
