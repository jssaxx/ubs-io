/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BOOSTIO_CEPHSYSTEM_H
#define BOOSTIO_CEPHSYSTEM_H

#include "file_system.h"
#include "rados/librados.h"

namespace ock {
namespace bio {
class CephSystem : public FileSystem {
public:

    BResult Init() override;

    void Stop() override;

    BResult Put(const char *key, const char *value, const size_t len) override;

    BResult Get(const char *key, char *value, const size_t len, const uint64_t off) override;

    BResult Delete(const char *key) override;

    BResult Stat(const char *key, ObjStat &objStat) override;

    BResult List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) override;

private:
    void LoadCephConfig();

private:
    rados_t mConn;
    rados_ioctx_t mIoCtx;

    std::string mCfgPath;
    std::string mCluster;
    std::string mUser;
    std::string mPool;
};
}
}


#endif // BOOSTIO_CEPHSYSTEM_H
