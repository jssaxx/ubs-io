/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2022-2022. All rights reserved.
 */
#ifndef BOOSTIO_UNDERFS_H
#define BOOSTIO_UNDERFS_H

#include <string>
#include <ctime>

#include "bio_err.h"
#include "bio_ref.h"

#ifdef _ceph_Integrate
#include "librados.h"
#else
typedef void *rados_t;
typedef void *rados_ioctx_t;
#endif

namespace ock {
namespace bio {
class UnderFs;
using UnderFsPtr = Ref<UnderFs>;
class UnderFs {
public:
    struct ObjStat {
        uint64_t size;
        time_t mTime;
    };

    static UnderFsPtr &Instance()
    {
        static auto instance = MakeRef<UnderFs>();
        return instance;
    }

    BResult Init();

    void Stop();

    BResult Put(const char *key, const char *value, const size_t len);

    BResult Get(const char *key, char *value, const size_t len, const uint64_t off);

    BResult Delete(const char *key);

    BResult Stat(const char *key, ObjStat &objStat);
 
    DEFINE_REF_COUNT_FUNCTIONS;
private:
    rados_t mConn;
    rados_ioctx_t mIoCtx;

    std::string mCluster { "ceph" };
    std::string mUser { "client.admin" };
    std::string mPool { "jfspool" };

    bool mInited { false };

    DEFINE_REF_COUNT_VARIABLE;
};
}
}
#endif // BOOSTIO_UNDERFS_H
