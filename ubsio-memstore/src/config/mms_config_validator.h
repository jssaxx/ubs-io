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

#ifndef HDAGGER_DG_VALIDATOR_H
#define HDAGGER_DG_VALIDATOR_H

#include <climits>
#include <string>

#include "mms_ref.h"
#include "mms_types.h"
#include "mms_str_util.h"
#include "mms_functions.h"

namespace ock {
namespace mms {
class Validator;
using ValidatorPtr = Ref<Validator>;

class Validator {
public:
    virtual ~Validator() = default;

    virtual bool Initialize() = 0;

    virtual bool Validate(const std::string &)
    {
        return true;
    }

    virtual bool Validate(int)
    {
        return true;
    }

    virtual bool Validate(float)
    {
        return true;
    }

    virtual bool Validate(long)
    {
        return true;
    }

    const std::string &ErrorMessage()
    {
        return mErrMsg;
    }

    DEFINE_REF_COUNT_FUNCTIONS;

protected:
    explicit Validator(const std::string &name) : mName(name) {}

protected:
    std::string mName;
    std::string mErrMsg;

    DEFINE_REF_COUNT_VARIABLE;
};

class VStrRange : public Validator {
public:
    static ValidatorPtr Create(const std::string &name, int32_t start, int32_t end, bool emptyAllowed = false)
    {
        return ValidatorPtr(new (std::nothrow) VStrRange(name, start, end, emptyAllowed));
    }

    VStrRange(const std::string &name, int32_t start, int32_t end, bool emptyAllowed = false)
        : Validator(name), mStart(start), mEnd(end), mEmptyAllowed(emptyAllowed)
    {}

    ~VStrRange() override = default;

    bool Initialize() override
    {
        if (mStart >= mEnd) {
            mErrMsg = "Failed to initialize validator for <" + mName + ">, because end should be bigger than start";
            return false;
        }
        return true;
    }

    bool Validate(const std::string &value) override
    {
        // allow empty
        if (mEmptyAllowed && value.empty()) {
            return true;
        }

        // format should be 100-200
        // separated by -
        auto pos = value.find("~");
        if (pos == std::string::npos) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be range like 1000-2000";
            return false;
        }

        std::string val1 = value.substr(0, pos);
        std::string val2 = value.substr(pos + 1, value.length() - val1.length() - 1);
        if (val1.empty() || val2.empty()) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be range like 1000-2000";
            return false;
        }

        long lStart = 0;
        StrUtil::StrToLong(val1, lStart);
        long lEnd = 0;
        StrUtil::StrToLong(val2, lEnd);
        if ((lStart == -1) || (lStart == 0 && val1 != "0")) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be range like 1000-2000";
            return false;
        } else if ((lEnd == -1) || (lEnd == 0 && val2 != "0")) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be range like 1000-2000";
            return false;
        } else if ((lStart < mStart) || (lEnd > mEnd)) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be between " + std::to_string(mStart) + "~" +
                std::to_string(mEnd);
            return false;
        }

        return true;
    }

private:
    int32_t mStart = 0;
    int32_t mEnd = 0;
    bool mEmptyAllowed = false;
};

class VStrEnum : public Validator {
public:
    static ValidatorPtr Create(const std::string &name, const std::string &enumStr)
    {
        return ValidatorPtr(new (std::nothrow) VStrEnum(name, enumStr));
    }

    VStrEnum(const std::string &name, const std::string &enumStr) : Validator(name), mEnumString(enumStr) {}

    ~VStrEnum() override = default;

    bool Initialize() override
    {
        // enum string should like this
        // rc||tcp||xx
        std::set<std::string> validEnumSet;
        StrUtil::Split(mEnumString, "||", validEnumSet);
        if (validEnumSet.empty()) {
            mErrMsg = "Failed to initialize validator for <" + mName + ">, because enum string is not correct";
            return false;
        }
        return true;
    }

    bool Validate(const std::string &value) override
    {
        // if value has ||
        // for example rc||tcp
        if (value.find("||") != std::string::npos) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be one of " + mEnumString;
            return false;
        }

        // create ||rc||tcp||xx||
        // find ||rc||
        std::string tmp = "||" + mEnumString + "||";
        if (tmp.find("||" + value + "||") == std::string::npos) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be one of " + mEnumString;
            return false;
        }
        return true;
    }

private:
    std::string mEnumString;
};

class VStrNotNull : public Validator {
public:
    static ValidatorPtr Create(const std::string &name)
    {
        return ValidatorPtr(new (std::nothrow) VStrNotNull(name));
    }

    explicit VStrNotNull(const std::string &name) : Validator(name){};

    ~VStrNotNull() override = default;

    bool Initialize() override
    {
        return true;
    }

    bool Validate(const std::string &value) override
    {
        if (value.empty()) {
            mErrMsg = "Invalid value for <" + mName + ">, it should not be empty";
            return false;
        }
        return true;
    }
};

class VIntRange : public Validator {
public:
    static ValidatorPtr Create(const std::string &name, const int &start, const int &end)
    {
        return ValidatorPtr(new (std::nothrow) VIntRange(name, start, end));
    }
    VIntRange(const std::string &name, const int &start, const int &end) : Validator(name), mStart(start), mEnd(end){};

    ~VIntRange() override = default;

    bool Initialize() override
    {
        if (mStart >= mEnd) {
            mErrMsg = "Failed to initialize validator for <" + mName + ">, because end should be bigger than start";
            return false;
        }
        return true;
    }

    bool Validate(int value) override
    {
        if (value < mStart || value > mEnd) {
            if (mEnd == INT32_MAX) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be >= " + std::to_string(mStart);
            } else {
                mErrMsg = "Invalid value for <" + mName + ">, it should be between " + std::to_string(mStart) + "~" +
                    std::to_string(mEnd);
            }
            return false;
        }

        return true;
    }

private:
    int mStart;
    int mEnd;
};

class VLongRange : public Validator {
public:
    static ValidatorPtr Create(const std::string &name, const long &start, const long &end)
    {
        return ValidatorPtr(new (std::nothrow) VLongRange(name, start, end));
    }

    VLongRange(const std::string &name, const long &start, const long &end)
        : Validator(name), mStart(start), mEnd(end){};

    ~VLongRange() override = default;

    bool Initialize() override
    {
        if (mStart > mEnd) {
            mErrMsg = "Failed to initialize validator for <" + mName + ">, because end should be bigger than start";
            return false;
        }
        return true;
    }

    bool Validate(long value) override
    {
        if (value > mEnd || value < mStart) {
            if (mEnd == LONG_MAX) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be >= " + std::to_string(mStart);
            } else {
                mErrMsg = "Invalid value for <" + mName + ">, it should be between " + std::to_string(mStart) + "~" +
                    std::to_string(mEnd);
            }
            return false;
        }
        return true;
    }

private:
    long mStart;
    long mEnd;
};

class VStrArray : public Validator {
public:
    static ValidatorPtr Create(const std::string &name, int32_t start, int32_t end, uint32_t count,
        bool emptyAllowed = false)
    {
        return ValidatorPtr(new (std::nothrow) VStrArray(name, start, end, count, emptyAllowed));
    }

    VStrArray(const std::string &name, int32_t start, int32_t end, uint32_t count, bool emptyAllowed = false)
        : Validator(name), mStart(start), mEnd(end), mCount(count), mEmptyAllowed(emptyAllowed)
    {}

    ~VStrArray() override = default;

    bool Initialize() override
    {
        if (mStart >= mEnd) {
            mErrMsg = "Failed to initialize validator for <" + mName + ">, because end should be bigger than start";
            return false;
        }
        return true;
    }

    bool Validate(const std::string &value) override
    {
        // allow empty
        if (mEmptyAllowed && value.empty()) {
            return true;
        }

        std::vector<std::string> splitStrings;
        StrUtil::Split(value, ",", splitStrings);
        if (splitStrings.size() != mCount) {
            mErrMsg = "Invalid value for <" + mName + ">, there should be " + std::to_string(mCount) + " items";
            return false;
        }

        for (auto &item : splitStrings) {
            long longItem = 0;
            if (!(StrUtil::StrToLong(item, longItem))) {
                mErrMsg = "Invalid value for <" + mName + ">, all items must be integer";
                return false;
            }

            if ((longItem < mStart) || (longItem > mEnd)) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be between " + std::to_string(mStart) + "~" +
                    std::to_string(mEnd);
                return false;
            }
        }

        return true;
    }

private:
    int32_t mStart = 0;
    int32_t mEnd = 0;
    uint32_t mCount = 0;
    bool mEmptyAllowed = false;
};

/*
 * Validator for ip mask, for example: 192.168.100.1/24
 */
class VIpv4MaskValidator : public Validator {
public:
    static ValidatorPtr Create(const std::string &name, bool emptyAllowed = false)
    {
        return ValidatorPtr(new (std::nothrow) VIpv4MaskValidator(name, emptyAllowed));
    }

    explicit VIpv4MaskValidator(const std::string &name, bool emptyAllowed = false)
        : Validator(name), mEmptyAllowed(emptyAllowed)
    {}

    ~VIpv4MaskValidator() override = default;

    bool Initialize() override
    {
        return true;
    }

    bool Validate(const std::string &value) override
    {
        if (value.empty() && mEmptyAllowed) {
            return true;
        }

        std::vector<std::string> ipMaskVec;
        StrUtil::Split(value, "/", ipMaskVec);
        if (ipMaskVec.size() != NO_2) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0/24";
            return false;
        }

        /* check mask value which should be 0~32 */
        long tmp = 0;
        if (!StrUtil::StrToLong(ipMaskVec[NO_1], tmp)) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0/24";
            return false;
        }

        if (tmp < 0 || tmp > NO_32) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0/24";
            return false;
        }

        /* split ip and check each seg */
        std::vector<std::string> ip;
        StrUtil::Split(ipMaskVec[0], ".", ip);
        if (ip.size() != NO_4) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0/24";
            return false;
        }

        for (auto &item : ip) {
            /* check mask value which should be 0~32 */
            tmp = 0;
            if (!StrUtil::StrToLong(item, tmp)) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0/24";
                return false;
            }

            if (tmp < 0 || tmp >= NO_256) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0/24";
                return false;
            }
        }

        return true;
    }

private:
    bool mEmptyAllowed = false;
};

/*
 * Validator for ip mask, for example: 192.168.100.1/24
 */
class VIpv4Validator : public Validator {
public:
    static ValidatorPtr Create(const std::string &name, bool emptyAllowed = false)
    {
        return { new (std::nothrow) VIpv4Validator(name, emptyAllowed) };
    }

    explicit VIpv4Validator(const std::string &name, bool emptyAllowed = false)
        : Validator(name), mEmptyAllowed(emptyAllowed)
    {}

    ~VIpv4Validator() override = default;

    bool Initialize() override
    {
        return true;
    }

    bool Validate(const std::string &value) override
    {
        if (value.empty() && mEmptyAllowed) {
            return true;
        }

        /* split ip and check each seg */
        std::vector<std::string> ip;
        StrUtil::Split(value, ".", ip);
        if (ip.size() != NO_4) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0";
            return false;
        }

        long tmp = 0;
        for (auto &item : ip) {
            /* check mask value which should be 0~32 */
            tmp = 0;
            if (!StrUtil::StrToLong(item, tmp)) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0";
                return false;
            }

            if (tmp < 0 || tmp >= NO_256) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0";
                return false;
            }
        }

        return true;
    }

private:
    bool mEmptyAllowed = false;
};

/*
 * Validator for ip port, for example: 192.168.100.1:2334
 */
class VIpv4PortValidator : public Validator {
public:
    static ValidatorPtr Create(const std::string &name, bool emptyAllowed = false)
    {
        return { new (std::nothrow) VIpv4PortValidator(name, emptyAllowed) };
    }

    explicit VIpv4PortValidator(const std::string &name, bool emptyAllowed = false)
        : Validator(name), mEmptyAllowed(emptyAllowed)
    {}

    ~VIpv4PortValidator() override = default;

    bool Initialize() override
    {
        return true;
    }

    bool Validate(const std::string &value) override
    {
        if (value.empty() && mEmptyAllowed) {
            return true;
        }

        std::vector<std::string> ipPortVec;
        StrUtil::Split(value, ":", ipPortVec);
        if (ipPortVec.size() != NO_2) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0:8989";
            return false;
        }

        /* check port value which should be 0~63535 */
        long tmp = 0;
        if (!StrUtil::StrToLong(ipPortVec[NO_1], tmp)) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0:8989";
            return false;
        }

        static const uint16_t PORT_MAX = 0xFFFF;
        if (tmp < 0 || tmp > PORT_MAX) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0:8989";
            return false;
        }

        /* split ip and check each seg */
        std::vector<std::string> ip;
        StrUtil::Split(ipPortVec[0], ".", ip);
        if (ip.size() != NO_4) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0:8989";
            return false;
        }

        for (auto &item : ip) {
            /* check mask value which should be 0~32 */
            tmp = 0;
            if (!StrUtil::StrToLong(item, tmp)) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0:8989";
                return false;
            }

            if (tmp < 0 || tmp >= NO_256) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be between ip mask like 192.168.100.0:8989";
                return false;
            }
        }

        return true;
    }

private:
    bool mEmptyAllowed = false;
};

/*
 * Validator for ip port list, for example: 192.168.100.1:2334, 192.168.100.1:3345
 */
class VIpv4PortListValidator : public Validator {
public:
    static ValidatorPtr Create(const std::string &name)
    {
        return { new (std::nothrow) VIpv4PortListValidator(name) };
    }

    explicit VIpv4PortListValidator(const std::string &name) : Validator(name){};

    ~VIpv4PortListValidator() override = default;

    bool Initialize() override
    {
        return true;
    }

    bool Validate(const std::string &value) override
    {
        if (value.empty()) {
            mErrMsg = "Invalid value for <" + mName + ">, it should not be empty";
            return false;
        }

        std::vector<std::string> ipList;
        StrUtil::Split(value, ",", ipList);
        if (ipList.empty()) {
            mErrMsg = "Invalid value for <" + mName +
                ">, it should be ip port list seperated with ',', for example: 127.0.0.1:2909, 127.0.0.1:9987";
            return false;
        }

        VIpv4PortValidator subValidator(mName, false);
        subValidator.Initialize();
        for (auto &item : ipList) {
            if (!subValidator.Validate(item)) {
                mErrMsg = "Invalid value for <" + mName +
                    ">, it should be ip port list seperated with ',', for example: 127.0.0.1:2909, 127.0.0.1:9987";
                return false;
            }
        }

        return true;
    }
};

/*
 * Validator for file/dir permission, for example 700, 644
 */
class VFilePermissionValidator : public Validator {
public:
    static ValidatorPtr Create(const std::string &name, bool ownerBigThanOther = false)
    {
        return { new (std::nothrow) VFilePermissionValidator(name, ownerBigThanOther) };
    }

    VFilePermissionValidator(const std::string &name, bool ownerBigThanOther)
        : Validator(name), mOwnerBigThanOther(ownerBigThanOther){};

    ~VFilePermissionValidator() override = default;

    bool Initialize() override
    {
        return true;
    }

    bool Validate(const std::string &value) override
    {
        if (value.empty()) {
            mErrMsg = "Invalid value for <" + mName + ">, it should not be empty";
            return false;
        }

        if (value.size() != NO_3) {
            mErrMsg = "Invalid value for <" + mName + ">, it should be 3 digital, for example 755";
            return false;
        }

        for (auto &eachChar : value) {
            if (eachChar != '7' && eachChar != '6' && eachChar != '5' && eachChar != '4' && eachChar != '0') {
                mErrMsg = "Invalid value for <" + mName + ">, it should be 3 digital, for example 755";
                return false;
            }
        }

        if (value.at(0) == '0') {
            mErrMsg = "Invalid value for <" + mName + ">, it should be 3 digital, for example 755";
            return false;
        }

        /* owner permission is biggest */
        if (mOwnerBigThanOther) {
            if (value.at(0) < value.at(NO_1)) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be 3 digital, for example 755";
                return false;
            }

            if (value.at(NO_1) < value.at(NO_2)) {
                mErrMsg = "Invalid value for <" + mName + ">, it should be 3 digital, for example 755";
                return false;
            }
        }

        return true;
    }

private:
    bool mOwnerBigThanOther = false;
};

class VStrRatio : public Validator {
public:
    static ValidatorPtr Create(const std::string &name)
    {
        return ValidatorPtr(new (std::nothrow) VStrRatio(name));
    }

    explicit VStrRatio(const std::string &name) : Validator(name){};

    ~VStrRatio() override = default;

    bool Initialize() override
    {
        return true;
    }

    bool Validate(const std::string &value) override
    {
        return ValidateRatios(mName, value, mErrMsg);
    }
};

class VStrRealPath : public Validator {
public:
    static ValidatorPtr Create(const std::string &name)
    {
        return ValidatorPtr(new (std::nothrow) VStrRealPath(name));
    }

    explicit VStrRealPath(const std::string &name) : Validator(name){};

    ~VStrRealPath() override = default;

    bool Initialize() override
    {
        return true;
    }

    bool Validate(const std::string &value) override
    {
        if (value.empty()) {
            mErrMsg = "Invalid value for <" + mName + ">, it should not be empty";
            return false;
        }

        std::string tmpPath(value);
        if (!FileUtil::CanonicalPath(tmpPath)) {
            mErrMsg = "Invalid value for <" + mName + ">, path must exist";
            return false;
        }

        return true;
    }
};

class VStrCephPool : public Validator {
public:
    static ValidatorPtr Create(const std::string &name)
    {
        return ValidatorPtr(new (std::nothrow) VStrCephPool(name));
    }

    explicit VStrCephPool(const std::string &name) : Validator(name){};

    ~VStrCephPool() override = default;

    bool Initialize() override
    {
        return true;
    }

    bool Validate(const std::string &value) override
    {
        if (value.empty()) {
            mErrMsg = "Invalid value for <" + mName + ">, it should not be empty";
            return false;
        }

        std::vector<std::string> idWithPoolNames;
        StrUtil::Split(value, ",", idWithPoolNames);
        for (const auto &idWithPoolName : idWithPoolNames) {
            std::vector<std::string> idAndPoolName;
            StrUtil::Split(idWithPoolName, ":", idAndPoolName);
            long poolId = 0;
            if (idAndPoolName.size() != NO_2 || !StrUtil::StrToLong(idAndPoolName[0], poolId) || poolId < 0) {
                mErrMsg = "Invalid value for <" + mName + ">, it should like poolNo0:poolName0,poolNo1:poolName1";
                return false;
            }
        }

        return true;
    }
};

class VStrBoolRange : public Validator {
public:
    static ValidatorPtr Create(const std::string &name)
    {
        return ValidatorPtr(new (std::nothrow) VStrBoolRange(name));
    }

    explicit VStrBoolRange(const std::string &name) : Validator(name){};

    ~VStrBoolRange() override = default;

    bool Initialize() override
    {
        return true;
    }

    bool Validate(const std::string &value) override
    {
        if (value != "true" && value != "false") {
            mErrMsg = "Invalid value for <" + mName + ">, it should be true or false";
            return false;
        }
        return true;
    }
};
}
}
#endif // HDAGGER_DG_VALIDATOR_H
