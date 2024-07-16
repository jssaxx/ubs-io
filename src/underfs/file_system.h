/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BOOSTIO_FILESYSTEM_H
#define BOOSTIO_FILESYSTEM_H

#include <string>
#include <unordered_map>

#include "bio_err.h"
#include "bio_ref.h"

namespace ock {
namespace bio {
class FileSystem {
public:
    struct ObjStat {
        uint64_t size;
        time_t time;
    };

    virtual BResult Init() = 0;

    virtual void Stop() = 0;

    virtual BResult Put(const char *key, const char *value, const size_t len) = 0;

    virtual BResult Get(const char *key, char *value, const size_t len, const uint64_t off) = 0;

    virtual BResult Delete(const char *key) = 0;

    virtual BResult Stat(const char *key, ObjStat &objStat) = 0;

    virtual BResult List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) = 0;

protected:
    bool mInited{ false };
};
}
}
#endif //BOOSTIO_FILESYSTEM_H
