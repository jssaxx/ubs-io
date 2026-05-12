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

#ifndef MMSCORE_MMS_REF_H
#define MMSCORE_MMS_REF_H

#include <cstdint>
#include <utility>

namespace ock {
namespace mms {
#ifndef DEFINE_REF_COUNT_FUNCTIONS
#define DEFINE_REF_COUNT_FUNCTIONS                         \
public:                                                    \
    inline void IncreaseRef()                              \
    {                                                      \
        __sync_fetch_and_add(&mRefCount, 1);               \
    }                                                      \
                                                           \
    inline void DecreaseRef()                              \
    {                                                      \
        int32_t tmp = __sync_sub_and_fetch(&mRefCount, 1); \
        if (tmp == 0) {                                    \
            delete this;                                   \
        }                                                  \
    }                                                      \
                                                           \
    inline int32_t GetRef()                                \
    {                                                      \
        return __sync_sub_and_fetch(&mRefCount, 0);        \
    }
#endif

#ifndef DEFINE_REF_COUNT_VARIABLE
#define DEFINE_REF_COUNT_VARIABLE \
private:                          \
    int32_t mRefCount = 0
#endif

class Referable {
public:
    Referable() = default;
    virtual ~Referable() = default;

    inline void IncreaseRef()
    {
        __sync_fetch_and_add(&mRefCount, 1);
    }

    inline void DecreaseRef()
    {
        // delete itself if reference count equal to 0
        if (__sync_sub_and_fetch(&mRefCount, 1) == 0) {
            delete this;
        }
    }

    inline int32_t GetRef()
    {
        return __sync_fetch_and_add(&mRefCount, 0);
    }

protected:
    int32_t mRefCount = 0;
};

#if __GNUC__ == 4 && __GNUC_MINOR__ == 8 && __GNUC_PATCHLEVEL__ == 5
template <class T, class U = T> T exchangeHdagger(T &obj, U &&new_value)
{
    T old_value = std::move(obj);
    obj = std::forward<U>(new_value);
    return old_value;
}
#endif

template <typename T> class Ref {
public:
    // constructor
    Ref() noexcept = default;

    // fix: can't be explicit
    Ref(T *newObj) noexcept
    {
        // if new obj is not null, increase reference count and assign to mObj
        // else nothing need to do as mObj is nullptr by default
        if (newObj != nullptr) {
            newObj->IncreaseRef();
            mObj = newObj;
        }
    }

    Ref(const Ref<T> &other) noexcept
    {
        // if other's obj is not null, increase reference count and assign to mObj
        // else nothing need to do as mObj is nullptr by default
        if (other.mObj != nullptr) {
            other.mObj->IncreaseRef();
            mObj = other.mObj;
        }
    }

#if __GNUC__ == 4 && __GNUC_MINOR__ == 8 && __GNUC_PATCHLEVEL__ == 5
    Ref(Ref<T> &&other) noexcept : mObj(exchangeHdagger(other.mObj, nullptr))
#else
    Ref(Ref<T> &&other) noexcept : mObj(std::__exchange(other.mObj, nullptr))
#endif
    {
        // move constructor
        // since this mObj is null, just exchange
    }

    // de-constructor
    ~Ref()
    {
        if (mObj != nullptr) {
            mObj->DecreaseRef();
        }
    }

    // operator =
    inline Ref<T> &operator = (T *newObj)
    {
        this->Set(newObj);
        return *this;
    }

    inline Ref<T> &operator = (const Ref<T> &other)
    {
        if (this != &other) {
            this->Set(other.mObj);
        }
        return *this;
    }

    Ref<T> &operator = (Ref<T> &&other) noexcept
    {
        if (this != &other) {
            auto tmp = mObj;
#if __GNUC__ == 4 && __GNUC_MINOR__ == 8 && __GNUC_PATCHLEVEL__ == 5
            mObj = exchangeHdagger(other.mObj, nullptr);
#else
            mObj = std::__exchange(other.mObj, nullptr);
#endif
            if (tmp != nullptr) {
                tmp->DecreaseRef();
            }
        }
        return *this;
    }

    // equal operator
    inline bool operator == (const Ref<T> &other) const
    {
        return mObj == other.mObj;
    }

    inline bool operator == (T *other) const
    {
        return mObj == other;
    }

    inline bool operator != (const Ref<T> &other) const
    {
        return mObj != other.mObj;
    }

    inline bool operator != (T *other) const
    {
        return mObj != other;
    }

    // get operator and set
    inline T *operator->() const
    {
        return mObj;
    }

    inline T *Get() const
    {
        return mObj;
    }

    inline void Set(T *newObj)
    {
        if (newObj == mObj) {
            return;
        }

        if (newObj != nullptr) {
            newObj->IncreaseRef();
        }

        if (mObj != nullptr) {
            mObj->DecreaseRef();
        }

        mObj = newObj;
    }

private:
    T *mObj = nullptr;
};

/*
 * @brief New an object return with ref object
 *
 * @param args             [in] args of object
 *
 * @return Ref object, if new failed internal, an empty Ref object will be returned
 */
template <typename C, typename... ARGS> static inline Ref<C> MakeRef(ARGS... args)
{
    return new (std::nothrow) C(args...);
}
}
}
#endif // MMSCORE_MMS_REF_H

