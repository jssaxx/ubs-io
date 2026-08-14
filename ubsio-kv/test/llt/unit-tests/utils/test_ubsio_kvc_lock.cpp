/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include "test_common.h"
#include "ubsio_kvc_lock.h"

using namespace ock::ubsio;

namespace {
TEST_F(KvTest, LockUtilitiesCoverScopesAndModes)
{
    Lock lock;
    {
        Locker<Lock> guard(&lock);
    }
    {
        Locker<Lock> guard(nullptr);
    }

    RecursiveLock recursiveLock;
    recursiveLock.DoLock();
    {
        Locker<RecursiveLock> guard(&recursiveLock);
    }
    recursiveLock.UnLock();

    ReadWriteLock readWriteLock;
    {
        ReadLocker<ReadWriteLock> guard(&readWriteLock);
    }
    {
        ReadLocker<ReadWriteLock> guard(nullptr);
    }
    {
        WriteLocker<ReadWriteLock> guard(&readWriteLock);
    }
    {
        WriteLocker<ReadWriteLock> guard(nullptr);
    }

    SpinLock spinLock;
    spinLock.TryLock();
    spinLock.Unlock();
    spinLock.Lock();
    spinLock.Unlock();
}
}
