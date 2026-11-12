/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.

 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/eventfd.h>
#include <unistd.h>
#include "net_hcom_types.h"

using namespace ock::bio;
namespace {
constexpr uint32_t SEGMENT_BYTES = 64 * 1024;
constexpr uint32_t MEMORY_BYTES = 4096;
constexpr int16_t BUSINESS_ERROR = 37;
constexpr int16_t TIMEOUT_SECONDS = 5;
constexpr uint16_t DEFAULT_PORT = 29873;
constexpr uint16_t FD_OPCODE = 2;
constexpr uint64_t EVENT_VALUE = 7;
constexpr auto WAIT_TIMEOUT = std::chrono::seconds(TIMEOUT_SECONDS);
std::mutex workerMutex;
std::vector<std::thread> workers;
std::atomic<int> failure{0};
int sentFd = -1;

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::_Exit(EXIT_FAILURE);
    }
}

int AcceptChannel(ubs_hcom_channel channel, const char *, uint64_t, const char *)
{
    // New-channel C callbacks hand over one reference, unlike context accessors.
    auto ch = MakeNetChannel(channel, true);
    ch->SetChannelTimeOut(TIMEOUT_SECONDS, TIMEOUT_SECONDS);
    return BIO_OK;
}

int Receive(ubs_hcom_service_context handle, uint64_t)
{
    ServiceContext borrowed(handle);
    auto cloned = std::make_shared<ServiceContext>();
    if (ServiceContext::Clone(*cloned, borrowed) != BIO_OK) {
        failure = 1;
        return BIO_ERR;
    }
    std::lock_guard<std::mutex> guard(workerMutex);
    workers.emplace_back([cloned]() {
        auto ch = cloned->Channel();
        if (cloned->OpCode() == FD_OPCODE && ch->SendFds(&sentFd, 1) != BIO_OK) {
            failure = 1;
        }
        ubs_hcom_channel_reply_context reply{};
        reply.rspCtx = cloned->RspCtx();
        reply.errorCode = BUSINESS_ERROR;
        NetCallRequest response(cloned->MessageData(), cloned->MessageDataLen());
        if (ch->Reply(reply, response) != BIO_OK) {
            failure = 1;
        }
    });
    return BIO_OK;
}

int IgnoreRequest(ubs_hcom_service_context, uint64_t) { return BIO_OK; }
int IgnoreBroken(ubs_hcom_channel, const char *, uint64_t, const char *) { return BIO_OK; }

void Configure(ubs_hcom_service service)
{
    auto &api = HcomApi::Instance();
    api.ubs_hcom_service_set_ipmask(service, "127.0.0.0/8");
    api.ubs_hcom_service_set_tls_opt(service, false, C_SERVICE_TLS_1_3, C_SERVICE_AES_GCM_128,
        nullptr, nullptr, nullptr, nullptr, nullptr);
    api.ubs_hcom_service_register_broken_handler(service, IgnoreBroken, C_CHANNEL_BROKEN_ALL, 0);
    api.ubs_hcom_service_register_handler(service, C_SERVICE_REQUEST_RECEIVED, Receive, 0);
    api.ubs_hcom_service_register_handler(service, C_SERVICE_REQUEST_POSTED, IgnoreRequest, 0);
    api.ubs_hcom_service_register_handler(service, C_SERVICE_READWRITE_DONE, IgnoreRequest, 0);
}
}

int main(int argc, char **argv)
{
    Require(argc == 2, "usage: hcom_capi_smoke tcp|shm");
    bool shm = std::string(argv[1]) == "shm";
    Require(shm || std::string(argv[1]) == "tcp", "unsupported protocol");
    auto &api = HcomApi::Instance();
    Require(api.Load(), "load all required HCOM C symbols");
    auto type = shm ? C_SERVICE_SHM : C_SERVICE_TCP;
    std::string suffix = std::to_string(getpid());
    std::string serverName = "BIO_C_SERVER_" + suffix;
    std::string clientName = "BIO_C_CLIENT_" + suffix;
    std::string url = shm ? "uds://BIO_C_SMOKE_" + suffix :
        "tcp://127.0.0.1:" + std::to_string(DEFAULT_PORT);
    ubs_hcom_service_options options{};
    options.maxSendRecvDataSize = SEGMENT_BYTES;
    options.workerGroupThreadCount = 1;
    options.workerGroupMode = C_SERVICE_EVENT_POLLING;
    ubs_hcom_service server = 0;
    ubs_hcom_service client = 0;
    Require(api.ubs_hcom_service_create(type, serverName.c_str(), options, &server) == BIO_OK, "create server");
    Require(api.ubs_hcom_service_create(type, clientName.c_str(), options, &client) == BIO_OK, "create client");
    Configure(server);
    Configure(client);
    Require(api.ubs_hcom_service_bind(server, url.c_str(), AcceptChannel) == BIO_OK, "bind server");
    Require(api.ubs_hcom_service_start(server) == BIO_OK, "start server");
    Require(api.ubs_hcom_service_start(client) == BIO_OK, "start client");
    ubs_hcom_service_connect_options connect{};
    connect.linkCount = 1;
    connect.mode = C_CLIENT_WORKER_POLL;
    connect.cbType = C_CHANNEL_FUNC_CB;
    ubs_hcom_channel handle = 0;
    Require(api.ubs_hcom_service_connect(client, url.c_str(), &handle, connect) == BIO_OK, "connect");
    auto ch = MakeNetChannel(handle, true);
    ch->SetChannelTimeOut(TIMEOUT_SECONDS, TIMEOUT_SECONDS);
    ch->SetUpCtx(EVENT_VALUE);
    Require(ch->GetUpCtx() == EVENT_VALUE && ch->GetId() != 0, "channel metadata");

    char request[] = "hcom-c-api-echo";
    char response[sizeof(request)]{};
    NetCallRequest req(request, sizeof(request), 1);
    NetCallResponse rsp(response, sizeof(response));
    Require(ch->Call(req, rsp) == BIO_OK, "sync RPC transport result");
    Require(rsp.errorCode == BUSINESS_ERROR && std::memcmp(request, response, sizeof(request)) == 0,
        "sync RPC business error and cloned message");
    std::atomic<bool> completed{false};
    NetCallResponse asyncRsp;
    Require(ch->Call(req, asyncRsp, [&](ServiceContext &context) {
        if (context.Result() != BIO_OK || context.ErrorCode() != BUSINESS_ERROR ||
            context.MessageDataLen() != sizeof(request) ||
            std::memcmp(context.MessageData(), request, sizeof(request)) != 0) {
            failure = 1;
        }
        completed = true;
    }) == BIO_OK, "async RPC submission");
    auto deadline = std::chrono::steady_clock::now() + WAIT_TIMEOUT;
    while (!completed && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(completed && failure == 0, "async RPC completion and business error");

    ubs_hcom_memory_region local = 0;
    ubs_hcom_memory_region remote = 0;
    Require(api.ubs_hcom_service_register_memory_region(client, MEMORY_BYTES, &local) == BIO_OK, "allocate local MR");
    Require(api.ubs_hcom_service_register_memory_region(server, MEMORY_BYTES, &remote) == BIO_OK, "allocate remote MR");
    ubs_hcom_mr_info localInfo{};
    ubs_hcom_mr_info remoteInfo{};
    Require(api.ubs_hcom_service_get_memory_region_info(local, &localInfo) == BIO_OK, "local MR info");
    Require(api.ubs_hcom_service_get_memory_region_info(remote, &remoteInfo) == BIO_OK, "remote MR info");
    std::memcpy(reinterpret_cast<void *>(localInfo.lAddress), request, sizeof(request));
    NetRequest io{};
    io.lAddress = reinterpret_cast<void *>(localInfo.lAddress);
    io.rAddress = reinterpret_cast<void *>(remoteInfo.lAddress);
    io.lKey = localInfo.lKey;
    io.rKey = remoteInfo.lKey;
    io.size = sizeof(request);
    Require(ch->Put(io, nullptr) == BIO_OK, "one-side put");
    std::memset(io.lAddress, 0, sizeof(request));
    Require(ch->Get(io, nullptr) == BIO_OK, "one-side get");
    Require(std::memcmp(io.lAddress, request, sizeof(request)) == 0, "one-side roundtrip contents");
    Require(api.ubs_hcom_service_destroy_memory_region(client, local) == BIO_OK, "destroy local MR");
    Require(api.ubs_hcom_service_destroy_memory_region(server, remote) == BIO_OK, "destroy remote MR");
    ubs_hcom_memory_region assigned = 0;
    std::vector<char> buffer(MEMORY_BYTES);
    int assignResult = api.ubs_hcom_service_register_assign_memory_region(client,
        reinterpret_cast<uintptr_t>(buffer.data()), buffer.size(), &assigned);
    if (shm) {
        // SHM can allocate shared MRs, but cannot register an arbitrary heap buffer.
        Require(assignResult != BIO_OK && assigned == 0, "SHM rejects externally assigned MR");
    } else {
        Require(assignResult == BIO_OK, "register assigned MR");
        Require(api.ubs_hcom_service_destroy_memory_region(client, assigned) == BIO_OK, "destroy assigned MR");
    }

    if (shm) {
        sentFd = eventfd(0, EFD_CLOEXEC);
        Require(sentFd >= 0, "create isolated eventfd");
        req.opcode = FD_OPCODE;
        Require(ch->Call(req, rsp) == BIO_OK, "RPC with FD transfer");
        int receivedFd = -1;
        Require(ch->ReceiveFds(&receivedFd, 1, TIMEOUT_SECONDS) == BIO_OK, "receive FD");
        uint64_t value = EVENT_VALUE;
        Require(write(receivedFd, &value, sizeof(value)) == sizeof(value), "write received eventfd");
        value = 0;
        Require(read(sentFd, &value, sizeof(value)) == sizeof(value) && value == EVENT_VALUE, "FD identity");
        close(receivedFd);
        receivedFd = -1;
        close(sentFd);
        sentFd = -1;
    }
    {
        std::lock_guard<std::mutex> guard(workerMutex);
        for (auto &worker : workers) {
            worker.join();
        }
    }
    ch.reset();
    Require(api.ubs_hcom_service_destroy(client, clientName.c_str()) == BIO_OK, "destroy client");
    Require(api.ubs_hcom_service_destroy(server, serverName.c_str()) == BIO_OK, "destroy server");
    Require(failure == 0, "server completions");
    std::printf("HCOM C %s RPC/clone/MR/one-side%s checks passed\n", argv[1], shm ? "/FD" : "");
    return 0;
}
