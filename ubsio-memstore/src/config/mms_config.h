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

#ifndef HDAGGER_DG_CONFIGURATION_H
#define HDAGGER_DG_CONFIGURATION_H

#include <map>
#include <string>
#include <iostream>
#include <sstream>
#include <mutex>

#include "mms_ref.h"
#include "mms_config_kv_reader.h"
#include "mms_config_validator.h"

namespace ock {
namespace mms {
class Configuration;

using ConfigurationPtr = Ref<Configuration>;

enum ConfValueType {
    VINT = 0,
    VFLOAT = 1,
    VSTRING = 2,
    VBOOL = 3,
    VLONG = 4,
};

using ValidatorTag = uint16_t;

class Configuration {
public:
    static bool ReadConf(const std::string &path, KVReader &kv)
    {
        /* check path exist or not */
        if (!FileUtil::Exist(path)) {
            std::cout << "Load conf failed as file " << path << " doesn't exists" << std::endl;
            return false;
        }

        /* check with realpath */
        std::string realPath = path;
        if (!FileUtil::CanonicalPath(realPath)) {
            std::cout << "Load conf failed as file " << path << " is not real path" << std::endl;
            return false;
        }

        /* read kv from file */
        if (!kv.FromFile(realPath)) {
            std::cout << "Load conf failed as load file " << path << " failed" << std::endl;
            return false;
        }

        return true;
    }

    template <class T> static bool ReadConf(const std::string &path, bool stopIfInvalid = true)
    {
        KVReader kv;
        bool ret = ReadConf(path, kv);
        if (!ret) {
            std::cout << "Failed to create key/value reader." << std::endl;
            return false;
        }

        auto conf = GetInstance<T>();
        if (conf.Get() == nullptr) {
            std::cout << "Failed to get instance for config." << std::endl;
            return false;
        }

        /* set kv into conf */
        uint32_t size = kv.Size();
        for (uint32_t i = 0; i < size; i++) {
            std::string key;
            std::string value;
            kv.GetI(i, key, value);
            if (!conf->SetWithTypeAutoConvert(key, value) && stopIfInvalid) {
                std::cout << "Failed to set a key/value pair for key<" << key << "> value<" << value << ">" <<
                    std::endl;
                return false;
            }
        }

        return true;
    }

    template <class T> static ConfigurationPtr GetInstance()
    {
        static ConfigurationPtr gInstance = nullptr;
        static std::mutex gLock;

        if (gInstance == nullptr) {
            std::lock_guard<std::mutex> guard(gLock);
            if (gInstance != nullptr) {
                return gInstance;
            }

            gInstance = new (std::nothrow) T();
            if (gInstance == nullptr) {
                std::cout << "Failed to new configuration object, probably out of memory" << std::endl;
                return nullptr;
            }

            /* load default conf */
            gInstance->LoadDefaultConf();
        }

        return gInstance;
    }

public:
    Configuration() = default;
    virtual ~Configuration() = default;

    int32_t GetInt(const std::string &key, int32_t defaultValue = 0);
    float GetFloat(const std::string &key, float defaultValue = 0.0f);
    const std::string &GetStr(const std::string &key, const std::string &defaultValue = "");
    bool GetBool(const std::string &key, bool defaultValue = false);
    long GetLong(const std::string &key, long defaultValue = 0);

    void Set(const std::string &key, int32_t value);
    void Set(const std::string &key, float value);
    void Set(const std::string &key, const std::string &value);
    void Set(const std::string &key, bool value);
    void Set(const std::string &key, long value);

    void AddIntConf(const std::pair<std::string, int> &, const ValidatorPtr &validator = nullptr, ValidatorTag tag = 0);
    void AddFloatConf(const std::pair<std::string, float> &, const ValidatorPtr &validator = nullptr,
        ValidatorTag tag = 0);
    void AddStrConf(const std::pair<std::string, std::string> &, const ValidatorPtr &validator = nullptr,
        ValidatorTag tag = 0);
    void AddBoolConf(const std::pair<std::string, bool> &pair);
    void AddLongConf(const std::pair<std::string, long> &, const ValidatorPtr &validator = nullptr,
        ValidatorTag tag = 0);

    void AddValidator(const std::string &key, const ValidatorPtr &validator, ValidatorTag tag);

    std::vector<std::string> Validate(ValidatorTag tag = 0);

    bool MergeConf(const ConfigurationPtr &config, bool stopIfInvalid = true);

    void Dump();
    void Dump(KVReader &reader);

    DEFINE_REF_COUNT_FUNCTIONS;

protected:
    virtual void LoadDefaultConf(){};

protected:
    bool SetWithTypeAutoConvert(const std::string &key, const std::string &value, bool skipIfLack = false);

    void ValidateOneType(const std::string &, const ValidatorPtr &, const ConfValueType &, std::vector<std::string> &);

protected:
    std::map<std::string, int32_t> mIntItems;     /* int32_t value config */
    std::map<std::string, float> mFloatItems;     /* float value config */
    std::map<std::string, std::string> mStrItems; /* string value config */
    std::map<std::string, bool> mBoolItems;       /* bool value config */
    std::map<std::string, long> mLongItems;       /* long value config */

    std::map<std::string, ConfValueType> mValueTypes; /* value type of keys */

    std::map<ValidatorTag, std::map<std::string, ValidatorPtr>> mTagValueValidator; /* validator */

    std::mutex mMutex;

    DEFINE_REF_COUNT_VARIABLE;
};

inline bool Configuration::SetWithTypeAutoConvert(const std::string &key, const std::string &value, bool skipIfLack)
{
    ConfValueType valueType = ConfValueType::VSTRING;
    {
        auto iter = mValueTypes.find(key);
        if (iter != mValueTypes.end()) {
            valueType = iter->second;
        } else if (skipIfLack) {
            return true;
        } else {
            std::cout << "<" << key << ">, it is an unknown key, skip it." << std::endl;
            return false;
        }

        if (valueType == ConfValueType::VINT) {
            long tmp = 0;
            if (!StrUtil::StrToLong(value, tmp)) {
                std::cout << "<" << key << ">, it was empty or in wrong type, it should be a int number." << std::endl;
                return false;
            }
            mIntItems[key] = static_cast<int32_t>(tmp);
        } else if (valueType == ConfValueType::VFLOAT) {
            if (!StrUtil::StrToFloat(value, mFloatItems[key])) {
                std::cout << "<" << key << ">, it was empty or in wrong type, it should be a float number." <<
                    std::endl;
                return false;
            }
        } else if (valueType == ConfValueType::VSTRING) {
            mStrItems[key] = value;
        } else if (valueType == ConfValueType::VBOOL) {
            bool b = false;
            std::istringstream(value) >> std::boolalpha >> b;
            mBoolItems[key] = b;
        } else if (valueType == ConfValueType::VLONG) {
            if (!StrUtil::StrToLong(value, mLongItems[key])) {
                std::cout << "<" << key << ">, it was empty or in wrong type, it should be a long number." << std::endl;
                return false;
            }
        }

        return true;
    }
}

inline int32_t Configuration::GetInt(const std::string &key, int32_t defaultValue)
{
    std::lock_guard<std::mutex> guard(mMutex);
    auto iter = mIntItems.find(key);
    if (iter != mIntItems.end()) {
        return iter->second;
    }

    return defaultValue;
}

inline float Configuration::GetFloat(const std::string &key, float defaultValue)
{
    std::lock_guard<std::mutex> guard(mMutex);
    auto iter = mFloatItems.find(key);
    if (iter != mFloatItems.end()) {
        return iter->second;
    }
    return defaultValue;
}

inline const std::string &Configuration::GetStr(const std::string &key, const std::string &defaultValue)
{
    std::lock_guard<std::mutex> guard(mMutex);
    auto iter = mStrItems.find(key);
    if (iter != mStrItems.end()) {
        return iter->second;
    }
    return defaultValue;
}

inline bool Configuration::GetBool(const std::string &key, bool defaultValue)
{
    std::lock_guard<std::mutex> guard(mMutex);
    auto iter = mBoolItems.find(key);
    if (iter != mBoolItems.end()) {
        return iter->second;
    }
    return defaultValue;
}

inline long Configuration::GetLong(const std::string &key, long defaultValue)
{
    std::lock_guard<std::mutex> guard(mMutex);
    auto iter = mLongItems.find(key);
    if (iter != mLongItems.end()) {
        return iter->second;
    }
    return defaultValue;
}

inline void Configuration::Set(const std::string &key, int32_t value)
{
    std::lock_guard<std::mutex> guard(mMutex);
    mIntItems[key] = value;
}

inline void Configuration::Set(const std::string &key, float value)
{
    std::lock_guard<std::mutex> guard(mMutex);
    mFloatItems[key] = value;
}

inline void Configuration::Set(const std::string &key, const std::string &value)
{
    std::lock_guard<std::mutex> guard(mMutex);
    mStrItems[key] = value;
}

inline void Configuration::Set(const std::string &key, bool value)
{
    std::lock_guard<std::mutex> guard(mMutex);
    mBoolItems[key] = value;
}

inline void Configuration::Set(const std::string &key, long value)
{
    std::lock_guard<std::mutex> guard(mMutex);
    mLongItems[key] = value;
}

inline void Configuration::AddValidator(const std::string &key, const ValidatorPtr &validator, ValidatorTag tag)
{
    if (UNLIKELY(key.empty())) {
        std::cout << "Failed to added validator as key is empty" << std::endl;
        return;
    } else if (validator == nullptr) {
        return;
    }

    auto tagIter = mTagValueValidator.find(tag);
    if (tagIter == mTagValueValidator.end()) {
        /* no tag added yet, add new map */
        std::map<std::string, ValidatorPtr> tmpMap;
        tmpMap[key] = validator;
        mTagValueValidator.emplace(tag, tmpMap);
    } else {
        /* already exist */
        tagIter->second[key] = validator;
    }
}

inline void Configuration::AddIntConf(const std::pair<std::string, int> &pair, const ValidatorPtr &validator,
    ValidatorTag tag)
{
    mIntItems[pair.first] = pair.second;
    mValueTypes[pair.first] = ConfValueType::VINT;
    AddValidator(pair.first, validator, tag);
}

inline void Configuration::AddFloatConf(const std::pair<std::string, float> &pair, const ValidatorPtr &validator,
    ValidatorTag tag)
{
    mFloatItems[pair.first] = pair.second;
    mValueTypes[pair.first] = ConfValueType::VFLOAT;
    AddValidator(pair.first, validator, tag);
}

inline void Configuration::AddStrConf(const std::pair<std::string, std::string> &pair, const ValidatorPtr &validator,
    ValidatorTag tag)
{
    // check nullptr
    mStrItems[pair.first] = pair.second;
    mValueTypes[pair.first] = ConfValueType::VSTRING;
    AddValidator(pair.first, validator, tag);
}

inline void Configuration::AddBoolConf(const std::pair<std::string, bool> &pair)
{
    mBoolItems[pair.first] = pair.second;
    mValueTypes[pair.first] = ConfValueType::VBOOL;
}

inline void Configuration::AddLongConf(const std::pair<std::string, long> &pair, const ValidatorPtr &validator,
    ValidatorTag tag)
{
    mLongItems[pair.first] = pair.second;
    mValueTypes[pair.first] = ConfValueType::VLONG;
    AddValidator(pair.first, validator, tag);
}

inline void Configuration::ValidateOneType(const std::string &key, const ValidatorPtr &validator,
    const ConfValueType &vType, std::vector<std::string> &errors)
{
    // find the configured value according to the type
    if (vType == ConfValueType::VSTRING) {
        auto valueIter = mStrItems.find(key);
        if (valueIter == mStrItems.end()) {
            errors.push_back("Failed to find <" + key + "> in string value map, which should not happen.");
            return;
        }

        // validate the value
        if (!(validator->Validate(valueIter->second))) {
            errors.push_back(validator->ErrorMessage());
            return;
        }
    } else if (vType == ConfValueType::VFLOAT) {
        auto valueIter = mFloatItems.find(key);
        if (valueIter == mFloatItems.end()) {
            errors.push_back("Failed to find <" + key + "> in float value map, which should not happen.");
            return;
        }

        // validate the value
        if (!(validator->Validate(valueIter->second))) {
            errors.push_back(validator->ErrorMessage());
            return;
        }
    } else if (vType == ConfValueType::VINT) {
        auto valueIter = mIntItems.find(key);
        if (valueIter == mIntItems.end()) {
            errors.push_back("Failed to find <" + key + "> in int value map, which should not happen.");
            return;
        }

        // validate the value
        if (!(validator->Validate(valueIter->second))) {
            errors.push_back(validator->ErrorMessage());
            return;
        }
    } else if (vType == ConfValueType::VLONG) {
        auto valueIter = mLongItems.find(key);
        if (valueIter == mLongItems.end()) {
            errors.push_back("Failed to find <" + key + "> in long value map, which should not happen.");
            return;
        }

        // validate the value
        if (!(validator->Validate(valueIter->second))) {
            errors.push_back(validator->ErrorMessage());
            return;
        }
    }
}

inline std::vector<std::string> Configuration::Validate(ValidatorTag tag)
{
    std::vector<std::string> errors;
    auto tagValidatorIter = mTagValueValidator.find(tag);
    if (tagValidatorIter == mTagValueValidator.end()) {
        errors.emplace_back("No validator found for tag found");
        return errors;
    }

    /* validate one tag */
    for (auto &item : tagValidatorIter->second) {
        if (item.second.Get() == nullptr) {
            errors.push_back("The validator of <" + item.first + "> is null, skip.");
            continue;
        } else {
            /* initialize, if failed then skip it */
            if (!(item.second->Initialize())) {
                errors.push_back(item.second->ErrorMessage());
                continue;
            }
        }

        /* firstly find the value type */
        auto typeIter = mValueTypes.find(item.first);
        if (typeIter == mValueTypes.end()) {
            errors.push_back("Failed to find <" + item.first + "> in type map, which should not happen.");
            continue;
        }

        ValidateOneType(item.first, item.second, typeIter->second, errors);
    }

    return errors;
}

inline bool Configuration::MergeConf(const ConfigurationPtr &config, bool stopIfInvalid)
{
    KVReader reader;
    config->Dump(reader);
    uint32_t size = reader.Size();
    for (uint32_t i = 0; i < size; i++) {
        std::string key;
        std::string value;
        reader.GetI(i, key, value);
        if (!SetWithTypeAutoConvert(key, value) && stopIfInvalid) {
            return false;
        }
    }

    return true;
}

inline void Configuration::Dump()
{
    std::lock_guard<std::mutex> guard(mMutex);
    printf("Configuration Dump:\n");
    for (auto &item : mIntItems) {
        printf("i] %s = %d\n", item.first.c_str(), item.second);
    }
    for (auto &item : mFloatItems) {
        printf("f] %s = %f\n", item.first.c_str(), item.second);
    }
    for (auto &item : mStrItems) {
        printf("s] %s = %s\n", item.first.c_str(), item.second.c_str());
    }
    for (auto &item : mBoolItems) {
        printf("b] %s = %s\n", item.first.c_str(), item.second ? "true" : "false");
    }
    for (auto &item : mLongItems) {
        printf("l] %s = %ld\n", item.first.c_str(), item.second);
    }
}

inline void Configuration::Dump(KVReader &reader)
{
    std::lock_guard<std::mutex> guard(mMutex);
    for (auto &item : mIntItems) {
        reader.SetItem(item.first, std::to_string(item.second));
    }
    for (auto &item : mFloatItems) {
        reader.SetItem(item.first, std::to_string(item.second));
    }
    for (auto &item : mStrItems) {
        reader.SetItem(item.first, item.second);
    }
    for (auto &item : mBoolItems) {
        reader.SetItem(item.first, item.second ? "true" : "false");
    }
    for (auto &item : mLongItems) {
        reader.SetItem(item.first, std::to_string(item.second));
    }
}
}
}

#endif // HDAGGER_DG_CONFIGURATION_H

