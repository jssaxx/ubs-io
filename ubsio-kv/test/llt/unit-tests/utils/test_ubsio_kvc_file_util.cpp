/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#include <string>
#include "test_common.h"
#include "ubsio_kvc_file_util.h"

using namespace ock::ubsio;

namespace {
TEST_F(KvTest, FileUtilityCoversManagedPaths)
{
    std::string rootPath = ".";
    EXPECT_TRUE(FileUtil::Exist(rootPath));
    std::string canonicalPath = rootPath;
    EXPECT_TRUE(FileUtil::CanonicalPath(canonicalPath));
    EXPECT_TRUE(FileUtil::Exist(canonicalPath));

    std::string missingPath = canonicalPath + "/file-that-must-not-exist";
    EXPECT_FALSE(FileUtil::Exist(missingPath));
    EXPECT_FALSE(FileUtil::CanonicalPath(missingPath));
    std::string emptyPath;
    EXPECT_FALSE(FileUtil::CanonicalPath(emptyPath));
    std::string oversizedPath(4097, 'x');
    EXPECT_FALSE(FileUtil::CanonicalPath(oversizedPath));
}
}
