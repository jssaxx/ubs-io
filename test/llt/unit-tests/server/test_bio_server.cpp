/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#include <mockcpp/mockcpp.hpp>
#include "gtest/gtest.h"
#include "message.h"
#include "bio_server.h"
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