/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include "gtest/gtest.h"
#include "message.h"
#include "bio_server.h"
#include "tracepoint.h"
#include "bio_server_c.h"
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
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    ShmInitRequest req = { { MESSAGE_MAGIC, 0, 0, INVALID_NID, getpid() } };
    auto ret = mirror->MirrorServerShmInit(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_qry_node_info)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    GetLocalNidRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() } };
    auto ret = mirror->MirrorServerQueryNodeInfo(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_qry_node_info_pt)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    FileLocationQueryReq req = { 1, 2 };
    auto ret = mirror->MirrorServerQueryNodeInfoByPt(ctx, &req);
    EXPECT_EQ(ret, BIO_ERR);
}

TEST_F(TestBioServer, test_bio_server_qry_node_view)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    QueryNodeViewRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() }, 0 };
    auto ret = mirror->MirrorServerQueryNodeView(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_qry_pt_view)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    QueryPtViewRequest req = { { MESSAGE_MAGIC, 0, 0, 0, getpid() }, 0 };
    auto ret = mirror->MirrorServerQueryPtView(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_put)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcd", KEY_MAX_SIZE);
    req.length = 128U;
    req.mrKey = 1;
    req.sliceLen = 128U;
    req.copyFree = false;
    req.memFromServer = true;
    req.mrAddress = 0ULL;
    req.mrSize = 0;
    auto ret = mirror->MirrorServerPut(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_get)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    GetRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    CopyKey(req.key, "abcd", KEY_MAX_SIZE);
    req.ptId = 1;
    req.offset = 0;
    req.length = 128U;
    auto ret = mirror->MirrorServerGet(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_get_read_err_reply_return_ok)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    GetRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    CopyKey(req.key, "abcd", KEY_MAX_SIZE);
    req.ptId = 1;
    req.offset = 0;
    req.length = 128U;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "WCACHE_READ_CALLBACK_FAIL", 0, 1, userParam);
    auto ret = mirror->MirrorServerGet(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "WCACHE_READ_CALLBACK_FAIL");
}

TEST_F(TestBioServer, test_list_list_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "LIST_LIST_FAIL", 0, 1, userParam);
    ListRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, 1, getpid() };
    CopyKey(req.prefix, "a", KEY_MAX_SIZE);
    auto ret = List(&req, nullptr);
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "LIST_LIST_FAIL");
}

TEST_F(TestBioServer, test_list_malloc_rsp_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "LIST_MALLOC_RSP_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "LIST_MALLOC_RSP_FAIL_RESET", 0, 1, userParam);
    ListRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, 1, getpid() };
    CopyKey(req.prefix, "a", KEY_MAX_SIZE);
    auto ret = List(&req, nullptr);
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
    LVOS_HVS_deactiveTracePoint(0, "LIST_MALLOC_RSP_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "LIST_MALLOC_RSP_FAIL_RESET");
}

TEST_F(TestBioServer, test_put_slice_length_eq_zero_return_fail)
{
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcd1", KEY_MAX_SIZE);
    req.length = NO_128;
    req.mrKey = 1;
    req.sliceLen = 0;
    req.copyFree = false;
    req.memFromServer = true;
    req.mrAddress = 0ULL;
    req.mrSize = 0;
    auto ret = Put(&req);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
}

TEST_F(TestBioServer, test_put_slice_length_eq_zero_alloc_slice_err_return_fail)
{
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcd1", KEY_MAX_SIZE);
    req.length = NO_128;
    req.mrKey = 1;
    req.sliceLen = 0;
    req.copyFree = false;
    req.memFromServer = true;
    req.mrAddress = 0ULL;
    req.mrSize = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "PUT_SLICELEN_ZERO_ALLOC_SLICE_FAIL", 0, 1, userParam);
    auto ret = Put(&req);
    LVOS_HVS_deactiveTracePoint(0, "PUT_SLICELEN_ZERO_ALLOC_SLICE_FAIL");
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
}

TEST_F(TestBioServer, test_put_slice_alloc_slice_err_return_fail)
{
    PutRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    req.tenantId = 1;
    req.affinity = 1;
    req.strategy = 1;
    CopyKey(req.key, "abcd1", KEY_MAX_SIZE);
    req.length = NO_128;
    req.mrKey = 1;
    req.sliceLen = NO_128;
    req.copyFree = false;
    req.memFromServer = true;
    req.mrAddress = 0ULL;
    req.mrSize = 0;
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "PUT_ALLOC_SLICE_FAIL", 0, 1, userParam);
    auto ret = Put(&req);
    LVOS_HVS_deactiveTracePoint(0, "PUT_ALLOC_SLICE_FAIL");
    EXPECT_EQ(ret, BIO_ALLOC_FAIL);
}

TEST_F(TestBioServer, test_get_slice_flowid_unexists_return_fail)
{
    GetSliceRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() }, NO_4194304, 0, 1, NO_128 };
    auto ret = GetSlice(&req, nullptr);
    EXPECT_EQ(ret, BIO_NOT_EXISTS);
}


TEST_F(TestBioServer, test_bio_server_stat)
{
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
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    ListRequest req;
    req.comm = { MESSAGE_MAGIC, 0, 0, 1, getpid() };
    CopyKey(req.prefix, "a", KEY_MAX_SIZE);
    auto ret = mirror->MirrorServerList(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_list_inner)
{
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
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    CreateFlowRequest req = { { MESSAGE_MAGIC, 0, 1, 1, getpid() }, 0, 1 };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL", 0, 1, userParam);
    mirror->MirrorServerCreateFlow(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL");
}

TEST_F(TestBioServer, test_mirror_slave_create_flow_wcache_err)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    CreateFlowRequest req = { { MESSAGE_MAGIC, 0, 1, 1, getpid() }, 1, 1 };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL", 0, 1, userParam);
    mirror->MirrorServerCreateFlow(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL");
}

TEST_F(TestBioServer, test_mirror_master_create_flow_rcache_alloc_obj_err)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    CreateFlowRequest req = { { MESSAGE_MAGIC, 0, 1, 1, getpid() }, 0, 1 };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_FIND", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_ALLOC_OBJ_FAIL", 0, 1, userParam);
    mirror->MirrorServerCreateFlow(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_FIND");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_ALLOC_OBJ_FAIL");
}

TEST_F(TestBioServer, test_mirror_other_create_flow_err)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    CreateFlowRequest req = { { MESSAGE_MAGIC, 0, 0, 1, getpid() }, 3, 1 };
    mirror->MirrorServerCreateFlow(ctx, &req);
}

TEST_F(TestBioServer, test_mirror_master_create_flow_rcache_init_obj_err)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    CreateFlowRequest req = { { MESSAGE_MAGIC, 0, 0, 1, getpid() }, 0, 1 };
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_FIND", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_INIT_OBJ_FAIL", 0, 1, userParam);
    mirror->MirrorServerCreateFlow(ctx, &req);
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_FLOW_CREATE_WCACHE_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_FIND");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_INIT_OBJ_FAIL");
}

TEST_F(TestBioServer, test_bio_server_load)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    LoadRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    CopyKey(req.key, "abcd", KEY_MAX_SIZE);
    auto ret = mirror->MirrorServerLoad(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_delete)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    DeleteRequest req;
    req.comm = { MESSAGE_MAGIC, 1, 1, 1, getpid() };
    CopyKey(req.key, "abcd", KEY_MAX_SIZE);
    auto ret = mirror->MirrorServerDelete(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_report_hb)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    auto ret = mirror->MirrorServerReportHb(ctx);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_get_slice)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    GetSliceRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() }, 1, 0, 1, 128 };
    auto ret = mirror->MirrorServerGetSlice(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_sync_data)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    SyncDataRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() } };
    auto ret = mirror->MirrorServerSyncData(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_get_evict_off)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    GetEvictRequest req = { { MESSAGE_MAGIC, 1, 1, 1, getpid() }, 1 };
    auto ret = mirror->MirrorServerGetEvictOffset(ctx, &req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_reader)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    std::vector<FlowAddr> addr;
    addr.emplace_back(FlowAddr(1, 0, NO_128));
    WCacheSlicePtr from = MakeRef<WCacheSlice>(1, 1, 1, NO_128, addr);
    WCacheSlicePtr to = MakeRef<WCacheSlice>(1, 1, 1, NO_128, addr);
    PutRequest req;
    ServiceContext netCtx;
    auto ret = mirror->ReaderRemote(from.Get(), to.Get(), req, netCtx);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_reader_not_equal)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    std::vector<FlowAddr> addr;
    addr.emplace_back(FlowAddr(1, 0, NO_128));
    std::vector<FlowAddr> addrTo;
    addrTo.emplace_back(FlowAddr(1, 0, NO_256));
    WCacheSlicePtr from = MakeRef<WCacheSlice>(1, 1, 1, NO_128, addr);
    WCacheSlicePtr to = MakeRef<WCacheSlice>(1, 1, 1, NO_256, addrTo);
    PutRequest req;
    req.memFromServer = true;
    req.comm.srcNid = NO_10;
    ServiceContext netCtx;
    auto ret = mirror->ReaderRemote(from.Get(), to.Get(), req, netCtx);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestBioServer, test_bio_server_reader_not_equal_server_false)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    std::vector<FlowAddr> addr;
    addr.emplace_back(FlowAddr(1, 0, NO_128));
    std::vector<FlowAddr> addrTo;
    addrTo.emplace_back(FlowAddr(1, 0, NO_256));
    addrTo.emplace_back(FlowAddr(1, 0, NO_256));
    WCacheSlicePtr from = MakeRef<WCacheSlice>(1, 1, 1, NO_128, addr);
    WCacheSlicePtr to = MakeRef<WCacheSlice>(1, 1, 1, NO_256, addrTo);
    PutRequest req;
    req.memFromServer = true;
    req.comm.srcNid = NO_10;
    ServiceContext netCtx;
    auto ret = mirror->ReaderRemote(from.Get(), to.Get(), req, netCtx);
    EXPECT_EQ(ret, BIO_NET_RETRY);
}

TEST_F(TestBioServer, test_bio_server_reader_not_equal_to_err)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    std::vector<FlowAddr> addr;
    addr.emplace_back(FlowAddr(1, 0, NO_128));
    addr.emplace_back(FlowAddr(1, 0, NO_128));
    std::vector<FlowAddr> addrTo;
    addrTo.emplace_back(FlowAddr(1, 0, NO_100));
    WCacheSlicePtr from = MakeRef<WCacheSlice>(1, 1, 1, NO_128, addr);
    WCacheSlicePtr to = MakeRef<WCacheSlice>(1, 1, 1, NO_100, addrTo);
    PutRequest req;
    req.memFromServer = false;
    req.comm.srcNid = NO_10;
    ServiceContext netCtx;
    auto ret = mirror->ReaderRemote(from.Get(), to.Get(), req, netCtx);
    EXPECT_EQ(ret, BIO_INNER_ERR);
}

TEST_F(TestBioServer, test_bio_server_writer)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    std::vector<FlowAddr> addr;
    addr.emplace_back(FlowAddr(1, 0, NO_128));
    WCacheSlicePtr from = MakeRef<WCacheSlice>(1, 1, 1, NO_128, addr);
    WCacheSlicePtr to = MakeRef<WCacheSlice>(1, 1, 1, NO_128, addr);
    std::vector<NetMrInfo> rMrVec;
    rMrVec.emplace_back(NetMrInfo(123U, 128U, 1));
    std::vector<NetMrInfo> lMrVec;
    lMrVec.emplace_back(NetMrInfo(123U, 128U, 1));
    uint32_t rKey = 1;
    bool isAlloc = true;
    auto ret = mirror->WriterParseMrInfo(from.Get(), to.Get(), rMrVec, lMrVec, rKey, isAlloc);
    EXPECT_EQ(ret, BIO_OK);

    GetResponse rsp;
    ret = mirror->WriterLocalDiffProcess(isAlloc, lMrVec, rsp);

    ServiceContext netCtx;
    GetRequest req;
    req.comm.srcNid = 1;
    std::vector<NetMrInfo> rMrVec1;
    rMrVec1.emplace_back(NetMrInfo(123U, 128U, 1));
    isAlloc = false;
    ret = mirror->WriterRemote(isAlloc, lMrVec, rMrVec1, netCtx, req);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_handle)
{
    MirrorServerPtr mirror = BioServer::Instance()->GetMirrorServer();
    ServiceContext ctx;
    auto ret = mirror->HandleQueryNodeInfoByPt(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleQueryNodeInfo(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleQueryNodeView(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleQueryPtView(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleGetEvictOffset(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandlePut(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleGet(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleDelete(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleStat(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleList(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleLoad(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleReportHb(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleCreateFlow(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleDestroyFlow(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleGetSlice(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleSyncData(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleGetEvictOffset(ctx);
    EXPECT_EQ(ret, BIO_OK);

    ret = mirror->HandleFreeMem(ctx);
    EXPECT_EQ(ret, BIO_OK);
}

TEST_F(TestBioServer, test_bio_server_add_finish_list)
{
    MirrorServerCrbPtr crb = BioServer::Instance()->GetMirrorCrb();
    CmPtTaskPtr ptTask = MakeRef<CmPtTask>();
    CmPtInfo ptInfo;
    crb->JobAddFinishList(ptTask, ptInfo);
}

TEST_F(TestBioServer, test_bio_server_add_retry_list)
{
    MirrorServerCrbPtr crb = BioServer::Instance()->GetMirrorCrb();
    CmPtTaskPtr ptTask = MakeRef<CmPtTask>();
    CmPtInfo ptInfo;
    crb->JobAddRetryList(ptTask, ptInfo);
}

TEST_F(TestBioServer, test_bio_server_expire_clear)
{
    MirrorServerCrbPtr crb = BioServer::Instance()->GetMirrorCrb();
    CmPtInfo ptInfo;
    ptInfo.ptId = 1;
    ptInfo.version = 1;
    crb->JobExpiredClear(ptInfo);
}

TEST_F(TestBioServer, test_bio_server_add_sync_data)
{
    MirrorServerCrbPtr crb = BioServer::Instance()->GetMirrorCrb();
    CmPtInfo ptInfo;
    ptInfo.ptId = 1;
    ptInfo.version = 1;
    crb->JobSyncData(ptInfo);
}

TEST_F(TestBioServer, test_start_server_log_options_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "MIN_LOG_LEVEL_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIN_LOG_LEVEL_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_LOG_INSTANCE", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "MIN_LOG_LEVEL_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MIN_LOG_LEVEL_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_LOG_INSTANCE");
}

TEST_F(TestBioServer, test_start_server_log_options_err1_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "LOG_ROTATION_FILE_SIZE_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "LOG_ROTATION_FILE_SIZE_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_LOG_INSTANCE", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "LOG_ROTATION_FILE_SIZE_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "LOG_ROTATION_FILE_SIZE_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_LOG_INSTANCE");
}

TEST_F(TestBioServer, test_start_server_log_options_err2_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "LOG_ROTATION_FILE_COUNT_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "LOG_ROTATION_FILE_COUNT_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_LOG_INSTANCE", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "LOG_ROTATION_FILE_COUNT_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "LOG_ROTATION_FILE_COUNT_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_LOG_INSTANCE");
}

TEST_F(TestBioServer, test_start_server_log_init_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "LOG_INIT_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "LOG_INIT_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
}

TEST_F(TestBioServer, test_start_server_config_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "CONFIG_INSTANCE_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "CONFIG_INSTANCE_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CONFIG", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "CONFIG_INSTANCE_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "CONFIG_INSTANCE_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CONFIG");
}

TEST_F(TestBioServer, test_start_server_service_tracer_init_open_file_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "TRACE_FILE_OPPEN_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "TRACE_FILE_OPPEN_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
}

TEST_F(TestBioServer, test_start_server_service_tracer_init_create_dir_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "TRACE_CREATE_DIR_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "TRACE_CREATE_DIR_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
}

TEST_F(TestBioServer, test_start_server_service_tracer_init_path_real_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "TRACE_PATH_REAL_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "TRACE_PATH_REAL_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "TRACE_PATH_REAL_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "TRACE_PATH_REAL_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
}

TEST_F(TestBioServer, test_start_server_underfs_init_dir_exist_return_ok)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_UNDERFS_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_OK);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_UNDERFS_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
}

TEST_F(TestBioServer, test_start_server_underfs_init_make_dir_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_UNDERFS_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "UNDERFS_MKDIR_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "UNDERFS_OPEN_DIR_FAIL", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_UNDERFS_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_MKDIR_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "UNDERFS_OPEN_DIR_FAIL");
}

TEST_F(TestBioServer, test_start_server_mirrorserver_init_mirror_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_INIT_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_MIRROR_SERVER_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_INIT_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_MIRROR_SERVER_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
}

TEST_F(TestBioServer, test_start_server_mirrorserver_init_mirror_task_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_MIRROR_SERVER_CRB_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_MIRROR_SERVER_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_TASK_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_TASK_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_MIRROR_SERVER_CRB_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_MIRROR_SERVER_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_TASK_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_TASK_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
}

TEST_F(TestBioServer, test_start_server_mirrorserver_init_mirror_job_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_MIRROR_SERVER_CRB_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_MIRROR_SERVER_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_JOB_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_JOB_FAIL_RESET", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_TASK_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_TASK_FAIL_RESET_OUTER", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_MIRROR_SERVER_TASK_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_MIRROR_SERVER_CRB_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_MIRROR_SERVER_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_JOB_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_JOB_FAIL_RESET");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_TASK_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_TASK_FAIL_RESET_OUTER");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_MIRROR_SERVER_TASK_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
}

TEST_F(TestBioServer, test_start_server_mirrorserver_init_executor_queue_init_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_MIRROR_SERVER_CRB_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_MIRROR_SERVER_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_EXECUTOR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "QUEUE_INIT_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_TASK_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_TASK_FAIL_RESET_OUTER", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_MIRROR_SERVER_CRB_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_MIRROR_SERVER_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_EXECUTOR");
    LVOS_HVS_deactiveTracePoint(0, "QUEUE_INIT_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_TASK_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_TASK_FAIL_RESET_OUTER");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
}

TEST_F(TestBioServer, test_start_server_mirrorserver_init_executor_thread_init_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_MIRROR_SERVER_CRB_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_MIRROR_SERVER_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_EXECUTOR", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "EXECUTOR_THREAD_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_TASK_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "MIRROR_SERVER_TASK_FAIL_RESET_OUTER", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_MIRROR_SERVER_CRB_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_MIRROR_SERVER_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_EXECUTOR");
    LVOS_HVS_deactiveTracePoint(0, "EXECUTOR_THREAD_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_TASK_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "MIRROR_SERVER_TASK_FAIL_RESET_OUTER");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
}

TEST_F(TestBioServer, test_start_server_cache_init_rcache_evict_param_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_EVICT_PARAM_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CACHE_INIT", 0, 1, userParam);

    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_EVICT_PARAM_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CACHE_INIT");
}

TEST_F(TestBioServer, test_start_server_cache_init_rcache_evict_thread_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_EVICT_THREAD_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CACHE_INIT", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_EVICT_THREAD_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CACHE_INIT");
}

TEST_F(TestBioServer, test_start_server_cache_init_rcache_gc_param_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_GC_PARAM_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_GC", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CACHE_INIT", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_GC_PARAM_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_GC");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CACHE_INIT");
}

TEST_F(TestBioServer, test_start_server_cache_init_rcache_gc_thread_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "RCACHE_GC_THREAD_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_EVICT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_RCACHE_GC", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CACHE_INIT", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
    LVOS_HVS_deactiveTracePoint(0, "RCACHE_GC_THREAD_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_EVICT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_RCACHE_GC");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CACHE_INIT");
}

TEST_F(TestBioServer, test_start_server_cache_init_cache_recover_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CACHE_PROCESS", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CACHE_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "CACHE_RECOVER_FM_GET_ALL_OBJECT_FAIL", 0, 1, userParam);

    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CACHE_PROCESS");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CACHE_INIT");
    LVOS_HVS_deactiveTracePoint(0, "CACHE_RECOVER_FM_GET_ALL_OBJECT_FAIL");
}

TEST_F(TestBioServer, test_start_server_diagnose_cliagent_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "CLI_AGENT_INIT_ERR", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
    LVOS_HVS_deactiveTracePoint(0, "CLI_AGENT_INIT_ERR");
}

TEST_F(TestBioServer, test_start_server_diagnose_handler_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "CLI_SERVER_DIAGNOSE_HANDLER_ERR", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
    LVOS_HVS_deactiveTracePoint(0, "CLI_SERVER_DIAGNOSE_HANDLER_ERR");
}

TEST_F(TestBioServer, test_start_server_diagnose_init_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "CLI_SERVER_DIAGNOSE_INIT_ERR", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_ROLLBACK_SERVICE_INIT");
    LVOS_HVS_deactiveTracePoint(0, "CLI_SERVER_DIAGNOSE_INIT_ERR");
}

TEST_F(TestBioServer, test_start_server_service_start_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "SERVICE_START_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_ERR);
    LVOS_HVS_deactiveTracePoint(0, "SERVICE_START_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
}

TEST_F(TestBioServer, test_start_server_config_init_err_return_fail)
{
    LVOS_TRACEP_PARAM_S userParam;
    LVOS_HVS_activeTracePoint(0, "CONFIG_INIT_FAIL", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_SERVER_START", 0, 1, userParam);
    LVOS_HVS_activeTracePoint(0, "NO_PROCESS_CONFIG", 0, 1, userParam);
    auto ret = BioServer::Instance()->Start();
    EXPECT_EQ(ret, BIO_INNER_ERR);
    LVOS_HVS_deactiveTracePoint(0, "CONFIG_INIT_FAIL");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_SERVER_START");
    LVOS_HVS_deactiveTracePoint(0, "NO_PROCESS_CONFIG");
}
