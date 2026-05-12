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
#ifndef MMSCORE_MMS_IP_UTIL_H
#define MMSCORE_MMS_IP_UTIL_H

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include "mms_str_util.h"

namespace ock {
namespace mms {
constexpr uint32_t CM_IP_SIZE = 32;

class IpUtil {
public:
    /*
     * @brief Translate the ip mask to address
     *
     * @param mask               [in] ip marks format, e.g. 192.168.1.0/24
     * @param ipMask             [out] address
     *
     * return true if convert successfully
     */
    static bool IpMaskToAddress(const std::string &maskString, in_addr_t &ipByMask, in_addr_t &mask);

    /*
     * @brief Filter ips by mask
     *
     * @param ipMask           [in] ip marks format, e.g. 192.168.1.0/24
     * @param outIps           [out] filtered ip vector
     *
     * @return true if filter successfully
     */
    static bool FilterIpByMask(const std::string &ipMask, std::vector<std::string> &outIps);
};

inline bool IpUtil::IpMaskToAddress(const std::string &maskString, in_addr_t &ipByMask, in_addr_t &mask)
{
    std::vector<std::string> ipMaskVec;
    StrUtil::Split(maskString, "/", ipMaskVec);
    if (ipMaskVec.size() != 2L) {
        return false;
    }

    long maskWidth = 0;
    if (!StrUtil::StrToLong(ipMaskVec[1], maskWidth)) {
        return false;
    }

    if (maskWidth < 0 || maskWidth > CM_IP_SIZE) {
        return false;
    }

    auto tmp = inet_addr(ipMaskVec[0].c_str());
    if (tmp == INADDR_NONE) {
        return false;
    }

    mask = 0xFFFFFFFF >> (CM_IP_SIZE - maskWidth);
    ipByMask = tmp & mask;
    return true;
}

inline bool IpUtil::FilterIpByMask(const std::string &ipMask, std::vector<std::string> &outIps)
{
    in_addr_t mask = 0;
    in_addr_t inputIpByMask = 0;
    if (!IpMaskToAddress(ipMask, inputIpByMask, mask)) {
        return false;
    }

    struct ifaddrs *addresses = nullptr;
    if (getifaddrs(&addresses) != 0) {
        return false;
    }

    struct ifaddrs *iter = addresses;
    while (iter != nullptr) {
        auto address = (reinterpret_cast<struct sockaddr_in *>(iter->ifa_addr))->sin_addr;
        if (iter->ifa_addr->sa_family != AF_INET || (address.s_addr & mask) != inputIpByMask) {
            iter = iter->ifa_next;
            continue;
        }

        char ipStr[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &((reinterpret_cast<struct sockaddr_in *>(iter->ifa_addr))->sin_addr), ipStr,
            INET_ADDRSTRLEN);
        outIps.emplace_back(ipStr);

        iter = iter->ifa_next;
    }
    freeifaddrs(addresses);

    return true;
}
}
}

#endif // MMSCORE_MMS_IP_UTIL_H

