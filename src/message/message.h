/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef MESSAGE_H
#define MESSAGE_H

#include <cstdint>
#include <string>
#include <utility>
#include "cache_def.h"

namespace ock {
namespace bio {
constexpr uint16_t MESSAGE_MAGIC = 0xABCD;
constexpr uint32_t KEY_MAX_SIZE = 256;

#define DECLARE_CHAR_ARRAY_SET_FUNC(func, CHAR_ARRAY)    \
    bool func(const std::string &other)                  \
    {                                                    \
        if (other.length() > (sizeof(CHAR_ARRAY) - 1)) { \
            return false;                                \
        }                                                \
                                                         \
        for (uint32_t i = 0; i < other.length(); i++) {  \
            CHAR_ARRAY[i] = other.at(i);                 \
        }                                                \
                                                         \
        (CHAR_ARRAY)[other.length()] = '\0';             \
        return true;                                     \
    }

#define DECLARE_CHAR_ARRAY_GET_FUNC(func, CHAR_ARRAY)                   \
    std::string func() const                                            \
    {                                                                   \
        return { CHAR_ARRAY, strnlen(CHAR_ARRAY, sizeof(CHAR_ARRAY)) }; \
    }

struct RequestComm {
    uint16_t magic;
    uint16_t ptId;
    uint64_t ptv;
    uint32_t srcNid;
    pid_t pid;

    RequestComm() = default;
    RequestComm(uint16_t pt, uint64_t v, uint32_t nid)
        : magic(MESSAGE_MAGIC), ptId(pt), ptv(v), srcNid(nid), pid(getpid()) {}
};

struct QueryViewRequest {
    RequestComm comm;
};

struct CreateFlowRequest {
    RequestComm comm;
    uint16_t opType;
    uint16_t ptId;
    uint64_t flowId;
};

struct PutRequest {
    RequestComm comm;
    CacheAttr attr;
    char key[KEY_MAX_SIZE];
    uint64_t length;
    uint64_t flowId;
    uint64_t offset;
    uint64_t index;
    uint32_t mrKey;
    uint32_t sliceLen;
    char sliceBuf[0];

    DECLARE_CHAR_ARRAY_SET_FUNC(Key, key);
    DECLARE_CHAR_ARRAY_GET_FUNC(Key, key);

    void Fill(RequestComm reqComm, CacheAttr &cacheAttr, const char *cKey, uint64_t len, uint64_t fId,
        uint64_t off, uint64_t idx, uint32_t mKey, uint32_t sLen)
    {
        comm = reqComm;
        attr = cacheAttr;
        Key(cKey);
        length = len;
        flowId = fId;
        offset = off;
        index = idx;
        mrKey = mKey;
        sliceLen = sLen;
    }
};

struct GetRequest {
    RequestComm comm;
    char key[KEY_MAX_SIZE]{};
    uint16_t ptId;
    uint64_t offset;
    uint64_t length;
    bool isMr;
    NetMrInfo mr;

    DECLARE_CHAR_ARRAY_SET_FUNC(Key, key);
    DECLARE_CHAR_ARRAY_GET_FUNC(Key, key);

    GetRequest(RequestComm reqComm, const char *cKey, uint16_t pt, uint64_t off, uint64_t len, NetMrInfo info)
        : comm(reqComm), ptId(pt), offset(off), length(len), isMr(false), mr(info)
    {
        Key(cKey);
    }

    void SetMrInfo(char *value, uint32_t len)
    {
        isMr = false;
        mr.address = reinterpret_cast<uintptr_t>(value);
        mr.size = len;
    }

    void SetMrInfo(NetMrInfo mrInfo)
    {
        isMr = true;
        mr = mrInfo;
    }
};

struct DeleteRequest {
    RequestComm comm;
    char key[KEY_MAX_SIZE]{};
    uint16_t ptId;

    DECLARE_CHAR_ARRAY_SET_FUNC(Key, key);
    DECLARE_CHAR_ARRAY_GET_FUNC(Key, key);

    DeleteRequest(RequestComm reqComm, const char *cKey, uint16_t pt) : comm(reqComm), ptId(pt)
    {
        Key(cKey);
    }
};

struct StatRequest {
    RequestComm comm;
    char key[KEY_MAX_SIZE]{};
    uint16_t ptId;

    DECLARE_CHAR_ARRAY_SET_FUNC(Key, key);
    DECLARE_CHAR_ARRAY_GET_FUNC(Key, key);

    StatRequest(RequestComm reqComm, const char *cKey, uint16_t pt) : comm(reqComm), ptId(pt)
    {
        Key(cKey);
    }
};

struct LoadRequest {
    RequestComm comm;
    char key[KEY_MAX_SIZE]{};
    uint16_t ptId;
    uint64_t offset;
    uint64_t length;

    DECLARE_CHAR_ARRAY_SET_FUNC(Key, key);
    DECLARE_CHAR_ARRAY_GET_FUNC(Key, key);

    LoadRequest(RequestComm reqComm, const char *cKey, uint16_t partId, uint64_t off, uint64_t len)
        : comm(reqComm), ptId(partId), offset(off), length(len)
    {
        Key(cKey);
    }
};

struct SyncDataRequest {
    RequestComm comm;

    SyncDataRequest(RequestComm reqComm)
        : comm(reqComm)
    {}
};

struct ClientCallbackCtx {
    int32_t result;
    std::atomic<uint32_t> quota{};
    sem_t sem{};
    void *resp;
    uint32_t respLen;

    ClientCallbackCtx(int32_t ret, uint32_t num)
    {
        result = ret;
        quota.store(num);
        sem_init(&sem, 0, 0);
        resp = nullptr;
        respLen = 0;
    }
};
}
}
#endif // MESSAGE_H
