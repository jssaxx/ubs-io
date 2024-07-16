/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#ifndef BOOSTIO_LOCALSYSTEM_H
#define BOOSTIO_LOCALSYSTEM_H

#include "file_system.h"

namespace ock {
namespace bio {
class LocalSystem : public FileSystem {
public:
    BResult Init() override;

    void Stop() override;

    BResult Put(const char *key, const char *value, const size_t len) override;

    BResult Get(const char *key, char *value, const size_t len, const uint64_t off) override;

    BResult Delete(const char *key) override;

    BResult Stat(const char *key, ObjStat &objStat) override;

    BResult List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat) override;

private:
    std::string mEmulationCephPath;
};
}
}


#endif // BOOSTIO_LOCALSYSTEM_H
