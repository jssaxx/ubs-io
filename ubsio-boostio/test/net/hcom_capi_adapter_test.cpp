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
#include <cstdio>
#include <cstdlib>
#include "net_hcom_types.h"

// Standalone adapter test: no HCOM or BoostIO shared libraries are linked.
namespace ock {
namespace bio {
HcomApi &HcomApi::Instance()
{
    static HcomApi api;
    return api;
}
}
}
using namespace ock::bio;

namespace {
constexpr ubs_hcom_channel CHANNEL = 11;
constexpr ubs_hcom_service_context BORROWED_CONTEXT = 21;
constexpr ubs_hcom_service_context CLONED_CONTEXT = 22;
constexpr int BUSINESS_ERROR = 37;
constexpr int TRANSPORT_TIMEOUT = 509;
int references = 1;
int contextFrees = 0;
ubs_hcom_channel_callback pending{};

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::_Exit(EXIT_FAILURE);
    }
}
}

int main()
{
    auto &api = HcomApi::Instance();
    api.ubs_hcom_channel_refer = [](ubs_hcom_channel ch) {
        Require(ch == CHANNEL, "reference channel handle");
        ++references;
    };
    api.ubs_hcom_channel_derefer = [](ubs_hcom_channel ch) {
        Require(ch == CHANNEL, "dereference channel handle");
        --references;
    };
    api.ubs_hcom_context_get_channel = [](ubs_hcom_service_context, ubs_hcom_channel *ch) -> int {
        *ch = CHANNEL;
        return BIO_OK;
    };
    api.ubs_hcom_context_get_result = [](ubs_hcom_service_context, int *result) -> int {
        *result = BIO_OK;
        return BIO_OK;
    };
    api.ubs_hcom_context_get_error_code = [](ubs_hcom_service_context, int32_t *result) -> int {
        *result = BUSINESS_ERROR;
        return BIO_OK;
    };
    api.ubs_hcom_context_clone = [](ubs_hcom_service_context ctx) {
        Require(ctx == BORROWED_CONTEXT, "clone borrowed context handle");
        return CLONED_CONTEXT;
    };
    api.ubs_hcom_context_free = [](ubs_hcom_service_context ctx) {
        Require(ctx == CLONED_CONTEXT, "free cloned context handle");
        ++contextFrees;
    };
    {
        auto ch = MakeNetChannel(CHANNEL, true);
        auto copy = ch;
        Require(references == 2, "copied channel acquires reference");
        auto moved = std::move(copy);
        Require(copy == nullptr && references == 2, "moved channel transfers reference");
        ChannelPtr assigned;
        assigned = moved;
        Require(references == 3, "assigned channel acquires reference");
        assigned = nullptr;
        Require(references == 2, "cleared channel releases reference");
        ServiceContext borrowed(BORROWED_CONTEXT);
        {
            auto fromContext = borrowed.Channel();
            Require(references == 3, "borrowed context channel acquires reference");
        }
        Require(references == 2, "context channel releases reference");
        ServiceContext clone;
        Require(ServiceContext::Clone(clone, borrowed) == BIO_OK, "clone context");
        Require(clone.Result() == BIO_OK, "cloned context transport result");
        Require(clone.ErrorCode() == BUSINESS_ERROR, "cloned context business error");
        Require(ServiceContext::Clone(clone, borrowed) == BIO_INVALID_PARAM, "reject overwrite of owned context");
    }
    Require(references == 0 && contextFrees == 1, "release channels and cloned context");

    api.ubs_hcom_channel_call = [](ubs_hcom_channel ch, ubs_hcom_channel_request req,
        ubs_hcom_channel_response *rsp, ubs_hcom_channel_callback *cb) -> int {
        Require(ch == CHANNEL && req.key == 0, "call channel handle and default key");
        if (cb != nullptr) {
            pending = *cb; // completion happens after the caller's stack frame
        } else {
            rsp->errorCode = BUSINESS_ERROR;
        }
        return BIO_OK;
    };
    ++references;
    auto ch = MakeNetChannel(CHANNEL, true);
    NetCallRequest req;
    NetCallResponse rsp;
    Require(ch->Call(req, rsp) == BIO_OK && rsp.errorCode == BUSINESS_ERROR, "sync call preserves business error");
    auto token = std::make_shared<int>(0);
    std::weak_ptr<int> weak = token;
    {
        NetCallback cb = [token](ServiceContext &ctx) {
            Require(ctx.Result() == BIO_OK && ctx.ErrorCode() == BUSINESS_ERROR, "async call preserves business error");
            ++*token;
        };
        Require(ch->Call(req, rsp, cb) == BIO_OK, "submit deferred callback");
    }
    Require(token.use_count() == 2, "retain deferred callback capture");
    pending.cb(pending.arg, BORROWED_CONTEXT);
    Require(*token == 1 && token.use_count() == 1, "invoke and release deferred callback");

    api.ubs_hcom_channel_call = [](ubs_hcom_channel, ubs_hcom_channel_request,
        ubs_hcom_channel_response *, ubs_hcom_channel_callback *) { return TRANSPORT_TIMEOUT; };
    {
        NetCallback cb = [token](ServiceContext &) { Require(false, "unexpected callback after submission failure"); };
        Require(ch->Call(req, rsp, cb) == TRANSPORT_TIMEOUT, "propagate submission failure");
    }
    Require(token.use_count() == 1, "release callback after submission failure");
    api.ubs_hcom_channel_call = [](ubs_hcom_channel, ubs_hcom_channel_request,
        ubs_hcom_channel_response *, ubs_hcom_channel_callback *cb) -> int {
        cb->cb(cb->arg, BORROWED_CONTEXT);
        return BIO_OK;
    };
    {
        NetCallback cb = [token](ServiceContext &) { ++*token; };
        Require(ch->Call(req, rsp, cb) == BIO_OK, "submit immediate callback");
    }
    Require(*token == 2 && token.use_count() == 1, "invoke and release immediate callback");
    token.reset();
    Require(weak.expired(), "release all callback captures");

    api.ubs_hcom_channel_reply = [](ubs_hcom_channel, ubs_hcom_channel_request request,
        ubs_hcom_channel_reply_context ctx, ubs_hcom_channel_callback *cb) -> int {
        Require(request.opcode == BUSINESS_ERROR && ctx.errorCode == BUSINESS_ERROR && cb == nullptr,
            "reply preserves opcode and business error without callback");
        return BIO_OK;
    };
    req.opcode = BUSINESS_ERROR;
    ubs_hcom_channel_reply_context reply{};
    reply.errorCode = BUSINESS_ERROR;
    Require(ch->Reply(reply, req) == BIO_OK, "reply transport result");
    ch.reset();
    Require(references == 0 && contextFrees == 1, "release final channel reference");
    std::puts("HCOM C adapter lifetime/error/callback checks passed");
    return 0;
}
