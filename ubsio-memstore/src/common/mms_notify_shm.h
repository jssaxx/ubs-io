/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 */

#ifndef MMS_NOTIFY_SHM_H
#define MMS_NOTIFY_SHM_H

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "mms_c.h"
#include "mms_types.h"

namespace ock {
namespace mms {

constexpr uint32_t NOTIFY_SHM_MAGIC = 0x4D4E5348;
constexpr uint16_t NOTIFY_SHM_VERSION = 2;
constexpr uint32_t NOTIFY_SHM_CACHE_LINE_SIZE = 64;
constexpr uint32_t NOTIFY_SHM_SOCKET_NAME_SIZE = 64;
constexpr uint16_t NOTIFY_SHM_MAX_FDS = NOTIFY_SHM_MAX_WORKERS + 1;

enum class NotifyShmState : uint32_t
{
    INIT = 0,
    ACTIVE,
    CLOSED,
};

enum class NotifyShmResult : uint8_t
{
    OK = 0,
    EMPTY,
    FULL,
    CLOSED,
};

struct NotifyShmEvent {
    uint64_t enqueueTimeNs;
    uint16_t keyLen;
    uint16_t opType;
    char key[MAX_KEY_SIZE];
};

struct NotifyShmPublishItem {
    const char *key;
    uint16_t keyLen;
    uint16_t opType;
};

struct NotifyShmProducerContext {
    const void *queueId = nullptr;
    uint64_t cachedDequeuePos = 0;
};

struct NotifyShmHandshake {
    int32_t result;
    uint32_t queueDepth;
    uint64_t memorySize;
    uint16_t workerNum;
    uint16_t queueNum;
    uint16_t cpuNum;
    uint8_t busyPolling;
    uint8_t reserved;
    uint16_t cpuIds[NOTIFY_SHM_MAX_WORKERS];
};

struct alignas(NOTIFY_SHM_CACHE_LINE_SIZE) NotifyShmSlot {
    std::atomic<uint64_t> sequence;
    NotifyShmEvent event;
};

struct alignas(NOTIFY_SHM_CACHE_LINE_SIZE) NotifyShmHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t queueDepth;
    uint32_t queueMask;
    std::atomic<uint32_t> state;
    alignas(NOTIFY_SHM_CACHE_LINE_SIZE) std::atomic<uint64_t> enqueuePos;
    std::atomic<uint64_t> enqueueCount;
    std::atomic<uint64_t> fullCount;
    alignas(NOTIFY_SHM_CACHE_LINE_SIZE) std::atomic<uint64_t> dequeuePos;
    std::atomic<uint64_t> dequeueCount;
};

class NotifyShmQueue {
public:
    static bool IsValidDepth(uint32_t queueDepth);
    static uint32_t CalculateShardDepth(uint32_t totalDepth, uint16_t queueNum);
    static uint64_t GetMemorySize(uint32_t queueDepth, uint16_t queueNum);
    static bool Initialize(void *address, uint64_t size, uint32_t queueDepth, uint16_t queueNum);
    static bool BuildSocketName(uint32_t serverPid, char *name, uint32_t nameSize);

    NotifyShmQueue() = default;

    bool Attach(void *address, uint64_t size, uint32_t queueDepth, uint16_t queueIndex);
    bool IsValid() const;
    void Close();
    void Reset();
    NotifyShmResult TryEnqueueBatch(const NotifyShmPublishItem *items, uint32_t itemNum, uint64_t enqueueTimeNs,
                                    NotifyShmProducerContext &producerContext);
    NotifyShmResult DequeueSingleConsumer(NotifyShmEvent &event);
    static void CpuRelax();

private:
    static uint64_t GetShardMemorySize(uint32_t queueDepth);
    NotifyShmResult ReserveSlots(uint32_t itemNum, uint64_t &pos, NotifyShmProducerContext &producerContext);
    void PublishSlots(uint64_t pos, const NotifyShmPublishItem *items, uint32_t itemNum, uint64_t enqueueTimeNs);
    bool CopyEvent(const NotifyShmEvent &source, NotifyShmEvent &target) const;
    void ReleaseSlot(NotifyShmSlot &slot, uint64_t pos);

private:
    NotifyShmHeader *mHeader = nullptr;
    NotifyShmSlot *mSlots = nullptr;
};

} // namespace mms
} // namespace ock

#endif
