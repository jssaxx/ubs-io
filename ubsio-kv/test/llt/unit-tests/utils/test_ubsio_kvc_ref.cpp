/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <atomic>
#include <utility>
#include "test_common.h"
#include "ubsio_kvc_ref.h"

using namespace ock::ubsio;

namespace {
class TrackedReferable : public Referable {
public:
    explicit TrackedReferable(std::atomic<int> *destroyed) : mDestroyed(destroyed) {}
    ~TrackedReferable() override
    {
        ++(*mDestroyed);
    }

    int value = 7;

private:
    std::atomic<int> *mDestroyed;
};

TEST_F(KvTest, ReferenceUtilityCoversOwnershipTransitions)
{
    std::atomic<int> destroyed{0};
    Ref<TrackedReferable> empty;
    Ref<TrackedReferable> nullRaw(nullptr);
    Ref<TrackedReferable> emptyCopy(empty);
    EXPECT_EQ(empty.Get(), nullptr);
    EXPECT_EQ(nullRaw.Get(), nullptr);
    EXPECT_EQ(emptyCopy.Get(), nullptr);
    EXPECT_TRUE(empty == nullptr);
    EXPECT_FALSE(empty != nullptr);

    auto first = MakeRef<TrackedReferable>(&destroyed);
    ASSERT_NE(first.Get(), nullptr);
    EXPECT_EQ(first->value, 7);
    EXPECT_EQ(first->GetRef(), 1);

    Ref<TrackedReferable> copy(first);
    EXPECT_EQ(first->GetRef(), 2);
    Ref<TrackedReferable> moved(std::move(copy));
    EXPECT_EQ(copy.Get(), nullptr);
    EXPECT_TRUE(first == moved);
    EXPECT_FALSE(first != moved);
    EXPECT_TRUE(first == first.Get());
    EXPECT_FALSE(first != first.Get());

    Ref<TrackedReferable> assigned;
    assigned = first;
    EXPECT_EQ(first->GetRef(), 3);
    assigned = assigned;
    assigned = first.Get();
    EXPECT_EQ(first->GetRef(), 3);

    Ref<TrackedReferable> movedAssigned;
    movedAssigned = std::move(assigned);
    EXPECT_EQ(assigned.Get(), nullptr);
    movedAssigned = std::move(movedAssigned);

    auto replacement = MakeRef<TrackedReferable>(&destroyed);
    ASSERT_NE(replacement.Get(), nullptr);
    replacement = std::move(movedAssigned);
    EXPECT_EQ(destroyed.load(), 1);
    first.Set(nullptr);
    moved = nullptr;
    EXPECT_EQ(destroyed.load(), 1);
    replacement = nullptr;
    EXPECT_EQ(destroyed.load(), 2);
}
}
