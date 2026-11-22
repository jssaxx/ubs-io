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

#ifndef BOOSTIO_BIO_META_EVENT_H
#define BOOSTIO_BIO_META_EVENT_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ock {
namespace bio {
enum UbsIoMetaEventType : uint8_t {
    UBSIO_META_RECOVER = 0,
    UBSIO_META_DELETE = 1,
};

struct UbsIoMetaEvent {
    UbsIoMetaEventType type;
    std::string key;
};

using UbsIoMetaEventCallback = std::function<void(const std::vector<UbsIoMetaEvent> &events)>;
}
}

#endif // BOOSTIO_BIO_META_EVENT_H
