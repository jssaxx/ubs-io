/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
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
    while (copyBytes < length && index < count) {
        if (innerOffset < iov[index].iov_len) {
            auto bytes = std::min(length - copyBytes, iov[index].iov_len - innerOffset);
            int ret = memcpy_s(buf + copyBytes, bytes, (const uint8_t *)iov[index].iov_base + innerOffset, bytes);
            if (UNLIKELY(ret != 0)) {
                CLOG_ERROR("Memmcpy copy failed, ret:" << ret);
                return -1;
            }
            size_t oldCopyBytes = copyBytes;
            size_t oldInnerOffset = innerOffset;
            size_t oldTotalOffset = totalOffset;
            copyBytes += bytes;
            innerOffset += bytes;
            totalOffset += bytes;
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
                CLOG_ERROR("Memmcpy copy failed, ret:" << ret);
                return -1;
            }
            size_t oldCopyBytes = copyBytes;
            size_t oldInnerOffset = innerOffset;
            size_t oldTotalOffset = totalOffset;
            copyBytes += bytes;
            innerOffset += bytes;
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
        CLOG_ERROR("param invalid.");
        errno = ENOMEM;
        return 0;
    }

    size_t computeSize = 0;
    for (auto i = 0; i < cnt; i++) {
        size_t oldComputeSize = computeSize;
        computeSize += vec[i].iov_len;
    }

    return computeSize;
}
