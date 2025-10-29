/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include "test_common.h"
#include "bio_str_util.h"
#include "bio_functions.h"
#include "message.h"
#include "bio_file_util.h"
#include "bio_log.h"
#include "bio_cryptor_helper.h"
#include "bio_crc_util.h"
#include "bio_double_list.h"
#include "bio_functions.h"
#include "bio_ip_util.h"
#include "bio_log.h"
#include "gmock/gmock.h"
#include "bio_ring_buffer.h"
#include "bio_cryptor_helper.h"
#include "tracepoint.h"
#include "bio_config_instance.h"

using namespace ock::bio;

bool TestCommon::gSetup = false;
constexpr uint32_t NO_0 = 0;
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

TEST_F(TestCommon, test_decrypt_return_fail)
{
    LOG_INFO("test_decrypt_return_fail");
    BioCryptorHelper *mbioCryptorHelper = new (std::nothrow) BioCryptorHelper("bio.log", "bio.log");
    std::string path = "bio.log";
    std::pair<char *, int> result;
    auto ret = mbioCryptorHelper->Decrypt(1, path, result);
    EXPECT_EQ(ret, -1);

    path = "bio1.log";
    ret = mbioCryptorHelper->Decrypt(1, path, result);
    EXPECT_EQ(ret, -1);

    path = "libbio_sdk.so";
    ret = mbioCryptorHelper->Decrypt(1, path, result);
    EXPECT_EQ(ret, -1);
}

TEST_F(TestCommon, test_copy_key_fail)
{
    LOG_INFO("test_copy_key_fail");
    CopyKey(nullptr, nullptr, KEY_MAX_SIZE);

    auto ret = StrUtil::StartWith("123.5", "1");
    EXPECT_EQ(ret, true);
}

constexpr uint32_t DEFAULT_CHECK_CRC = 2591144780;
TEST_F(TestCommon, test_crc32_calculation)
{
    LOG_INFO("test_crc32_calculation");
    const char testData[] = "hello";
    uint32_t dataLen = sizeof(testData) - 1;
    uint32_t result = BioCrcUtil::Crc32(testData, dataLen);
    EXPECT_EQ(result, DEFAULT_CHECK_CRC);
}

TEST_F(TestCommon, test_crc32_with_soft_crc)
{
    LOG_INFO("test_crc32_with_soft_crc");
    const char testData[] = "hello";
    uint32_t dataLen = sizeof(testData) - 1;
    uint32_t result = BioCrcUtil::SoftCrc32(testData, dataLen);
    EXPECT_EQ(result, DEFAULT_CHECK_CRC);
}

struct Node {
    Node* next[1];
    Node* prev[1];
    int data;
};

// InsertAt
TEST_F(TestCommon, node_nullptr)
{
    LOG_INFO("node_nullptr");
    Node* node = nullptr;
    BioDoubleList<Node*>* list = new BioDoubleList<Node*>();
    EXPECT_FALSE(list->InsertAt(nullptr, node));
    delete list;
}

TEST_F(TestCommon, insert_at_empty_list)
{
    LOG_INFO("insert_at_empty_list");
    BioDoubleList<Node*>* list = new BioDoubleList<Node*>();
    Node* node = new Node();
    node->data = NO_10;
    EXPECT_TRUE(list->InsertAt(nullptr, node));
    delete list;
}

TEST_F(TestCommon, insert_at_head)
{
    LOG_INFO("insert_at_head");
    BioDoubleList<Node*>* list = new BioDoubleList<Node*>();
    Node* node1 = new Node();
    node1->data = NO_10;
    list->InsertAt(nullptr, node1);
    Node* node2 = new Node();
    node2->data = NO_20;
    EXPECT_TRUE(list->InsertAt(node1, node2));
    delete list;
}

TEST_F(TestCommon, insert_at_middle)
{
    LOG_INFO("insert_at_middle");
    BioDoubleList<Node*>* list = new BioDoubleList<Node*>();
    Node* node1 = new Node();
    node1->data = NO_10;
    Node* node2 = new Node();
    node2->data = NO_20;
    Node* node3 = new Node();
    node3->data = NO_30;

    list->InsertAt(nullptr, node1);
    list->InsertAt(node1, node2);

    EXPECT_TRUE(list->InsertAt(node1, node3));
    delete list;
}

// Remove
TEST_F(TestCommon, remove_head_node)
{
    LOG_INFO("remove_head_node");
    BioDoubleList<Node*>* list = new BioDoubleList<Node*>();
    Node* node1 = new Node();
    node1->data = NO_10;
    Node* node2 = new Node();
    node2->data = NO_20;
    list->PushBack(node1);
    list->PushBack(node2);

    // 现在头节点是 node1，移除头节点
    EXPECT_TRUE(list->Remove(node1));
    EXPECT_FALSE(list->IsEmpty());
    EXPECT_EQ(list->Begin(), node2);
    EXPECT_EQ(list->End(), node2);
    delete list;
}

TEST_F(TestCommon, remove_tail_node)
{
    LOG_INFO("remove_tail_node");
    BioDoubleList<Node*>* list = new BioDoubleList<Node*>();
    Node* node1 = new Node();
    node1->data = NO_10;
    Node* node2 = new Node();
    node2->data = NO_20;
    list->PushBack(node1);
    list->PushBack(node2);

    // 移除尾节点
    EXPECT_TRUE(list->Remove(node2));
    EXPECT_EQ(list->End(), node1);
    delete list;
}

TEST_F(TestCommon, remove_middle_node)
{
    LOG_INFO("remove_middle_node");
    BioDoubleList<Node*>* list = new BioDoubleList<Node*>();
    Node* node1 = new Node();
    node1->data = NO_10;
    Node* node2 = new Node();
    node2->data = NO_20;
    Node* node3 = new Node();
    node3->data = NO_30;
    list->PushBack(node1);
    list->PushBack(node2);
    list->PushBack(node3);

    // 移除中间节点
    EXPECT_TRUE(list->Remove(node2));
    EXPECT_EQ(list->Begin(), node1);
    EXPECT_EQ(list->End(), node3);
    delete list;
}

// 测试 PushBack 函数
TEST_F(TestCommon, push_back_empty_list)
{
    LOG_INFO("push_back_empty_list");
    BioDoubleList<Node*>* list = new BioDoubleList<Node*>();
    Node* node = new Node();
    node->data = NO_10;

    EXPECT_TRUE(list->PushBack(node));
    EXPECT_EQ(list->Begin(), node);
    EXPECT_EQ(list->End(), node);
    delete list;
}

TEST_F(TestCommon, push_back_non_empty_list)
{
    LOG_INFO("push_back_non_empty_list");
    BioDoubleList<Node*>* list = new BioDoubleList<Node*>();
    Node* node1 = new Node();
    node1->data = NO_10;
    Node* node2 = new Node();
    node2->data = NO_20;

    list->PushBack(node1);
    EXPECT_TRUE(list->PushBack(node2));

    // 检查节点的插入
    EXPECT_EQ(list->Begin(), node1);
    EXPECT_EQ(list->End(), node2);
    delete list;
}

TEST_F(TestCommon, push_back_null_node)
{
    LOG_INFO("push_back_null_node");
    BioDoubleList<Node*>* list = new BioDoubleList<Node*>();
    Node* node = nullptr;
    EXPECT_FALSE(list->PushBack(node));
    delete list;
}

// CanonicalPath
TEST_F(TestCommon, canonical_path_path_too_long)
{
    LOG_INFO("canonical_path_path_too_long");
    std::string path(NO_4097, 'a');
    EXPECT_FALSE(FileUtil::CanonicalPath(path));
}

TEST_F(TestCommon, canonical_path_invalid_path)
{
    LOG_INFO("canonical_path_invalid_path");
    std::string path = "/invalid/path/to/file";
    EXPECT_FALSE(FileUtil::CanonicalPath(path));
}

// 测试 ValidateRatios 函数
TEST_F(TestCommon, test_empty_value)
{
    LOG_INFO("test_empty_value");
    std::string errMsg;
    EXPECT_FALSE(ValidateRatios("test", "", errMsg));
    EXPECT_EQ(errMsg, "Invalid value for <test>, it should not be empty");
}

TEST_F(TestCommon, test_invalid_format)
{
    LOG_INFO("test_invalid_format");
    std::string errMsg;
    EXPECT_FALSE(ValidateRatios("test", "4,6", errMsg));
    EXPECT_EQ(errMsg, "Invalid value for <test>, it should like 4:6");

    EXPECT_FALSE(ValidateRatios("test", "4", errMsg));
    EXPECT_EQ(errMsg, "Invalid value for <test>, it should like 4:6");

    EXPECT_FALSE(ValidateRatios("test", "4:6:8", errMsg));
    EXPECT_EQ(errMsg, "Invalid value for <test>, it should like 4:6");
}

TEST_F(TestCommon, test_invalid_number)
{
    LOG_INFO("test_invalid_number");
    std::string errMsg;
    EXPECT_FALSE(ValidateRatios("test", "4:abc", errMsg));
    EXPECT_EQ(errMsg, "Invalid value for <test>, it should like 4:6");

    EXPECT_FALSE(ValidateRatios("test", "x:y", errMsg));
    EXPECT_EQ(errMsg, "Invalid value for <test>, it should like 4:6");
}

TEST_F(TestCommon, test_out_of_range_number)
{
    LOG_INFO("test_out_of_range_number");
    std::string errMsg;
    EXPECT_FALSE(ValidateRatios("test", "11:0", errMsg));
    EXPECT_EQ(errMsg, "Invalid value for <test>, ratio should be in range 0 to 10");

    EXPECT_FALSE(ValidateRatios("test", "0:11", errMsg));
    EXPECT_EQ(errMsg, "Invalid value for <test>, ratio should be in range 0 to 10");
}

TEST_F(TestCommon, test_invalid_sum)
{
    LOG_INFO("test_invalid_sum");
    std::string errMsg;
    EXPECT_FALSE(ValidateRatios("test", "5:6", errMsg));
    EXPECT_EQ(errMsg, "Invalid value for <test>, sum of ratios must equal 10");

    EXPECT_FALSE(ValidateRatios("test", "1:2", errMsg));
    EXPECT_EQ(errMsg, "Invalid value for <test>, sum of ratios must equal 10");
}

TEST_F(TestCommon, test_valid_input)
{
    LOG_INFO("test_valid_input");
    std::string errMsg;
    EXPECT_TRUE(ValidateRatios("test", "4:6", errMsg));
    EXPECT_TRUE(ValidateRatios("test", "10:0", errMsg));
    EXPECT_TRUE(ValidateRatios("test", "0:10", errMsg));
}

// IpMaskToAddress
TEST_F(TestCommon, ip_mask_to_address_invalid_format)
{
    LOG_INFO("ip_mask_to_address_invalid_format");
    in_addr_t ipByMask;
    in_addr_t mask;
    EXPECT_FALSE(IpUtil::IpMaskToAddress("192.168.0.1", ipByMask, mask));
    EXPECT_FALSE(IpUtil::IpMaskToAddress("192.168.0.1/24/32", ipByMask, mask));
}

TEST_F(TestCommon, ip_mask_to_address_invalid_mask_width)
{
    LOG_INFO("ip_mask_to_address_invalid_mask_width");
    in_addr_t ipByMask;
    in_addr_t mask;
    EXPECT_FALSE(IpUtil::IpMaskToAddress("192.168.0.1/abc", ipByMask, mask));
    EXPECT_FALSE(IpUtil::IpMaskToAddress("192.168.0.1/33", ipByMask, mask));
}

TEST_F(TestCommon, ip_mask_to_address_invalid_ip)
{
    LOG_INFO("ip_mask_to_address_invalid_ip");
    in_addr_t ipByMask;
    in_addr_t mask;
    EXPECT_FALSE(IpUtil::IpMaskToAddress("999.999.999.999/24", ipByMask, mask));
}

// FilterIpByMask
TEST_F(TestCommon, filter_ip_by_mask_ip_mask_to_address_failed)
{
    LOG_INFO("filter_ip_by_mask_ip_mask_to_address_failed");
    std::vector<std::string> outIps;
    EXPECT_FALSE(IpUtil::FilterIpByMask("invalid/mask", outIps));
}

TEST_F(TestCommon, filter_ip_by_mask_get_if_addrs_failed)
{
    LOG_INFO("filter_ip_by_mask_get_if_addrs_failed");
    std::vector<std::string> outIps;
    EXPECT_TRUE(IpUtil::FilterIpByMask("192.168.0.0/24", outIps));
}

TEST_F(TestCommon, filter_ip_by_mask_no_matching_ip)
{
    LOG_INFO("filter_ip_by_mask_no_matching_ip");
    std::vector<std::string> outIps;
    EXPECT_TRUE(IpUtil::FilterIpByMask("192.168.1.1/24", outIps));
    EXPECT_TRUE(outIps.empty());
}

TEST_F(TestCommon, filter_ip_by_mask_matching_ips)
{
    LOG_INFO("filter_ip_by_mask_matching_ips");
    std::vector<std::string> outIps;
    EXPECT_TRUE(IpUtil::FilterIpByMask("192.168.0.0/24", outIps));
    EXPECT_EQ(outIps.size(), 0);
}

TEST_F(TestCommon, instance_invalid_min_log_level)
{
    LOG_INFO("instance_invalid_min_log_level");
    LoggerOptions options;
    options.logType = NO_1;
    options.minLogLevel = -1;
    options.rotationFileSizeInMB = NO_50;
    options.rotationFileCount = NO_10;
    options.path = "./bio.log";

    Logger* logger = Logger::Instance(options);
    EXPECT_NE(logger, nullptr);
}

TEST_F(TestCommon, instance_empty_path)
{
    LOG_INFO("instance_empty_path");
    LoggerOptions options;
    options.logType = NO_1;
    options.minLogLevel = NO_2;
    options.rotationFileSizeInMB = NO_50;
    options.rotationFileCount = NO_10;
    options.path = "";

    Logger* logger = Logger::Instance(options);
    EXPECT_NE(logger, nullptr);
}

TEST_F(TestCommon, instance_invalid_rotation_file_size)
{
    LOG_INFO("instance_invalid_rotation_file_size");
    LoggerOptions options;
    options.logType = NO_1;
    options.minLogLevel = NO_2;
    options.rotationFileSizeInMB = NO_128;
    options.rotationFileCount = NO_10;
    options.path = "./bio.log";

    Logger* logger = Logger::Instance(options);
    EXPECT_NE(logger, nullptr);
}

TEST_F(TestCommon, instance_invalid_rotation_file_count)
{
    LOG_INFO("instance_invalid_rotation_file_count");
    LoggerOptions options;
    options.logType = NO_1;
    options.minLogLevel = NO_2;
    options.rotationFileSizeInMB = NO_50;
    options.rotationFileCount = NO_128;
    options.path = "./bio.log";

    Logger* logger = Logger::Instance(options);
    EXPECT_NE(logger, nullptr);
}

TEST_F(TestCommon, instance_valid_options)
{
    LOG_INFO("instance_valid_options");
    LoggerOptions options;
    options.logType = NO_1;
    options.minLogLevel = NO_2;
    options.rotationFileSizeInMB = NO_50;
    options.rotationFileCount = NO_10;
    options.path = "./bio.log";

    Logger* logger = Logger::Instance(options);
    EXPECT_NE(logger, nullptr);
}

TEST_F(TestCommon, instance_invalid_log_type)
{
    LOG_INFO("instance_invalid_log_type");
    LoggerOptions options;
    options.logType = 0;
    options.minLogLevel = NO_2;
    options.rotationFileSizeInMB = NO_50;
    options.rotationFileCount = NO_10;
    options.path = "./bio.log";

    Logger* logger = Logger::Instance(options);
    EXPECT_NE(logger, nullptr);
}

// ResetLogLevel
TEST_F(TestCommon, reset_log_level)
{
    LOG_INFO("reset_log_level");
    LoggerOptions options;
    options.logType = 0;
    options.minLogLevel = NO_2;
    options.rotationFileSizeInMB = NO_50;
    options.rotationFileCount = NO_10;
    options.path = "./bio.log";

    Logger* logger = Logger::Instance(options);
    logger->ResetLogLevel(NO_2);
}

TEST_F(TestCommon, reset_log_level_nullptr)
{
    LOG_INFO("reset_log_level_nullptr");
    LoggerOptions options;
    options.logType = 0;
    options.minLogLevel = NO_2;
    options.rotationFileSizeInMB = NO_50;
    options.rotationFileCount = NO_10;
    options.path = "./bio.log";

    Logger* logger = Logger::Instance(options);
    logger->ResetLogLevel(NO_3);
}

// StrToFloat
TEST_F(TestCommon, strtofloat_invalid_input)
{
    LOG_INFO("strtofloat_invalid_input");
    std::string input = "abc";
    float value;
    EXPECT_FALSE(StrUtil::StrToFloat(input, value));
}

TEST_F(TestCommon, strtofloat_remaining_chars)
{
    LOG_INFO("strtofloat_remaining_chars");
    std::string input = "0.0abc";
    float value;
    EXPECT_FALSE(StrUtil::StrToFloat(input, value));
}

TEST_F(TestCommon, bio_ring_buffer_init)
{
    LOG_INFO("bio_ring_buffer_init");
    RingBuffer<int> buffer(0);
    auto ret = buffer.Initialize();
    EXPECT_EQ(ret, -1);
}

TEST_F(TestCommon, bio_cryptor_decrypt_initilize_error)
{
    LOG_INFO("bio_cryptor_decrypt_error");
    std::string masterPath("masterPath");
    std::string standbyPath("standbyPath");
    auto hseCryptorHelper = new (std::nothrow) BioCryptorHelper(masterPath, standbyPath);

    std::ofstream outFile("passwordPath");
    outFile.is_open();
    std::string content = "password";
    outFile << content << std::endl; // 写入内容并换行
    outFile.close();

    std::pair<char *, int> data;
    int ret = hseCryptorHelper->Decrypt(NO_1, "passwordPath", data);
    delete hseCryptorHelper;
    EXPECT_EQ(ret, -1);
}

TEST_F(TestCommon, bio_cryptor_decrypt_refresh_error)
{
    LOG_INFO("bio_cryptor_decrypt_error");
    std::string masterPath("masterPath");
    std::string standbyPath("standbyPath");
    auto hseCryptorHelper = new (std::nothrow) BioCryptorHelper(masterPath, standbyPath);

    std::pair<char *, int> data;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "BIO_CRYPTOR_HELPER_UNDO", 0, 1, userParam);
    int ret = hseCryptorHelper->Decrypt(NO_1, "passwordPath", data);
    LVOS_HVS_deactiveTracePoint(0, "BIO_CRYPTOR_HELPER_UNDO");
    delete hseCryptorHelper;
    EXPECT_EQ(ret, -1);
}

TEST_F(TestCommon, bio_cryptor_decrypt_inner_error)
{
    LOG_INFO("bio_cryptor_decrypt_error");
    std::string masterPath("masterPath");
    std::string standbyPath("standbyPath");
    auto hseCryptorHelper = new (std::nothrow) BioCryptorHelper(masterPath, standbyPath);

    std::pair<char *, int> data;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "BIO_CRYPTOR_HELPER_UNDO", 0, NO_2, userParam);
    int ret = hseCryptorHelper->Decrypt(NO_1, "passwordPath", data);
    LVOS_HVS_deactiveTracePoint(0, "BIO_CRYPTOR_HELPER_UNDO");
    delete hseCryptorHelper;
    EXPECT_EQ(ret, -1);
}

TEST_F(TestCommon, bio_cryptor_decrypt_ok)
{
    LOG_INFO("bio_cryptor_decrypt_error");
    std::string masterPath("masterPath");
    std::string standbyPath("standbyPath");
    auto hseCryptorHelper = new (std::nothrow) BioCryptorHelper(masterPath, standbyPath);

    std::pair<char *, int> data;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "BIO_CRYPTOR_HELPER_UNDO", 0, NO_3, userParam);
    int ret = hseCryptorHelper->Decrypt(NO_1, "passwordPath", data);
    LVOS_HVS_deactiveTracePoint(0, "BIO_CRYPTOR_HELPER_UNDO");
    delete hseCryptorHelper;
    EXPECT_EQ(ret, 0);
}

TEST_F(TestCommon, bio_cryptor_check_key_error)
{
    LOG_INFO("bio_cryptor_decrypt_error");
    std::string masterPath("masterPath");
    std::string standbyPath("standbyPath");
    auto hseCryptorHelper = new (std::nothrow) BioCryptorHelper(masterPath, standbyPath);

    bool flag = true;
    int ret = hseCryptorHelper->CheckMasterKeyExpired(NO_1, flag, NO_1);
    delete hseCryptorHelper;
    EXPECT_EQ(ret, -1);
}

TEST_F(TestCommon, bio_cryptor_erase_data_error)
{
    LOG_INFO("bio_cryptor_decrypt_error");
    std::string masterPath("masterPath");
    std::string standbyPath("standbyPath");
    auto hseCryptorHelper = new(std::nothrow) BioCryptorHelper(masterPath, standbyPath);

    std::pair<char *, int> data;
    const char *str = "test";
    data.first = new char[strlen(str) + 1];

    int32_t ret = strcpy_s(data.first, strlen(str) + 1, str);
    EXPECT_EQ(ret, 0);
    data.second = NO_2;
    hseCryptorHelper->EraseDecryptData(data);
    delete hseCryptorHelper;
}

TEST_F(TestCommon, bio_config_validator_vstr_enum)
{
    LOG_INFO("bio_config_validator_vstr_enum");
    std::string name = "VStrEnum";
    std::string enumStr = "";
    ValidatorPtr validatorPtr = VStrEnum::Create(name, enumStr);
    auto ret = validatorPtr->Initialize();
    EXPECT_EQ(ret, false);

    std::string value = "||tcp";
    ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);

    value = "tcp";
    ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);
}

TEST_F(TestCommon, bio_config_validator_vstr_not_null)
{
    LOG_INFO("bio_config_validator_vstr_not_null");
    ValidatorPtr validatorPtr = VStrNotNull::Create("VStrNotNull");
    std::string value = "";
    auto ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);
}

TEST_F(TestCommon, bio_config_validator_vint_range)
{
    LOG_INFO("bio_config_validator_vint_range");
    std::string name = "VIntRange";
    int start = NO_1024;
    int end = NO_128;
    ValidatorPtr validatorPtr = VIntRange::Create(name, start, end);
    auto ret = validatorPtr->Initialize();
    EXPECT_EQ(ret, false);

    int value = NO_128;
    ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);

    start = NO_1;
    end = INT32_MAX;
    ValidatorPtr validatorPtrTwo = VIntRange::Create(name, start, end);
    value = NO_0;
    ret = validatorPtrTwo->Validate(value);
    EXPECT_EQ(ret, false);
}

TEST_F(TestCommon, bio_config_validator_vip4_mask)
{
    LOG_INFO("bio_config_validator_vip4_mask");
    std::string name = "VIpv4Mask";
    ValidatorPtr validatorPtr = VIpv4MaskValidator::Create(name, false);
    std::string value = "";
    auto ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);

    value = "10.175.118.6";
    ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);

    value = "10.175.118.6/255";
    ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);

    value = "10.175.118/22";
    ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);

    value = "10.175.275.6/22";
    ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);

    value = "10.175.118.6/22";
    ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, true);
}

TEST_F(TestCommon, bio_config_validator_vstr_ceph_pool)
{
    LOG_INFO("bio_config_validator_vstr_ceph_pool");
    std::string name = "VStrCephPool";
    ValidatorPtr validatorPtr = VStrCephPool::Create(name);
    std::string value = "";
    auto ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);

    value = "0,1:jfspool1";
    ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);
}

TEST_F(TestCommon, bio_config_validator_vstr_bool_range)
{
    LOG_INFO("bio_config_validator_vstr_bool_range");
    std::string name = "VStrBoolRange";
    ValidatorPtr validatorPtr = VStrBoolRange::Create(name);
    std::string value = "bool";
    auto ret = validatorPtr->Validate(value);
    EXPECT_EQ(ret, false);
}

TEST_F(TestCommon, bio_config_validator_error_message)
{
    LOG_INFO("bio_config_validator");
    std::string name = "VStrBoolRange";
    ValidatorPtr validatorPtr = VStrBoolRange::Create(name);
    auto ret = validatorPtr->ErrorMessage();
    EXPECT_EQ(ret, "");
}

TEST_F(TestCommon, bio_config_validator_virtual)
{
    LOG_INFO("bio_config_validator_virtual");
    std::string name = "VStrBoolRange";
    ValidatorPtr validatorPtr = VStrBoolRange::Create(name);
    int valueInt = NO_1;
    auto ret = validatorPtr->Validate(valueInt);
    EXPECT_EQ(ret, true);

    float valueFloat = 0.0f;
    ret = validatorPtr->Validate(valueFloat);
    EXPECT_EQ(ret, true);

    long valueLong = 1L;
    ret = validatorPtr->Validate(valueLong);
    EXPECT_EQ(ret, true);
}

TEST_F(TestCommon, bio_config_modify_config_evict_water_level)
{
    LOG_INFO("bio_config_modify_config_evict_water_level");
    uint8_t tier = NO_1;
    uint64_t level = NO_50;
    auto ret = BioConfig::Instance()->ModifyConfigEvictWaterLevel(tier, level);
    EXPECT_EQ(ret, NO_90);

    tier = NO_1;
    ret = BioConfig::Instance()->ModifyConfigEvictWaterLevel(tier, level);
    EXPECT_EQ(ret, NO_50);
}

TEST_F(TestCommon, bio_config_modify_config_mem_read_write_ratio)
{
    LOG_INFO("bio_config_modify_config_mem_read_write_ratio");
    std::string ratio = "5:5";
    auto ret = BioConfig::Instance()->ModifyConfigMemReadWriteRatio(ratio);
    EXPECT_EQ(ret, "5:5");
}

TEST_F(TestCommon, bio_config_modify_config_disk_read_write_ratio)
{
    LOG_INFO("bio_config_modify_config_disk_read_write_ratio");
    std::string ratio = "5:5";
    auto ret = BioConfig::Instance()->ModifyConfigDiskReadWriteRatio(ratio);
    EXPECT_EQ(ret, "5:5");
}

TEST_F(TestCommon, bio_config_kv_reader_from_file)
{
    LOG_INFO("bio_config_kv_reader_from_file");
    std::string filePath = "invalid_path";
    KVReader kv;
    auto ret = kv.FromFile(filePath);
    EXPECT_EQ(ret, false);
}

TEST_F(TestCommon, bio_config_kv_reader_get_i)
{
    LOG_INFO("bio_config_kv_reader_get_i");
    uint32_t index = NO_1024;
    std::string outKey;
    std::string outValue;
    KVReader kv;
    kv.GetI(index, outKey, outValue);
}

TEST_F(TestCommon, bio_config_read_conf_file_not_exist)
{
    LOG_INFO("bio_config_read_conf_file_not_exist");
    std::string path = "invalid_path";
    auto ret = Configuration::GetInstance<BioConfig>()->ReadConf<BioConfig>(path);
    EXPECT_EQ(ret, false);
}

TEST_F(TestCommon, bio_config_get_int_default_value)
{
    LOG_INFO("bio_config_read_conf_file_not_exist");
    ConfigurationPtr conf = Configuration::GetInstance<BioConfig>();
    std::string key = "default";
    int32_t defaultValue = NO_1;
    auto ret = conf->GetInt(key, defaultValue);
    EXPECT_EQ(ret, NO_1);
}

TEST_F(TestCommon, bio_config_get_str_default_value)
{
    LOG_INFO("bio_config_get_str_default_value");
    ConfigurationPtr conf = Configuration::GetInstance<BioConfig>();
    std::string key = "default";
    std::string defaultValue = "value";
    auto ret = conf->GetStr(key, defaultValue);
    EXPECT_EQ(ret, "value");
}

TEST_F(TestCommon, bio_config_add_validator)
{
    LOG_INFO("bio_config_add_validator");
    ConfigurationPtr conf = Configuration::GetInstance<BioConfig>();
    std::string name = "VStrBoolRange";
    ValidatorPtr validatorPtr = VStrBoolRange::Create(name);
    std::string key = "";
    ValidatorTag validatorTag = NO_1;
    conf->AddValidator(key, validatorPtr, validatorTag);
}

TEST_F(TestCommon, bio_config_validate_one_type)
{
    LOG_INFO("bio_config_validate_one_type");
    ConfigurationPtr conf = Configuration::GetInstance<BioConfig>();
    ValidatorTag validatorTag = NO_4096;
    auto errors = conf->Validate(validatorTag);
    EXPECT_NE(errors.size(), 0);

    validatorTag = NO_128;
    std::string key = "test";
    std::string name = "VStrBoolRange";
    ValidatorPtr validatorPtr = VStrBoolRange::Create(name);
    conf->AddValidator(key, validatorPtr, validatorTag);
    errors = conf->Validate(validatorTag);
    EXPECT_NE(errors.size(), 0);

    std::pair<std::string, float> floatPair = std::make_pair("test", 0.0f);
    conf->AddFloatConf(floatPair, validatorPtr, validatorTag);
    errors = conf->Validate(validatorTag);
    EXPECT_EQ(errors.size(), 0);

    std::pair<std::string, long> longPair = std::make_pair("test", 1L);
    conf->AddLongConf(longPair, validatorPtr, validatorTag);
    errors = conf->Validate(validatorTag);
    EXPECT_EQ(errors.size(), 0);
}

TEST_F(TestCommon, bio_config_validate_dump)
{
    LOG_INFO("bio_config_validate_dump");
    ConfigurationPtr conf = Configuration::GetInstance<BioConfig>();
    ValidatorTag validatorTag = NO_10;
    std::string name = "VStrBoolRange";
    ValidatorPtr validatorPtr = VStrBoolRange::Create(name);

    std::pair<std::string, float> floatPair = std::make_pair("test", 0.0f);
    conf->AddFloatConf(floatPair, validatorPtr, validatorTag);
    KVReader kv;
    conf->Dump(kv);

    std::pair<std::string, long> longPair = std::make_pair("test", 1L);
    conf->AddLongConf(longPair, validatorPtr, validatorTag);
    conf->Dump(kv);

    std::pair<std::string, bool> boolPair = std::make_pair("test", true);
    conf->AddBoolConf(boolPair);
    conf->Dump(kv);
}

TEST_F(TestCommon, bio_config_init_bak_file_exit_test)
{
    LOG_INFO("bio_config_init_bak_file_exit_test");
    BioConfigPtr config = BioConfig::Instance();
    std::string homePath = "/opt/boostio/bin";
    std::string confPath = "/opt/boostio/bin/conf/bio.conf";
    std::string initBakPath = "/opt/boostio/bin/conf/bio.conf.bak.init";
    (void)system("touch /opt/boostio/bin/conf/bio.conf.bak.init");
    FileUtil::BackUpFile(confPath, initBakPath);
    config->BakFileProcess(homePath);
    bool exist = FileUtil::Exist(initBakPath);
    EXPECT_EQ(exist, false);
}

TEST_F(TestCommon, bio_config_bak_file_exit_test)
{
    LOG_INFO("bio_config_bak_file_exit_test");
    BioConfigPtr config = BioConfig::Instance();
    std::string homePath = "/opt/boostio/bin";
    std::string confPath = "/opt/boostio/bin/conf/bio.conf";
    std::string bakPath = "/opt/boostio/bin/conf/bio.conf.bak";
    (void)system("touch /opt/boostio/bin/conf/bio.conf.bak");
    FileUtil::BackUpFile(confPath, bakPath);
    config->BakFileProcess(homePath);
    bool exist = FileUtil::Exist(bakPath);
    EXPECT_EQ(exist, false);
}