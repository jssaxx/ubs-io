/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef BIO_KV_H
#define BIO_KV_H

#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

#include "bio_def.h"
#include "bio_str_util.h"
#include "bio_file_util.h"

namespace ock {
namespace bio {
struct KvPair {
    std::string name;
    std::string value;
};

class KVReader {
public:
    KVReader() = default;
    ~KVReader();

    KVReader(const KVReader &) = delete;
    KVReader &operator = (const KVReader &) = delete;
    KVReader(const KVReader &&) = delete;
    KVReader &operator = (const KVReader &&) = delete;

    bool FromFile(const std::string &filePath);

    bool GetItem(const std::string &key, std::string &outValue);
    void GetItems(std::unordered_map<std::string, std::string> &items);
    bool SetItem(const std::string &key, const std::string &value);

    uint32_t Size();
    void GetI(uint32_t index, std::string &outKey, std::string &outValue);

    void Dump();

private:
    std::map<std::string, uint32_t> mItemsIndex;
    std::vector<KvPair *> mItems;
    std::mutex mLock;
};

inline KVReader::~KVReader()
{
    {
        std::lock_guard<std::mutex> guard(mLock);
        for (auto &val : mItems) {
            KvPair *p = val;
            delete (p);
        }

        mItems.clear();
        mItemsIndex.clear();
    }
}

inline bool KVReader::FromFile(const std::string &filePath)
{
    char* path = new(std::nothrow) char[PATH_MAX + 1];
    if (path == nullptr) {
        printf("Memory allocation failed.\n");
        return false;
    }
    std::fill(path, path + PATH_MAX + 1, 0x00);
    if (strlen(filePath.c_str()) > PATH_MAX || realpath(filePath.c_str(), path) == nullptr) {
        delete[] path;
        return false;
    }

    /* open file to read */
    std::ifstream inConfFile(path);
    if (!inConfFile) {
        delete[] path;
        return false;
    }

    bool result = true;
    std::string strLine;
    while (getline(inConfFile, strLine)) {
        std::string strKey;
        std::string strValue;
        if (!FileUtil::ParseConfigLine(strLine, strKey, strValue)) {
            continue;
        }

        /* set key value */
        if (!SetItem(strKey, strValue)) {
            result = false;
            break;
        }
    }

    inConfFile.close();
    inConfFile.clear();
    delete[] path;
    return result;
}

inline bool KVReader::GetItem(const std::string &key, std::string &outValue)
{
    std::lock_guard<std::mutex> guard(mLock);
    auto iter = mItemsIndex.find(key);
    if (iter != mItemsIndex.end()) {
        outValue = mItems.at(iter->second)->value;
        return true;
    }
    return false;
}

inline void KVReader::GetItems(std::unordered_map<std::string, std::string> &items)
{
    std::lock_guard<std::mutex> guard(mLock);
    for (auto &kv : mItems) {
        items.emplace(kv->name, kv->value);
    }
}

inline bool KVReader::SetItem(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> guard(mLock);
    auto iter = mItemsIndex.find(key);
    if (iter != mItemsIndex.end()) {
        mItems.at(iter->second)->value = value;
    } else {
        // check nullptr
        auto *kv = new (std::nothrow) KvPair();
        if (UNLIKELY(kv == nullptr)) {
            return false;
        }
        kv->name = key;
        kv->value = value;
        mItems.push_back(kv);
        mItemsIndex[key] = mItems.size() - 1;
    }
    return true;
}

inline uint32_t KVReader::Size()
{
    std::lock_guard<std::mutex> guard(mLock);
    return mItems.size();
}

inline void KVReader::GetI(uint32_t index, std::string &outKey, std::string &outValue)
{
    std::lock_guard<std::mutex> guard(mLock);
    if (index >= mItems.size()) {
        return;
    }

    outKey = mItems.at(index)->name;
    outValue = mItems.at(index)->value;
}

inline void KVReader::Dump()
{
    std::lock_guard<std::mutex> guard(mLock);
    for (auto val : mItems) {
        KvPair *p = val;
        printf("%s = %s\n", p->name.c_str(), p->value.c_str());
    }
}
}
}
#endif // BIO_KV_H
