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
#ifndef BIO_NET_HCOM_TYPES_H
#define BIO_NET_HCOM_TYPES_H

#include <functional>
#include <memory>
#include <new>
#include "hcom_api_dl.h"
#include "bio_err.h"

namespace ock {
namespace bio {
enum ServiceProtocol {
    RDMA = C_SERVICE_RDMA,
    TCP = C_SERVICE_TCP,
    UDS = C_SERVICE_UDS,
    SHM = C_SERVICE_SHM,
    UNKNOWN = 255,
};

struct NetCallRequest : ubs_hcom_channel_request {
    NetCallRequest(void *data = nullptr, uint32_t length = 0, uint16_t op = 0)
        : ubs_hcom_channel_request{}
    {
        address = data;
        size = length;
        opcode = op;
    }
};
struct NetCallResponse : ubs_hcom_channel_response {
    NetCallResponse(void *data = nullptr, uint32_t length = 0) : ubs_hcom_channel_response{}
    {
        address = data;
        size = length;
    }
};
using NetRequest = ubs_hcom_oneside_request;
class ServiceContext;
using NetCallback = std::function<void(ServiceContext &)>;

class NetChannel {
public:
    NetChannel(std::nullptr_t = nullptr) : handle_(0) {}
    explicit NetChannel(ubs_hcom_channel handle, bool adopt = false) : handle_(handle)
    {
        if (handle_ != 0 && !adopt) {
            HcomApi::Instance().ubs_hcom_channel_refer(handle_);
        }
    }
    ~NetChannel()
    {
        if (handle_ != 0) {
            HcomApi::Instance().ubs_hcom_channel_derefer(handle_);
        }
    }
    NetChannel(const NetChannel &other) : NetChannel(other.handle_) {}
    NetChannel(NetChannel &&other) noexcept : handle_(other.handle_) { other.handle_ = 0; }
    NetChannel &operator=(const NetChannel &other)
    {
        if (handle_ != other.handle_) {
            NetChannel copy(other);
            std::swap(handle_, copy.handle_);
        }
        return *this;
    }
    NetChannel &operator=(NetChannel &&other) noexcept
    {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = 0;
        }
        return *this;
    }
    void reset()
    {
        if (handle_ != 0) {
            HcomApi::Instance().ubs_hcom_channel_derefer(handle_);
            handle_ = 0;
        }
    }
    bool operator==(std::nullptr_t) const { return handle_ == 0; }
    bool operator!=(std::nullptr_t) const { return handle_ != 0; }
    NetChannel *operator->() const { return const_cast<NetChannel *>(this); }
    ubs_hcom_channel Handle() const { return handle_; }
    uint64_t GetId() const { return HcomApi::Instance().ubs_hcom_channel_get_id(handle_); }
    uint64_t GetUpCtx() const { return HcomApi::Instance().ubs_hcom_channel_get_up_ctx(handle_); }
    void SetUpCtx(uint64_t ctx) { HcomApi::Instance().ubs_hcom_channel_set_up_ctx(handle_, ctx); }
    void SetChannelTimeOut(int16_t one, int16_t two)
    {
        HcomApi::Instance().ubs_hcom_channel_set_timeout(handle_, one, two);
    }
    int SendFds(int *fds, uint32_t count)
    {
        return HcomApi::Instance().ubs_hcom_channel_send_fds(handle_, fds, count);
    }
    int ReceiveFds(int *fds, uint32_t count, int32_t timeout)
    {
        return HcomApi::Instance().ubs_hcom_channel_recv_fds(handle_, fds, count, timeout);
    }
    int Get(const NetRequest &req, std::nullptr_t)
    {
        return HcomApi::Instance().ubs_hcom_channel_get(handle_, req, nullptr);
    }
    int Put(const NetRequest &req, std::nullptr_t)
    {
        return HcomApi::Instance().ubs_hcom_channel_put(handle_, req, nullptr);
    }
    int Call(const NetCallRequest &req, NetCallResponse &rsp, const NetCallback &cb = {});
    int Reply(ubs_hcom_channel_reply_context ctx, const NetCallRequest &req, const NetCallback &cb = {});

private:
    ubs_hcom_channel handle_;
};
using ChannelPtr = NetChannel;

inline ChannelPtr MakeNetChannel(ubs_hcom_channel handle, bool adopt = false)
{
    return NetChannel(handle, adopt);
}

class ServiceContext {
public:
    explicit ServiceContext(ubs_hcom_service_context handle = 0) : handle_(handle) {}
    ~ServiceContext()
    {
        if (owned_ && handle_ != 0) {
            HcomApi::Instance().ubs_hcom_context_free(handle_);
        }
    }
    ServiceContext(const ServiceContext &) = delete;
    ServiceContext &operator=(const ServiceContext &) = delete;
    int Result() const
    {
        int result = BIO_ERR;
        if (handle_ != 0) {
            HcomApi::Instance().ubs_hcom_context_get_result(handle_, &result);
        }
        return result;
    }
    int32_t ErrorCode() const
    {
        int32_t result = BIO_ERR;
        if (handle_ != 0) {
            HcomApi::Instance().ubs_hcom_context_get_error_code(handle_, &result);
        }
        return result;
    }
    ChannelPtr Channel() const
    {
        ubs_hcom_channel channel = 0;
        if (handle_ != 0) {
            HcomApi::Instance().ubs_hcom_context_get_channel(handle_, &channel);
        }
        return MakeNetChannel(channel);
    }
    uint16_t OpCode() const
    {
        return handle_ == 0 ? UINT16_MAX : HcomApi::Instance().ubs_hcom_context_get_opcode(handle_);
    }
    void *MessageData() const
    {
        return handle_ == 0 ? nullptr : HcomApi::Instance().ubs_hcom_context_get_data(handle_);
    }
    uint32_t MessageDataLen() const
    {
        return handle_ == 0 ? 0 : HcomApi::Instance().ubs_hcom_context_get_datalen(handle_);
    }
    void *RspCtx() const
    {
        ubs_hcom_channel_reply_context ctx{};
        if (handle_ != 0) {
            HcomApi::Instance().ubs_hcom_context_get_rspctx(handle_, &ctx);
        }
        return ctx.rspCtx;
    }
    static int Clone(ServiceContext &dst, const ServiceContext &src)
    {
        if (src.handle_ == 0 || dst.handle_ != 0) {
            return BIO_INVALID_PARAM;
        }
        dst.handle_ = HcomApi::Instance().ubs_hcom_context_clone(src.handle_);
        dst.owned_ = dst.handle_ != 0;
        return dst.owned_ ? BIO_OK : BIO_ALLOC_FAIL;
    }

private:
    ubs_hcom_service_context handle_ = 0;
    bool owned_ = false;
};

// HCOM copies cb/arg during submission. The captured state lives until completion;
// a failed submission never invokes the callback and releases the state here.
inline void NetCompletion(void *arg, ubs_hcom_service_context handle)
{
    std::unique_ptr<NetCallback> callback(static_cast<NetCallback *>(arg));
    ServiceContext context(handle);
    (*callback)(context);
}

template <typename Submit> int SubmitNetOperation(const NetCallback &callback, Submit submit)
{
    if (!callback) {
        return submit(nullptr);
    }
    auto *state = new (std::nothrow) NetCallback(callback);
    if (state == nullptr) {
        return BIO_ALLOC_FAIL;
    }
    ubs_hcom_channel_callback cb{NetCompletion, state};
    int ret = submit(&cb);
    if (ret != BIO_OK) {
        delete state;
    }
    return ret;
}

inline int NetChannel::Call(const NetCallRequest &req, NetCallResponse &rsp, const NetCallback &cb)
{
    return SubmitNetOperation(cb, [&](ubs_hcom_channel_callback *completion) {
        return HcomApi::Instance().ubs_hcom_channel_call(handle_, req, &rsp, completion);
    });
}

inline int NetChannel::Reply(ubs_hcom_channel_reply_context ctx, const NetCallRequest &req, const NetCallback &cb)
{
    return SubmitNetOperation(cb, [&](ubs_hcom_channel_callback *completion) {
        return HcomApi::Instance().ubs_hcom_channel_reply(handle_, req, ctx, completion);
    });
}

struct MemoryRegion {
    ubs_hcom_memory_region handle = 0;
    ubs_hcom_mr_info info{};
    uintptr_t GetAddress() const { return info.lAddress; }
    uint64_t GetSize() const { return info.size; }
    uint64_t GetKey() const { return info.lKey.keys[0]; }
    bool IsValid() const { return handle != 0; }
};
}
}
#endif
