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

#ifndef CEPTOR_TIMESTAMP_H
#define CEPTOR_TIMESTAMP_H

#include <cstdint>
#include <string>
#include <algorithm>

namespace ock {
namespace interceptor {
class TimeStamp {

public:
    TimeStamp();

    explicit TimeStamp(int64_t microSeconds);

    void Swap(TimeStamp &other)
    {
        std::swap(microSecondsSinceEpoch, other.microSecondsSinceEpoch);
    }

    std::string ToString() const;

    std::string ToFormattedString() const;

    bool Vaild() const
    {
        return microSecondsSinceEpoch > 0;
    }

    int64_t GetMicroSeconds() const
    {
        return microSecondsSinceEpoch;
    }

    double GetSeconds() const
    {
        return static_cast<double>(microSecondsSinceEpoch / microSecondPerSecond);
    }

    static TimeStamp Now();

    TimeStamp operator-(TimeStamp &other)
    {
        int64_t diff = microSecondsSinceEpoch - other.microSecondsSinceEpoch;
        return TimeStamp(diff);
    }

    TimeStamp operator+(TimeStamp &other)
    {
        int64_t sum = microSecondsSinceEpoch + other.microSecondsSinceEpoch;
        return TimeStamp(sum);
    }

    TimeStamp operator+(const double &secondes)
    {
        int64_t delta = static_cast<int64_t>(secondes * TimeStamp::microSecondPerSecond);
        return TimeStamp(microSecondsSinceEpoch + delta);
    }

    inline bool operator<(const TimeStamp &rhs) const
    {
        return microSecondsSinceEpoch < rhs.microSecondsSinceEpoch;
    }

private:
    int64_t microSecondsSinceEpoch;

    static const int microSecondPerSecond = 1e6;
};
}
}

#endif // CEPTOR_TIMESTAMP_H