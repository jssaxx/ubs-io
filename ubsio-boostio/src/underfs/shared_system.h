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

#ifndef BOOSTIO_SHAREDSYSTEM_H
#define BOOSTIO_SHAREDSYSTEM_H

#include "local_system.h"

namespace ock {
namespace bio {
class SharedSystem : public LocalSystem {
public:
    BResult Init() noexcept override;

    void Stop() noexcept override;

    BResult Put(const char *key, const char *value, const size_t len) noexcept override;

    BResult PutV(const char *key, const struct iovec *iov, size_t iovcnt) noexcept override;

    BResult Delete(const char *key) noexcept override;

    BResult Stat(const char *key, ObjStat &objStat) noexcept override;

protected:
    BResult LookupFile(const char *key, std::string &filePath, uint64_t &valueSize,
        FileReference &reference) noexcept override;
};
}
}

#endif // BOOSTIO_SHAREDSYSTEM_H
