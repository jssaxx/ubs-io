#ifndef MMS_TEST_SERVER_H
#define MMS_TEST_SERVER_H

#include <mockcpp/mockcpp.hpp>
#include "gtest/gtest.h"

namespace ock {
namespace mms {
class TestServer : public testing::Test {
    void SetUp() override;

    void TearDown() override;

private:
    static bool gSetup;
};
}
}
#endif

