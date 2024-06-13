/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include "gtest/gtest.h"
#include "message.h"
#include "bio_server.h"
#include "tracepoint.h"
#include "bio_server_c.h"
#include "bdm_obj.h"
#include "test_bio_server.h"

using namespace ock::bio;

bool TestBioServer::gSetup = false;

void TestBioServer::SetUp()
{
    if (gSetup) {
        return;
    }
    gSetup = true;
    return;
}

void TestBioServer::TearDown()
{
    return;
}

TEST_F(TestBioServer, test_bio_server_check_all)
{
    LOG_INFO("test_bio_server_check_all");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    RequestComm reqComm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    auto ret = mirror->CheckAll(reqComm);
    EXPECT_EQ(ret, true);

    reqComm = { 0, 1, 1, 1, getpid() };
    ret = mirror->CheckAll(reqComm);
    EXPECT_EQ(ret, false);

    reqComm = { MESSAGE_MAGIC, 1, NO_2, 1, getpid() };
    ret = mirror->CheckAll(reqComm);
    EXPECT_EQ(ret, false);
}

TEST_F(TestBioServer, test_bio_server_shm_init)
{
    LOG_INFO("test_bio_server_shm_init");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    ShmInitRequest req = { { MESSAGE_MAGIC, 0, 0, INVALID_NID, getpid() } };
    auto ret = mirror->MirrorServerShmInit(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_qry_node_info)
{
    LOG_INFO("test_bio_server_qry_node_info");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    GetLocalNidRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() } };
    auto ret = mirror->MirrorServerQueryNodeInfo(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_qry_node_info_by_pt)
{
    LOG_INFO("test_bio_server_qry_node_info_by_pt");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    FileLocationQueryReq req = { 1, 2 };
    auto ret = mirror->MirrorServerQueryNodeInfoByPt(ctx, &req);
    EXPECT_EQ(ret, BIO_ERR);
}

TEST_F(TestBioServer, test_bio_server_qry_node_view)
{
    LOG_INFO("test_bio_server_qry_node_view");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    QueryNodeViewRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() }, 0 };
    auto ret = mirror->MirrorServerQueryNodeView(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_qry_pt_view)
{
    LOG_INFO("test_bio_server_qry_pt_view");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    QueryPtViewRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() }, 0 };
    auto ret = mirror->MirrorServerQueryPtView(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_put)
{
    LOG_INFO("test_bio_server_put");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcd", KEY_MAX_SIZE);
    req.length = NO_128;
    req.mrKey = 1;
    req.sliceLen = NO_128;
    req.ioStratege = 0;
    req.memFromServer = true;
    req.mrAddress = 0UL;
    req.mrSize = 0;
    auto ret = mirror->MirrorServerPut(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_put_slice_alloc_fail_reply_ok)
{
    LOG_INFO("test_bio_server_put_slice_alloc_fail_reply_ok");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcdslice", KEY_MAX_SIZE);
    req.length = NO_128;
    req.mrKey = 1;
    req.sliceLen = 0;
    req.ioStratege = 0;
    req.memFromServer = true;
    req.mrAddress = 0UL;
    req.mrSize = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "PUT_SLICE_ZERO_ALLOC_FAIL", 0, 1, userParam);
    auto ret = mirror->MirrorServerPut(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "PUT_SLICE_ZERO_ALLOC_FAIL");
}

TEST_F(TestBioServer, test_bio_server_put_slice_normal_alloc_fail_reply_ok)
{
    LOG_INFO("test_bio_server_put_slice_normal_alloc_fail_reply_ok");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcdslice", KEY_MAX_SIZE);
    req.length = NO_128;
    req.mrKey = 1;
    req.sliceLen = NO_128;
    req.ioStratege = 0;
    req.memFromServer = true;
    req.mrAddress = 0ULL;
    req.mrSize = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "PUT_SLICE_NORMAL_ALLOC_FAIL", 0, 1, userParam);
    auto ret = mirror->MirrorServerPut(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "PUT_SLICE_NORMAL_ALLOC_FAIL");
}

TEST_F(TestBioServer, test_bio_server_get)
{
    LOG_INFO("test_bio_server_get");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    GetRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    CopyKey(req.key, "abcd", KEY_MAX_SIZE);
    req.ptId = 1;
    req.offset = 0;
    req.length = NO_128;
    auto ret = mirror->MirrorServerGet(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_get_read_err_reply_return_ok)
{
    LOG_INFO("test_bio_server_get_read_err_reply_return_ok");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    GetRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 0, getpid() };
    CopyKey(req.key, "abcd", KEY_MAX_SIZE);
    req.ptId = 1;
    req.offset = 0;
    req.length = NO_128;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_READ_CALLBACK_FAIL", 0, 1, userParam);
    auto ret = mirror->MirrorServerGet(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_READ_CALLBACK_FAIL");
}

TEST_F(TestBioServer, test_list_list_err_return_fail)
{
    LOG_INFO("test_list_list_err_return_fail");
    ListRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, 1, getpid() };
    CopyKey(req.prefix, "a", KEY_MAX_SIZE);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "LIST_LIST_FAIL", 0, 1, userParam);
    auto ret = List(&req, nullptr);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "LIST_LIST_FAIL");
}

TEST_F(TestBioServer, test_list_malloc_rsp_err_return_fail)
{
    LOG_INFO("test_list_malloc_rsp_err_return_fail");
    ListRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, 1, getpid() };
    CopyKey(req.prefix, "a", KEY_MAX_SIZE);
    ListResponse *listRsp = nullptr;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "LIST_MALLOC_RSP_FAIL", 0, 1, userParam);
    auto ret = List(&req, &listRsp);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
    LVOS_HVS_deactiveTracePoint(0, "LIST_MALLOC_RSP_FAIL");
    delete[] listRsp;
}

TEST_F(TestBioServer, test_put_slice_length_eq_zero_return_fail)
{
    LOG_INFO("test_put_slice_length_eq_zero_return_fail");
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcd1", KEY_MAX_SIZE);
    req.length = NO_128;
    req.mrKey = 1;
    req.sliceLen = 0;
    req.ioStratege = 0;
    req.memFromServer = true;
    req.mrAddress = 0UL;
    req.mrSize = 0;
    PutResponse response;
    auto ret = Put(&req, &response);
    EXPECT_EQ(ret, BIO_INNER_RETRY);
}

TEST_F(TestBioServer, test_put_slice_length_eq_zero_alloc_slice_err_return_fail)
{
    LOG_INFO("test_put_slice_length_eq_zero_alloc_slice_err_return_fail");
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcd1", KEY_MAX_SIZE);
    req.length = NO_128;
    req.mrKey = 1;
    req.sliceLen = 0;
    req.ioStratege = 0;
    req.memFromServer = true;
    req.mrAddress = 0ULL;
    req.mrSize = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "PUT_SLICELEN_ZERO_ALLOC_SLICE_FAIL", 0, 1, userParam);
    PutResponse response;
    auto ret = Put(&req, &response);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
    LVOS_HVS_deactiveTracePoint(0, "PUT_SLICELEN_ZERO_ALLOC_SLICE_FAIL");
}

TEST_F(TestBioServer, test_put_slice_alloc_slice_err_return_fail)
{
    LOG_INFO("test_put_slice_alloc_slice_err_return_fail");
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcd1", KEY_MAX_SIZE);
    req.length = NO_128;
    req.mrKey = 1;
    req.sliceLen = NO_128;
    req.ioStratege = 0;
    req.memFromServer = true;
    req.mrAddress = 0ULL;
    req.mrSize = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "PUT_ALLOC_SLICE_FAIL", 0, 1, userParam);
    PutResponse response;
    auto ret = Put(&req, &response);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
    LVOS_HVS_deactiveTracePoint(0, "PUT_ALLOC_SLICE_FAIL");
}

TEST_F(TestBioServer, test_get_slice_flowid_unexists_return_fail)
{
    LOG_INFO("test_get_slice_flowid_unexists_return_fail");
    GetSliceRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() }, NO_4194304, 0, 1, NO_128 };
    auto ret = GetSlice(&req, nullptr);
    EXPECT_EQ(ret, BIO_INNER_RETRY);
}

TEST_F(TestBioServer, test_bio_server_stat)
{
    LOG_INFO("test_bio_server_stat");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    StatRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    CopyKey(req.key, "abcd", KEY_MAX_SIZE);
    auto ret = mirror->MirrorServerStat(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_list)
{
    LOG_INFO("test_bio_server_list");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    ListRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, 1, getpid() };
    CopyKey(req.prefix, "a", KEY_MAX_SIZE);
    auto ret = mirror->MirrorServerList(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_list_prefix_null_reply_ok)
{
    LOG_INFO("test_bio_server_list_prefix_null_reply_ok");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    ListRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, 1, getpid() };
    req.isListUnderFs = false;
    CopyKey(req.prefix, "a", KEY_MAX_SIZE);
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_INDEX_TABLE_FAIL", 0, 1, userParam);
    auto ret = mirror->MirrorServerList(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_INDEX_TABLE_FAIL");
}

TEST_F(TestBioServer, test_bio_server_list_inner)
{
    LOG_INFO("test_bio_server_list_inner");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    std::unordered_map<std::string, ObjStat> objs;
    mirror->ReplyListResultLocal(ctx, objs);

    ListRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, 1, getpid() };
    CopyKey(req.prefix, "a", KEY_MAX_SIZE);
    mirror->ReplyListResultRemote(ctx, &req, objs);
}

TEST_F(TestBioServer, test_bio_server_list_inner_objs_notempty)
{
    LOG_INFO("test_bio_server_list_inner_objs_notempty");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    ObjStat stat;
    std::unordered_map<std::string, ObjStat> objs;
    objs.emplace("obj1", stat);
    mirror->ReplyListResultLocal(ctx, objs);

    ListRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, 1, getpid() };
    CopyKey(req.prefix, "a", KEY_MAX_SIZE);
    mirror->ReplyListResultRemote(ctx, &req, objs);
}

TEST_F(TestBioServer, test_mirror_master_create_flow_wcache_err)
{
    LOG_INFO("test_mirror_master_create_flow_wcache_err");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    CreateFlowRequest req = { { MESSAGE_MAGIC, 0, 1, 1, getpid() }, 0, 1 };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL", 0, 1, userParam);
    mirror->MirrorServerCreateFlow(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL");
}

TEST_F(TestBioServer, test_slave_create_flow_ok)
{
    LOG_INFO("test_slave_create_flow_ok");
    CreateFlowRequest req = { { MESSAGE_MAGIC, 0, NO_128, NO_10, getpid() }, 1, 1 };
    auto ret = CreateFlowSlave(&req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, destroy_flow_OK)
{
    LOG_INFO("destroy_flow_OK");
    DestroyFlowRequest req = { { MESSAGE_MAGIC, 0, NO_128, NO_10, getpid() }, 1 };
    auto ret = DestroyFlow(&req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, report_hb_ok)
{
    LOG_INFO("report_hb_ok");
    uint64_t curNodeTimes = 0;
    uint64_t curPtTimes = 0;
    auto ret = ReportHb(&curNodeTimes, &curPtTimes);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_mirror_master_create_flow_rcache_alloc_obj_err)
{
    LOG_INFO("test_mirror_master_create_flow_rcache_alloc_obj_err");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    CreateFlowRequest req = { { MESSAGE_MAGIC, 0, 1, 1, getpid() }, 0, 1 };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_FIND", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_ALLOC_OBJ_FAIL", 0, 1, userParam);
    mirror->MirrorServerCreateFlow(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_FIND");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_ALLOC_OBJ_FAIL");
}

TEST_F(TestBioServer, test_mirror_server_destroy_flow_rcache_alloc_obj_err)
{
    LOG_INFO("test_mirror_server_destroy_flow_rcache_alloc_obj_err");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    DestroyFlowRequest req = { { MESSAGE_MAGIC, 0, 1, 1, getpid() }, 0 };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "DESTROY_WCACHE_FAIL", 0, 1, userParam);
    auto ret = mirror->MirrorServerDestroyFlow(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "DESTROY_WCACHE_FAIL");
}

TEST_F(TestBioServer, test_mirror_server_get_offset_synccall_err)
{
    LOG_INFO("test_mirror_server_get_offset_synccall_err");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    uint64_t offset = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SYNCCALL_FAIL", 0, 1, userParam);
    auto ret = mirror->GetFlowGlobEvictOffset(0, 0, offset);
    EXPECT_EQ(ret, BIO_NET_RETRY);
    LVOS_HVS_deactiveTracePoint(0, "SYNCCALL_FAIL");
}

TEST_F(TestBioServer, test_mirror_server_get_offset_synccall_channel_err)
{
    LOG_INFO("test_mirror_server_get_offset_synccall_channel_err");
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    uint64_t offset = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SYNCCALL_CHANNEL_FAIL", 0, 1, userParam);
    auto ret = mirror->GetFlowGlobEvictOffset(0, 0, offset);
    EXPECT_EQ(ret, BIO_NET_RETRY);
    LVOS_HVS_deactiveTracePoint(0, "SYNCCALL_CHANNEL_FAIL");
}
