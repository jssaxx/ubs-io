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

#ifndef MMSCORE_MMS_STR_UTIL_H
#define MMSCORE_MMS_STR_UTIL_H
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <regex>

namespace ock {
namespace mms {
class StrUtil {
public:
    static bool StartWith(const std::string &src, const std::string &start);
    static bool EndWith(const std::string &src, const std::string &end);

    static bool StrToLong(const std::string &src, long &value);
    static bool StrToFloat(const std::string &src, float &value);
    static bool StrToUint16(const std::string &src, uint16_t &value);
    static bool StrToUint32(const std::string &src, uint32_t &value);
    static bool StrToUint32Pair(const std::string &src, std::pair<uint32_t, uint32_t> &value);

    static void Split(const std::string &src, const std::string &sep, std::vector<std::string> &out);
    static void Split(const std::string &src, const std::string &sep, std::set<std::string> &out);

    static void StrTrim(std::string &src);
    static void Replace(std::string &src, const std::string &regex, const std::string &replaced);
};

inline bool StrUtil::StartWith(const std::string &src, const std::string &start)
{
    return src.compare(0, start.size(), start) == 0;
}

inline bool StrUtil::EndWith(const std::string &src, const std::string &end)
{
    return src.compare(src.size() - end.size(), end.size(), end) == 0;
}

inline bool StrUtil::StrToLong(const std::string &src, long &value)
{
    char *remain = nullptr;
    errno = 0;
    value = std::strtol(src.c_str(), &remain, 10L); // 10 is decimal digits
    if ((value == 0 && src != "0") || remain == nullptr || strlen(remain) > 0 || errno == ERANGE) {
        return false;
    }
    return true;
}

inline bool StrUtil::StrToFloat(const std::string &src, float &value)
{
    constexpr float EPSINON = 0.000001;

    char *remain = nullptr;
    errno = 0;
    value = std::strtof(src.c_str(), &remain);
    if (remain == nullptr || strlen(remain) > 0 ||
        ((value - HUGE_VALF) >= -EPSINON && (value - HUGE_VALF) <= EPSINON && errno == ERANGE)) {
        return false;
    } else if ((value >= -EPSINON && value <= EPSINON) && (src != "0.0")) {
        return false;
    }
    return true;
}

inline bool StrUtil::StrToUint16(const std::string &src, uint16_t &value)
{
    if (src.empty()) {
        return false;
    }

    char *remain = nullptr;
    errno = 0;

    unsigned long temp = std::strtoul(src.c_str(), &remain, 10);
    if (errno == ERANGE || temp > UINT16_MAX || remain == src.c_str() || (remain != nullptr && *remain != '\0')) {
        return false;
    }

    value = static_cast<uint16_t>(temp);
    return true;
}

inline bool StrUtil::StrToUint32Pair(const std::string &src, std::pair<uint32_t, uint32_t> &value)
{
    if (src.empty()) {
        return false;
    }

    size_t dashPos = src.find('-');
    if (dashPos == std::string::npos || dashPos == 0 || dashPos == src.length() - 1 ||
        src.find('-', dashPos + 1) != std::string::npos) {
        return false;
    }

    std::string firstPart = src.substr(0, dashPos);
    std::string secondPart = src.substr(dashPos + 1);
    if (firstPart.empty() || secondPart.empty()) {
        return false;
    }

    uint32_t first;
    uint32_t second;
    if (!StrToUint32(firstPart, first) || !StrToUint32(secondPart, second)) {
        return false;
    }

    value = std::make_pair(first, second);
    return true;
}

inline bool StrUtil::StrToUint32(const std::string &src, uint32_t &value)
{
    if (src.empty()) {
        return false;
    }

    char *remain = nullptr;
    errno = 0;

    unsigned long temp = std::strtoul(src.c_str(), &remain, 10);
    if (errno == ERANGE || temp > UINT32_MAX || remain == src.c_str() || (remain != nullptr && *remain != '\0')) {
        return false;
    }

    value = static_cast<uint32_t>(temp);
    return true;
}

inline void StrUtil::Split(const std::string &src, const std::string &sep, std::vector<std::string> &out)
{
    std::string::size_type pos1 = 0;
    std::string::size_type pos2 = src.find(sep);

    std::string tmpStr;
    while (std::string::npos != pos2) {
        tmpStr = src.substr(pos1, pos2 - pos1);
        out.emplace_back(tmpStr);
        pos1 = pos2 + sep.size();
        pos2 = src.find(sep, pos1);
    }

    if (pos1 != src.length()) {
        tmpStr = src.substr(pos1);
        out.emplace_back(tmpStr);
    }
}

inline void StrUtil::Split(const std::string &src, const std::string &sep, std::set<std::string> &out)
{
    std::string::size_type pos1 = 0;
    std::string::size_type pos2 = src.find(sep);

    std::string tmpStr;
    while (std::string::npos != pos2) {
        tmpStr = src.substr(pos1, pos2 - pos1);
        out.insert(tmpStr);
        pos1 = pos2 + sep.size();
        pos2 = src.find(sep, pos1);
    }

    if (pos1 != src.length()) {
        tmpStr = src.substr(pos1);
        out.insert(tmpStr);
    }
}

inline void StrUtil::StrTrim(std::string &src)
{
    if (src.empty()) {
        return;
    }

    src.erase(0, src.find_first_not_of(' '));
    src.erase(src.find_last_not_of(' ') + 1);
}

inline void StrUtil::Replace(std::string &src, const std::string &regex, const std::string &replaced)
{
    src = std::regex_replace(src, std::regex(regex), replaced);
    if (src.rfind(replaced) == src.length() - 1) {
        src = src.substr(0, src.length() - 1);
    }
}
}
}
#endif // MMSCORE_MMS_STR_UTIL_H

