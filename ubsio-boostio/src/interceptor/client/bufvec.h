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

#ifndef BOOSTIO_BUFVEC_H
#define BOOSTIO_BUFVEC_H

namespace ock {
namespace bio {
struct BufVec {
    const iovec *iov;
    const int count;
    const size_t size;

    int index = 0;
    size_t innerOffset = 0;
    size_t totalOffset = 0;

    explicit BufVec(const iovec *vec, int cnt) noexcept;

    virtual ~BufVec() noexcept = default;

    void Reset() noexcept;

    ssize_t Read(uint8_t *buf, size_t length) noexcept;

    ssize_t Write(const uint8_t *buf, size_t length) noexcept;

    static size_t ComputeSize(const iovec *vec, int cnt) noexcept;

    template <class OS>
    friend OS &operator<<(OS &os, const BufVec &bv) noexcept
    {
        os << bv.count << ":(";
        for (auto i = 0; i < bv.count; i++) {
            os << bv.iov[i].iov_len << ",";
        }
        os << ") total=" << bv.count;
        return os;
    }
};
} // namespace bio
} // namespace ock
#endif // BOOSTIO_BUFVEC_H
