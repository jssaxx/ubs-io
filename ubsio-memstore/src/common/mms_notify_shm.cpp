/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#include "mms_notify_shm.h"

#include <cstdio>
#include <new>

#include "mms_def.h"
#include "securec.h"

namespace ock {
namespace mms {

bool NotifyShmQueue::IsValidDepth(uint32_t queueDepth)
{
    return queueDepth >= NO_1024 && (queueDepth & (queueDepth - NO_1)) == 0;
}

uint32_t NotifyShmQueue::CalculateShardDepth(uint32_t totalDepth, uint16_t queueNum)
{
    if (queueNum == 0) {
        return 0;
    }
    uint32_t shardDepth = totalDepth / queueNum;
    if (shardDepth < NO_1024) {
        return 0;
    }
    uint32_t alignedDepth = NO_1;
    while (alignedDepth <= shardDepth / NO_2) {
        alignedDepth <<= NO_1;
    }
    return alignedDepth;
}

uint64_t NotifyShmQueue::GetShardMemorySize(uint32_t queueDepth)
{
    return sizeof(NotifyShmHeader) + static_cast<uint64_t>(queueDepth) * sizeof(NotifyShmSlot);
}

uint64_t NotifyShmQueue::GetMemorySize(uint32_t queueDepth, uint16_t queueNum)
{
    return GetShardMemorySize(queueDepth) * queueNum;
}

bool NotifyShmQueue::BuildSocketName(uint32_t serverPid, char *name, uint32_t nameSize)
{
    if (name == nullptr || nameSize < NOTIFY_SHM_SOCKET_NAME_SIZE || serverPid == 0) {
        return false;
    }
    int32_t size = snprintf(name, nameSize, "mms-notify-shm-%u", serverPid);
    return size > 0 && static_cast<uint32_t>(size) < nameSize;
}

bool NotifyShmQueue::Initialize(void *address, uint64_t size, uint32_t queueDepth, uint16_t queueNum)
{
    if (address == nullptr || !IsValidDepth(queueDepth) || queueNum == 0 ||
        size < GetMemorySize(queueDepth, queueNum)) {
        return false;
    }

    auto *base = static_cast<uint8_t *>(address);
    uint64_t shardSize = GetShardMemorySize(queueDepth);
    for (uint16_t queueIndex = 0; queueIndex < queueNum; ++queueIndex) {
        auto *header = new (base + shardSize * queueIndex) NotifyShmHeader();
        header->magic = NOTIFY_SHM_MAGIC;
        header->version = NOTIFY_SHM_VERSION;
        header->reserved = 0;
        header->queueDepth = queueDepth;
        header->queueMask = queueDepth - NO_1;
        header->state.store(static_cast<uint32_t>(NotifyShmState::INIT), std::memory_order_relaxed);
        header->enqueuePos.store(0, std::memory_order_relaxed);
        header->dequeuePos.store(0, std::memory_order_relaxed);
        header->enqueueCount.store(0, std::memory_order_relaxed);
        header->dequeueCount.store(0, std::memory_order_relaxed);
        header->fullCount.store(0, std::memory_order_relaxed);
        if (!header->enqueuePos.is_lock_free() || !header->dequeuePos.is_lock_free()) {
            return false;
        }

        auto *slots = reinterpret_cast<NotifyShmSlot *>(header + NO_1);
        for (uint32_t index = 0; index < queueDepth; ++index) {
            new (&slots[index]) NotifyShmSlot();
            slots[index].sequence.store(index, std::memory_order_relaxed);
        }
        header->state.store(static_cast<uint32_t>(NotifyShmState::ACTIVE), std::memory_order_release);
    }
    return true;
}

bool NotifyShmQueue::Attach(void *address, uint64_t size, uint32_t queueDepth, uint16_t queueIndex)
{
    uint64_t shardSize = GetShardMemorySize(queueDepth);
    uint64_t offset = shardSize * queueIndex;
    if (address == nullptr || !IsValidDepth(queueDepth) || size < offset + shardSize) {
        return false;
    }
    auto *header = reinterpret_cast<NotifyShmHeader *>(static_cast<uint8_t *>(address) + offset);
    if (header->magic != NOTIFY_SHM_MAGIC || header->version != NOTIFY_SHM_VERSION ||
        header->queueDepth != queueDepth) {
        return false;
    }
    mHeader = header;
    mSlots = reinterpret_cast<NotifyShmSlot *>(header + NO_1);
    return true;
}

bool NotifyShmQueue::IsValid() const
{
    return mHeader != nullptr && mSlots != nullptr &&
           mHeader->state.load(std::memory_order_acquire) == static_cast<uint32_t>(NotifyShmState::ACTIVE);
}

void NotifyShmQueue::Close()
{
    if (mHeader != nullptr) {
        mHeader->state.store(static_cast<uint32_t>(NotifyShmState::CLOSED), std::memory_order_release);
    }
}

void NotifyShmQueue::Reset()
{
    mHeader = nullptr;
    mSlots = nullptr;
}

NotifyShmResult NotifyShmQueue::TryEnqueueBatch(const NotifyShmPublishItem *items, uint32_t itemNum,
                                                uint64_t enqueueTimeNs, NotifyShmProducerContext &producerContext)
{
    if (items == nullptr || itemNum == 0 || mHeader == nullptr || itemNum > mHeader->queueDepth) {
        return NotifyShmResult::FULL;
    }

    uint64_t pos = 0;
    auto result = ReserveSlots(itemNum, pos, producerContext);
    if (result != NotifyShmResult::OK) {
        return result;
    }
    PublishSlots(pos, items, itemNum, enqueueTimeNs);
    mHeader->enqueueCount.fetch_add(itemNum, std::memory_order_relaxed);
    return NotifyShmResult::OK;
}

NotifyShmResult NotifyShmQueue::ReserveSlots(uint32_t itemNum, uint64_t &pos, NotifyShmProducerContext &producerContext)
{
    if (producerContext.queueId != mHeader) {
        producerContext.queueId = mHeader;
        producerContext.cachedDequeuePos = mHeader->dequeuePos.load(std::memory_order_acquire);
    }

    while (IsValid()) {
        pos = mHeader->enqueuePos.load(std::memory_order_relaxed);
        if (producerContext.cachedDequeuePos > pos ||
            pos - producerContext.cachedDequeuePos + itemNum > mHeader->queueDepth) {
            producerContext.cachedDequeuePos = mHeader->dequeuePos.load(std::memory_order_acquire);
            if (pos - producerContext.cachedDequeuePos + itemNum > mHeader->queueDepth) {
                mHeader->fullCount.fetch_add(NO_1, std::memory_order_relaxed);
                return NotifyShmResult::FULL;
            }
        }
        if (mHeader->enqueuePos.compare_exchange_weak(pos, pos + itemNum, std::memory_order_relaxed,
                                                      std::memory_order_relaxed)) {
            return NotifyShmResult::OK;
        }
        CpuRelax();
    }
    return NotifyShmResult::CLOSED;
}

void NotifyShmQueue::PublishSlots(uint64_t pos, const NotifyShmPublishItem *items, uint32_t itemNum,
                                  uint64_t enqueueTimeNs)
{
    for (uint32_t index = 0; index < itemNum; ++index) {
        uint64_t slotPos = pos + index;
        NotifyShmSlot &slot = mSlots[slotPos & mHeader->queueMask];
        slot.event.enqueueTimeNs = enqueueTimeNs;
        slot.event.keyLen = items[index].keyLen;
        slot.event.opType = items[index].opType;
        auto ret = memcpy_s(slot.event.key, MAX_KEY_SIZE, items[index].key, items[index].keyLen);
        if (UNLIKELY(ret != EOK)) {
            slot.event.keyLen = 0;
        }
        slot.sequence.store(slotPos + NO_1, std::memory_order_release);
    }
}

NotifyShmResult NotifyShmQueue::DequeueSingleConsumer(NotifyShmEvent &event)
{
    if (!IsValid()) {
        return NotifyShmResult::CLOSED;
    }

    uint64_t pos = mHeader->dequeuePos.load(std::memory_order_relaxed);
    NotifyShmSlot &slot = mSlots[pos & mHeader->queueMask];
    uint64_t sequence = slot.sequence.load(std::memory_order_acquire);
    int64_t diff = static_cast<int64_t>(sequence) - static_cast<int64_t>(pos + NO_1);
    if (diff < 0) {
        return NotifyShmResult::EMPTY;
    }
    if (diff > 0) {
        CpuRelax();
        return NotifyShmResult::EMPTY;
    }

    if (UNLIKELY(!CopyEvent(slot.event, event))) {
        event.keyLen = 0;
    }
    ReleaseSlot(slot, pos);
    mHeader->dequeuePos.store(pos + NO_1, std::memory_order_release);
    return NotifyShmResult::OK;
}

bool NotifyShmQueue::CopyEvent(const NotifyShmEvent &source, NotifyShmEvent &target) const
{
    target.enqueueTimeNs = source.enqueueTimeNs;
    target.keyLen = source.keyLen;
    target.opType = source.opType;
    if (UNLIKELY(source.keyLen == 0 || source.keyLen >= MAX_KEY_SIZE)) {
        target.keyLen = 0;
        return false;
    }
    auto ret = memcpy_s(target.key, MAX_KEY_SIZE, source.key, source.keyLen);
    if (UNLIKELY(ret != EOK)) {
        target.keyLen = 0;
        return false;
    }
    return true;
}

void NotifyShmQueue::ReleaseSlot(NotifyShmSlot &slot, uint64_t pos)
{
    slot.sequence.store(pos + mHeader->queueDepth, std::memory_order_release);
    mHeader->dequeueCount.fetch_add(NO_1, std::memory_order_relaxed);
}

void NotifyShmQueue::CpuRelax()
{
#ifdef __aarch64__
    __asm__ volatile("yield");
#elif defined(__x86_64__)
    __asm__ volatile("pause");
#endif
}

} // namespace mms
} // namespace ock
