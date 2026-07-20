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

#include "test_common.h"
#include <cstring>
#include "bio_config.h"
#include "bio_file_util.h"
#include "bio_functions.h"
#include "bio_log.h"
#define private public
#include "bio_config_instance.h"
#undef private
#include "bio_config_validator.h"
#include "bio_str_util.h"
#include "bio_tls_util.h"
#include "message.h"
#include "net_common.h"

using namespace ock::bio;

namespace {
class UnitConfiguration : public Configuration {
public:
    using Configuration::SetWithTypeAutoConvert;

protected:
    void LoadDefaultConf() override {}
};
} // namespace

bool TestCommon::gSetup = false;

void TestCommon::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestCommon::TearDown()
{
    return;
}

TEST_F(TestCommon, test_strtofloat_return_ok)
{
    LOG_INFO("test_strtofloat_return_ok");
    float value = 0.0f;
    auto ret = StrUtil::StrToFloat("123.5", value);
    EXPECT_EQ(ret, true);
    EXPECT_EQ(value, 123.5f);
}

TEST_F(TestCommon, test_strstartwith_return_ok)
{
    LOG_INFO("test_strstartwith_return_ok");
    auto ret = StrUtil::StartWith("123.5", "1");
    EXPECT_EQ(ret, true);
}

TEST_F(TestCommon, test_copy_key_fail)
{
    LOG_INFO("test_copy_key_fail");
    CopyKey(nullptr, nullptr, KEY_MAX_SIZE);

    auto ret = StrUtil::StartWith("123.5", "1");
    EXPECT_EQ(ret, true);
}

TEST_F(TestCommon, test_file_util_basic_ops)
{
    const std::string base = "./file_util_ut";
    const std::string nested = base + "/a/b";
    const std::string file = nested + "/conf.txt";
    const std::string bak = nested + "/conf.bak";
    FileUtil::RemoveDirRecursive(base);

    EXPECT_FALSE(FileUtil::MakeDir("", 0755));
    EXPECT_TRUE(FileUtil::MakeDir(base, 0755));
    EXPECT_TRUE(FileUtil::Exist(base));
    EXPECT_TRUE(FileUtil::MakeDirRecursive(nested, 0755));
    EXPECT_TRUE(FileUtil::Readable(nested));
    EXPECT_TRUE(FileUtil::Writable(nested));
    EXPECT_TRUE(FileUtil::ReadAndWritable(nested));

    std::vector<std::string> lines = {"alpha=1", "beta=2"};
    EXPECT_TRUE(FileUtil::WriteFile(file, lines));
    std::vector<std::string> readLines;
    EXPECT_TRUE(FileUtil::ReadFile(file, readLines));
    EXPECT_EQ(readLines.size(), 2UL);
    EXPECT_EQ(FileUtil::FindTargetLine(readLines, "beta"), 1);
    EXPECT_TRUE(FileUtil::AppendConfigToLine(readLines, "beta", ",gamma=3"));
    EXPECT_FALSE(FileUtil::AppendConfigToLine(readLines, "missing", "=4"));
    EXPECT_TRUE(FileUtil::BackUpFile(file, bak));

    std::string realPath = file;
    EXPECT_TRUE(FileUtil::CanonicalPath(realPath));
    std::string capacityFile = file;
    EXPECT_GT(FileUtil::GetDiskCapacity(capacityFile), 0);
    EXPECT_TRUE(FileUtil::RenameFile(bak, bak));
    EXPECT_TRUE(FileUtil::RenameFile(bak, bak + ".new"));
    EXPECT_TRUE(FileUtil::RemoveFile(bak + ".new"));
    EXPECT_TRUE(FileUtil::Remove(file, false));
    EXPECT_TRUE(FileUtil::RemoveDirRecursive(base));
    EXPECT_FALSE(FileUtil::RemoveDirRecursive(base));
}

TEST_F(TestCommon, test_tls_and_net_common_helpers)
{
    char plain[NO_32] = {};
    size_t plainLen = 0;
    EXPECT_EQ(TlsUtil::DefaultDecrypter("abc", 3, plain, &plainLen), 0);
    EXPECT_EQ(plainLen, 3UL);
    EXPECT_EQ(std::strncmp(plain, "abc", 3), 0);
    EXPECT_EQ(TlsUtil::LoadDecryptFunction("./not_exist_decrypt_lib.so"), nullptr);
    TlsUtil::CloseTlsLib();

    NetNode node(7, 9);
    NetNode nodeCopy(node);
    EXPECT_EQ(nodeCopy.nid, 7U);
    EXPECT_EQ(nodeCopy.pid, 9U);
    NetNode assigned;
    assigned = nodeCopy;
    EXPECT_EQ(assigned.whole, nodeCopy.whole);

    NetConnPayload payload(node);
    bool isCtrl = false;
    EXPECT_EQ(payload.FromPayloadStr(payload.ToPayloadStr(CONN_PAYLOAD_PREFIX_CTRL), isCtrl), BIO_OK);
    EXPECT_TRUE(isCtrl);
    EXPECT_EQ(payload.FromPayloadStr(payload.ToPayloadStr(CONN_PAYLOAD_PREFIX_DATA), isCtrl), BIO_OK);
    EXPECT_FALSE(isCtrl);
    EXPECT_EQ(payload.FromPayloadStr("bad-prefix", isCtrl), BIO_INVALID_PARAM);
    EXPECT_EQ(payload.FromPayloadStr(CONN_PAYLOAD_PREFIX_DATA + std::string("bad"), isCtrl), BIO_INVALID_PARAM);

    NetChannelUpCtx ctrl(node, true, true);
    EXPECT_TRUE(ctrl.IsCtrlPanel());
    NetChannelUpCtx data(node, false, false);
    EXPECT_FALSE(data.IsCtrlPanel());

    NetOptions opt;
    opt.FillNetBaseConfigs(NO_2, NO_3, Role::NET_CLIENT, ock::hcom::UDS);
    EXPECT_EQ(opt.handlerCount, NO_2);
    EXPECT_EQ(opt.connCount, NO_3);
    EXPECT_EQ(opt.role, Role::NET_CLIENT);
    opt.FillNetTlsConfigs(true, "cert", "ca", "crl", "key", "pw", "dec");
    EXPECT_TRUE(opt.enableTls);
    EXPECT_EQ(opt.privateKeyPassword, "pw");
}

TEST_F(TestCommon, test_configuration_core_paths)
{
    UnitConfiguration conf;
    conf.AddIntConf({"int.key", 1}, VIntRange::Create("int.key", 0, 10), 1);
    conf.AddFloatConf({"float.key", 1.5F});
    conf.AddStrConf({"str.key", "value"}, VStrNotNull::Create("str.key"), 1);
    conf.AddBoolConf({"bool.key", false});
    conf.AddLongConf({"long.key", 10L}, VLongRange::Create("long.key", 1L, 100L), 1);

    EXPECT_TRUE(conf.SetWithTypeAutoConvert("int.key", "8"));
    EXPECT_FALSE(conf.SetWithTypeAutoConvert("int.key", "bad"));
    EXPECT_FALSE(conf.SetWithTypeAutoConvert("int.key", std::to_string(static_cast<long>(INT32_MAX) + 1L)));
    EXPECT_TRUE(conf.SetWithTypeAutoConvert("float.key", "2.5"));
    EXPECT_FALSE(conf.SetWithTypeAutoConvert("float.key", "bad"));
    EXPECT_TRUE(conf.SetWithTypeAutoConvert("float.key", "2.5"));
    EXPECT_TRUE(conf.SetWithTypeAutoConvert("str.key", "abc"));
    EXPECT_TRUE(conf.SetWithTypeAutoConvert("bool.key", "true"));
    EXPECT_TRUE(conf.SetWithTypeAutoConvert("long.key", "20"));
    EXPECT_FALSE(conf.SetWithTypeAutoConvert("long.key", "bad"));
    EXPECT_TRUE(conf.SetWithTypeAutoConvert("long.key", "20"));
    EXPECT_TRUE(conf.SetWithTypeAutoConvert("missing.key", "1", true));
    EXPECT_FALSE(conf.SetWithTypeAutoConvert("missing.key", "1", false));

    EXPECT_EQ(conf.GetInt("int.key"), 8);
    EXPECT_FLOAT_EQ(conf.GetFloat("float.key"), 2.5F);
    EXPECT_EQ(conf.GetStr("str.key"), "abc");
    EXPECT_TRUE(conf.GetBool("bool.key"));
    EXPECT_EQ(conf.GetLong("long.key"), 20L);
    EXPECT_EQ(conf.GetInt("none", 9), 9);

    auto errors = conf.Validate(1);
    EXPECT_TRUE(errors.empty());
    EXPECT_FALSE(conf.Validate(99).empty());

    KVReader reader;
    conf.Dump(reader);
    EXPECT_GT(reader.Size(), 0U);

    ConfigurationPtr other(new (std::nothrow) UnitConfiguration());
    other->Set("int.key", 2);
    EXPECT_TRUE(conf.MergeConf(other, false));

    KVReader fileReader;
    EXPECT_FALSE(Configuration::ReadConf("./missing_config_file", fileReader));
}

static ConfigurationPtr CreateBioConfForAutoConfig()
{
    ConfigurationPtr conf(new (std::nothrow) BioConfig());
    static_cast<BioConfig *>(conf.Get())->LoadDefaultConf();
    conf->Set(NET_TLS_ENABLE_SWITCH.first, std::string("false"));
    conf->Set(DISK_CONF_PATH.first, std::string(""));
    conf->Set(MEM_CAPACITY_SIZE_GB.first, 0);
    conf->Set(NET_DATA_PROTOCOL.first, std::string("tcp"));
    conf->Set(WORK_SCENE.first, std::string("none"));
    return conf;
}

TEST_F(TestCommon, test_bio_config_auto_config_paths)
{
    auto conf = CreateBioConfForAutoConfig();
    BioConfig cfg;

    EXPECT_EQ(cfg.AutoConfigNet(conf), BIO_OK);
    EXPECT_EQ(cfg.GetNetConfig().dataIp, "127.0.0.1");
    EXPECT_EQ(cfg.GetNetConfig().protocol, 1U);

    conf->Set(NET_DATA_PROTOCOL.first, std::string("rdma"));
    EXPECT_EQ(cfg.AutoConfigNet(conf), BIO_OK);
    EXPECT_EQ(cfg.GetNetConfig().protocol, 0U);

    conf->Set(NET_DATA_PROTOCOL.first, std::string("invalid"));
    EXPECT_EQ(cfg.AutoConfigNet(conf), BIO_OK);
    EXPECT_EQ(cfg.GetNetConfig().protocol, NO_255);

    conf->Set(NET_DATA_IP_MASK.first, std::string("255.255.255.255/32"));
    EXPECT_EQ(cfg.AutoConfigNet(conf), BIO_ERR);
    conf->Set(NET_DATA_IP_MASK.first, std::string("127.0.0.1/24"));

    EXPECT_EQ(cfg.AutoConfigCm(conf), BIO_OK);
    EXPECT_EQ(cfg.GetCmConfig().ptNum, CM_PT_NUM.second);
    EXPECT_EQ(cfg.AutoConfigClient(conf), BIO_OK);
    EXPECT_EQ(cfg.GetClientConfig().localMrSize, NO_512 * 1024UL * 1024UL);

    EXPECT_EQ(cfg.AutoConfigUnderFs(conf), BIO_OK);
    EXPECT_EQ(cfg.GetUnderFsConfig().cephConfig.pools.size(), 2UL);

    EXPECT_EQ(cfg.AutoConfigDaemonLogAndOther(conf), BIO_OK);
    EXPECT_EQ(cfg.GetDaemonConfig().workScene, 0U);
    conf->Set(WORK_SCENE.first, std::string("bigdata"));
    EXPECT_EQ(cfg.AutoConfigDaemonLogAndOther(conf), BIO_OK);
    EXPECT_EQ(cfg.GetDaemonConfig().workScene, 1U);
    conf->Set(WORK_SCENE.first, std::string("bad"));
    EXPECT_EQ(cfg.AutoConfigDaemonLogAndOther(conf), BIO_INVALID_PARAM);
    conf->Set(WORK_SCENE.first, std::string("none"));

    conf->Set(LOG_LEVEL.first, std::string("trace"));
    EXPECT_EQ(cfg.AutoConfigDaemonLogAndOther(conf), BIO_OK);
    conf->Set(LOG_LEVEL.first, std::string("debug"));
    EXPECT_EQ(cfg.AutoConfigDaemonLogAndOther(conf), BIO_OK);
    conf->Set(LOG_LEVEL.first, std::string("warn"));
    EXPECT_EQ(cfg.AutoConfigDaemonLogAndOther(conf), BIO_OK);
    conf->Set(LOG_LEVEL.first, std::string("error"));
    EXPECT_EQ(cfg.AutoConfigDaemonLogAndOther(conf), BIO_OK);
    conf->Set(LOG_LEVEL.first, std::string("bad"));
    EXPECT_EQ(cfg.AutoConfigDaemonLogAndOther(conf), BIO_ERR);
    conf->Set(LOG_LEVEL.first, std::string("info"));

    EXPECT_EQ(cfg.AutoConfigDaemonCache(conf), BIO_OK);
    EXPECT_EQ(cfg.GetDaemonConfig().memCap, 0UL);
    EXPECT_EQ(cfg.AutoConfigDaemonDisk(conf), BIO_OK);
    EXPECT_EQ(cfg.AutoConfigDaemon(conf), BIO_OK);
    EXPECT_EQ(cfg.AutoConfAfterLoadFromFile(conf), BIO_OK);
}

TEST_F(TestCommon, test_bio_config_file_and_modify_paths)
{
    BioConfig cfg;
    const std::string base = "./bio_config_ut";
    const std::string oldFile = base + "/old.conf";
    const std::string newFile = base + "/new.conf";
    FileUtil::RemoveDirRecursive(base);
    EXPECT_TRUE(FileUtil::MakeDirRecursive(base, 0755));

    std::vector<std::string> lines = {"bio.disk.path=/disk0", "bio.log.level=info"};
    EXPECT_TRUE(FileUtil::WriteFile(oldFile, lines));
    EXPECT_EQ(cfg.AddDiskPath("/disk1", oldFile), BIO_OK);

    std::vector<std::string> readLines;
    EXPECT_TRUE(FileUtil::ReadFile(oldFile, readLines));
    EXPECT_NE(readLines[0].find("/disk1"), std::string::npos);
    EXPECT_EQ(cfg.AddDiskPath("/disk1", base + "/missing.conf"), BIO_INNER_ERR);

    EXPECT_TRUE(FileUtil::WriteFile(newFile, lines));
    EXPECT_EQ(cfg.ReplaceFile(oldFile, newFile), BIO_OK);
    EXPECT_EQ(cfg.ReplaceFile(oldFile + ".missing", newFile), BIO_INNER_ERR);

    std::string diskPath = base;
    cfg.ResizeDaemonConfigDisks(diskPath);
    uint32_t diskId = NO_255;
    EXPECT_TRUE(cfg.CheckDiskIsExist(diskPath, diskId));
    EXPECT_EQ(diskId, 0U);
    std::string missingDisk = base + "/none";
    EXPECT_FALSE(cfg.CheckDiskIsExist(missingDisk, diskId));

    EXPECT_EQ(cfg.ModifyConfigEvictWaterLevel(0, NO_30), RCACHE_EVICT_WATER_LEVEL.second);
    EXPECT_EQ(cfg.ModifyConfigEvictWaterLevel(1, 40), RCACHE_EVICT_WATER_LEVEL.second);
    EXPECT_EQ(cfg.ModifyConfigMemReadWriteRatio("3:7"), MEM_READ_WRITE_RATIO.second);
    EXPECT_EQ(cfg.ModifyConfigDiskReadWriteRatio("2:8"), DISK_READ_WRITE_RATIO.second);

    cfg.BakFileProcess("./");
    FileUtil::RemoveDirRecursive(base);
}

TEST_F(TestCommon, test_validator_error_paths)
{
    std::string errMsg;
    EXPECT_TRUE(ValidateRatios("ratio", "4:6", errMsg));
    EXPECT_FALSE(ValidateRatios("ratio", "", errMsg));
    EXPECT_FALSE(ValidateRatios("ratio", "4", errMsg));
    EXPECT_FALSE(ValidateRatios("ratio", "a:6", errMsg));
    EXPECT_FALSE(ValidateRatios("ratio", "11:0", errMsg));
    EXPECT_FALSE(ValidateRatios("ratio", "3:6", errMsg));

    auto range = VStrRange::Create("range", 1, 10);
    EXPECT_TRUE(range->Initialize());
    EXPECT_TRUE(range->Validate("1~10"));
    EXPECT_FALSE(range->Validate("1"));
    EXPECT_FALSE(range->Validate("~10"));
    EXPECT_FALSE(range->Validate("a~10"));
    EXPECT_FALSE(range->Validate("0~11"));
    EXPECT_FALSE(VStrRange::Create("bad.range", 10, 1)->Initialize());

    auto array = VStrArray::Create("array", 1, 5, 2);
    EXPECT_TRUE(array->Initialize());
    EXPECT_TRUE(array->Validate("1,5"));
    EXPECT_FALSE(array->Validate("1"));
    EXPECT_FALSE(array->Validate("1,a"));
    EXPECT_FALSE(array->Validate("1,6"));

    auto ip = VIpv4Validator::Create("ip");
    EXPECT_TRUE(ip->Validate("127.0.0.1"));
    EXPECT_FALSE(ip->Validate("127.0.0"));
    EXPECT_FALSE(ip->Validate("127.0.0.a"));
    EXPECT_FALSE(ip->Validate("127.0.0.256"));

    auto portList = VIpv4PortListValidator::Create("port.list");
    EXPECT_TRUE(portList->Validate("127.0.0.1:2181,127.0.0.1:2182"));
    EXPECT_FALSE(portList->Validate(""));
    EXPECT_FALSE(portList->Validate("127.0.0.1"));

    auto perm = VFilePermissionValidator::Create("perm", true);
    EXPECT_TRUE(perm->Validate("755"));
    EXPECT_FALSE(perm->Validate(""));
    EXPECT_FALSE(perm->Validate("75"));
    EXPECT_FALSE(perm->Validate("788"));
    EXPECT_FALSE(perm->Validate("055"));
    EXPECT_FALSE(perm->Validate("475"));

    auto realPath = VStrRealPath::Create("path");
    EXPECT_TRUE(realPath->Validate("."));
    EXPECT_FALSE(realPath->Validate(""));
    EXPECT_FALSE(realPath->Validate("./not_exist_path_for_validator"));

    auto cephPool = VStrCephPool::Create("pool");
    EXPECT_TRUE(cephPool->Validate("0:pool0,1:pool1"));
    EXPECT_FALSE(cephPool->Validate(""));
    EXPECT_FALSE(cephPool->Validate("bad"));
    EXPECT_FALSE(cephPool->Validate("-1:pool"));
    EXPECT_TRUE(VStrBoolRange::Create("bool")->Validate("true"));
    EXPECT_FALSE(VStrBoolRange::Create("bool")->Validate("yes"));
}
