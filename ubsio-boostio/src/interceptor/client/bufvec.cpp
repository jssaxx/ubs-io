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

#include <cstdint>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <linux/fs.h>
#include "interceptor_log.h"
#include "bio_def.h"
#include "securec.h"
#include "bufvec.h"

using namespace ock::bio;

BufVec::BufVec(const iovec *vec, int cnt) noexcept : iov{ vec }, count{ cnt }, size{ ComputeSize(vec, cnt) } {}

void BufVec::Reset() noexcept
{
    index = 0;
    innerOffset = 0;
    totalOffset = 0;
}

ssize_t BufVec::Read(uint8_t *buf, size_t length) noexcept
{
    if (buf == nullptr || length == 0) {
        return -1;
    }
    size_t copyBytes = 0;
    size_t cpyLength = length;
    while (copyBytes < length && index < count) {
        if (innerOffset < iov[index].iov_len) {
            auto bytes = std::min(length - copyBytes, iov[index].iov_len - innerOffset);
            int ret = memcpy_s(buf + copyBytes, cpyLength,
                               (const uint8_t *)iov[index].iov_base + innerOffset, bytes);
            if (UNLIKELY(ret != 0)) {
                CLOG_ERROR("Memory copy failed, ret:" << ret << ".");
                return -1;
            }
            copyBytes += bytes;
            innerOffset += bytes;
            if (SIZE_MAX - totalOffset < bytes) {
                return -1;
            }
            totalOffset += bytes;
            cpyLength -= bytes;
        } else {
            index++;
            innerOffset = 0;
        }
    }

    return copyBytes;
}

ssize_t BufVec::Write(const uint8_t *buf, size_t length) noexcept
{
    size_t copyBytes = 0;
    while (copyBytes < length && index < count) {
        if (innerOffset < iov[index].iov_len) {
            auto bytes = std::min(length - copyBytes, iov[index].iov_len - innerOffset);
            int ret = memcpy_s((uint8_t *)iov[index].iov_base + innerOffset, iov[index].iov_len - innerOffset,
                buf + copyBytes, bytes);
            if (UNLIKELY(ret != 0)) {
                CLOG_ERROR("Memory copy failed, ret:" << ret << ".");
                return -1;
            }
            copyBytes += bytes;
            innerOffset += bytes;
            if (SIZE_MAX - totalOffset < bytes) {
                return -1;
            }
            totalOffset += bytes;
        } else {
            index++;
            innerOffset = 0;
        }
    }

    return copyBytes;
}

size_t BufVec::ComputeSize(const iovec *vec, int cnt) noexcept
{
    if (UNLIKELY(vec == nullptr)) {
        CLOG_ERROR("Input param vec is nullptr.");
        errno = ENOMEM;
        return 0;
    }

    size_t computeSize = 0;
    for (auto i = 0; i < cnt; i++) {
        if (SIZE_MAX - computeSize < vec[i].iov_len) {
            CLOG_ERROR("Sum over SIZE_MAX.");
            errno = E2BIG;
            return 0;
        }
        computeSize += vec[i].iov_len;
    }

    return computeSize;
}
