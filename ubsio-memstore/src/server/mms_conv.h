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

#ifndef MMS_CONV_H
#define MMS_CONV_H

#include <functional>
#include <string>
#include <memory>
#include <vector>
#include "mms_c.h"

namespace ock {
namespace mms {
class MmsConv {
public:
    /**
     * @brief: Initialize mms service
     *
     * @param[in]: option: log options
     * @return: return RET_MMS_OK mean success, others, return non-zero value
     */
    static CResult Initialize(const MmsOptions &options, ServiceCallback service);

    /**
     * @brief: Exit mms service
     *
     * @return: void
     */
    static void Exit();

    /**
     * @brief: Put value
     *
     * @param[in]: userId: user id
     * @param[in]: itemList: key/value desc list
     * @param[in]: num: batch num
     * @return: return RET_MMS_OK mean success, others, return non-zero value
     */
    static CResult Put(uint64_t userId, PutItems *itemList, uint32_t itemNum);

    /**
     * @brief: Get value
     *
     * @param[in]: userId: user id
     * @param[in/out]: itemList: key/value desc list
     * @param[in]: num: batch num
     * @return: return RET_MMS_OK mean success, others, return non-zero value
     */
    static CResult Get(uint64_t userId, GetItems *itemList, uint32_t itemNum);

    /**
     * @brief: Get value
     *
     * @param[in]: userId: user id
     * @param[in]: itemList: key/value desc list
     * @param[in]: num: batch num
     * @return: return RET_MMS_OK mean success, others, return non-zero value
     */
    static CResult Update(uint64_t userId, UpdateItems *itemList, uint32_t itemNum);

    /**
     * @brief: Delete object
     *
     * @param[in]: userId: user id
     * @param[in]: itemList: key/value desc list
     * @param[in]: num: batch num
     * @return: return RET_MMS_OK mean ok, others, return non-zero value
     */
    static CResult Delete(uint64_t userId, DeleteItems *itemList, uint32_t itemNum);

    /**
    * @brief: Replace object
    *
    * @param[in]: userId: user id
    * @param[in]: itemList: key/value desc list
    * @param[in]: num: batch num
    * @return: return RET_MMS_OK mean ok, others, return non-zero value
    */
    static CResult Replace(uint64_t userId, ReplaceItems *itemList, uint32_t itemNum);

    /**
     * @brief: start catch up task to recover
     *
     * @return: return RET_MMS_OK mean ok, others, return non-zero value
     */
    static CResult StartCatchUpTask();
};
}
}
#endif
