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

#ifndef MMS_H
#define MMS_H

#include <functional>
#include <string>
#include <memory>
#include <vector>
#include "mms_c.h"

namespace ock {
namespace mms {
class Mms {
public:
    /**
     * @brief: Initialize the MMS service.
     *
     * @param[in]: options: MMS options.
     * @param[in]: service: Service status callback.
     * @return: RET_MMS_OK on success; otherwise, a non-zero error code.
     */
    static CResult Initialize(const MmsOptions &options, ServiceCallback service);

    /**
     * @brief: Register the data change notification callback.
     *
     * @param[in]: callback: Data change notification callback.
     * @param[in]: lpUserData: User data passed to the notification callback.
     * @return: RET_MMS_OK on success; otherwise, a non-zero error code.
     */
    static CResult RegisterCallback(NotifyCallback callback, void *lpUserData);

    /**
     * @brief: Exit the MMS service.
     *
     * @return: void.
     */
    static void Exit();

    /**
     * @brief: Put key/value items.
     *
     * For each item, PutItems::valueAddr is used to return the memory address where the data is written, and
     * PutItems::result is used to return the per-item execution result.
     *
     * @param[in/out]: itemList: Key/value descriptor list.
     * @param[in]: itemNum: Number of items in itemList.
     * @return: RET_MMS_OK if all items succeed; otherwise, the last failed item's error code.
     */
    static CResult Put(PutItems *itemList, uint32_t itemNum);

    /**
     * @brief: Get key/value items.
     *
     * If GetItems::value points to a null pointer, the returned data address is managed internally and must not be
     * freed by the caller. The caller must ensure that the key is not modified while reading data through that address.
     *
     * For each item, GetItems::realLength is used to return the real value length, and GetItems::result is used to
     * return the per-item execution result.
     *
     * @param[in/out]: itemList: Key/value descriptor list.
     * @param[in]: itemNum: Number of items in itemList.
     * @return: RET_MMS_OK if all items succeed; otherwise, the last failed item's error code. If an item's realLength
     *          is 0, the key was not found.
     */
    static CResult Get(GetItems *itemList, uint32_t itemNum);

    /**
     * @brief: Update key/value items.
     *
     * For each item, UpdateItems::result is used to return the per-item execution result.
     *
     * @param[in/out]: itemList: Key/value descriptor list.
     * @param[in]: itemNum: Number of items in itemList.
     * @return: RET_MMS_OK if all items succeed; otherwise, the last failed item's error code.
     */
    static CResult Update(UpdateItems *itemList, uint32_t itemNum);

    /**
     * @brief: Delete key/value items.
     *
     * For each item, DeleteItems::result is used to return the per-item execution result.
     *
     * @param[in/out]: itemList: Key descriptor list.
     * @param[in]: itemNum: Number of items in itemList.
     * @return: RET_MMS_OK if all items succeed; otherwise, the last failed item's error code.
     */
    static CResult Delete(DeleteItems *itemList, uint32_t itemNum);

    /**
     * @brief: Replace key/value items.
     *
     * For each item, ReplaceItems::result is used to return the per-item execution result.
     *
     * @param[in/out]: itemList: Key/value descriptor list.
     * @param[in]: itemNum: Number of items in itemList.
     * @return: RET_MMS_OK if all items succeed; otherwise, the last failed item's error code.
     */
    static CResult Replace(ReplaceItems *itemList, uint32_t itemNum);

    /**
     * @brief: Start a catch-up task for recovery.
     *
     * @return: RET_MMS_OK on success; otherwise, a non-zero error code.
     */
    static CResult StartCatchUpTask();
};
}
}
#endif
