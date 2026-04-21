# interceptor 到 JuiceFS 读写流程串讲

本文只讲当前主路径，也就是现在实际在跑的 `3copy` 方案。

不展开讲旧的 copy-free 主路径，只在最后说明它和当前主路径的关系。

## 1. 总体结论

当前主路径可以先用一句话概括：

- 写：`user buf -> interceptor shm -> JuiceFS page/block`
- 读：`JuiceFS page -> interceptor shm -> user buf`

所以：

- 当前写前台本质上是两次拷贝
- 当前读前台本质上也是两次拷贝

这也是为什么前面性能分析里，读写最终都收敛到了 `memcpy` 主导。

## 2. 初始化流程

大读和大写要走 shm，前提是 `interceptor client` 先和 server 建好一块共享内存池。

### 2.1 流程图

```text
应用进程启动
  -> LD_PRELOAD 进入 interceptor
  -> InterceptorClientNetService::StartNetService
  -> 建立 IPC ctrl channel
  -> CreateDataMessageMem
  -> server HandleInterceptorCreateDataMsgMemPool
  -> server 创建 /interceptor_mem_pool_<pid>
  -> server SendFds 回 client
  -> client mmap 这块 shm
  -> 后续大读/大写都从这里分 block
```

### 2.2 关键函数

- `interceptor client`
  - [interceptor_net.cpp:90](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_net.cpp:90)
    `InterceptorClientNetService::StartNetService`
    作用：初始化 `NetEngine`、建立 IPC ctrl channel、创建 data-msg shm pool。
  - [interceptor_net.cpp:171](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_net.cpp:171)
    `InterceptorClientNetService::CreateDataMessageMem`
    作用：向 server 申请 shm pool，接收 fd，并在 client 侧 `mmap`。

- `interceptor server`
  - [interceptor_server.cpp:265](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/server/interceptor_server.cpp:265)
    `InterceptorServer::HandleInterceptorCreateDataMsgMemPool`
    作用：按 `pid` 创建 `/interceptor_mem_pool_<pid>`，发送 fd 给 client，并在 server 侧保存 `pid -> shm 地址` 的映射。

## 3. 写流程

### 3.1 总体流程图

```text
应用 write/pwrite
  -> ProxyOperations::Pwrite / Write
  -> 按大小分流

小写:
  -> PwriteSmallInner
  -> 组请求包 InterceptorPwriteIn + data
  -> SendSyncBuff(BIO_OP_INTERCEPTOR_WRITE)
  -> server HandleInterceptorWrite
  -> BioWriteHook
  -> JuiceFS WriteHookJF
  -> VFS.Write
  -> fileWriter.Write
  -> writeChunk
  -> wSlice.WriteAt
  -> reply

大写:
  -> PwriteLargeInner
  -> AcquireLargeWriteBlock
  -> memcpy(user -> interceptor shm)
  -> SendSync(BIO_OP_INTERCEPTOR_LARGE_WRITE)
  -> server HandleInterceptorLargeWrite
  -> TransDataMsgMemAddr(pid, mrOffset)
  -> BioWriteHook(shmAddr)
  -> JuiceFS WriteHookJF
  -> VFS.Write
  -> fileWriter.Write
  -> writeChunk
  -> wSlice.WriteAt
  -> reply
  -> CacheLargeWriteBlock
```

### 3.2 interceptor client 层

- [interceptor_fileio.cpp:439](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:439)
  `ProxyOperations::PwriteInner`
  作用：写入口的大小分流，`<= small` 走小写，`> small` 走大写。

- [interceptor_fileio.cpp:448](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:448)
  `ProxyOperations::PwriteSmallInner`
  作用：小写路径。把 `InterceptorPwriteIn` 和用户数据组装成一个小包，直接 `SendSyncBuff` 给 server。

- [interceptor_fileio.cpp:483](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:483)
  `ProxyOperations::PwriteLargeInner`
  作用：大写路径。先从 shm pool 拿一块 block，把用户数据拷到 block，然后只把控制信息发给 server。

- [interceptor_fileio.cpp:50](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:50)
  `AcquireLargeWriteBlock`
  作用：给大写拿 shm block。优先复用线程本地缓存块，拿不到再去全局 pool 申请。

- [interceptor_fileio.cpp:69](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:69)
  `CacheLargeWriteBlock`
  作用：大写成功后把这块 block 缓存在当前线程，减少下一次 block alloc。

- [interceptor_fileio.cpp:75](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:75)
  `ReleaseLargeWriteBlock`
  作用：失败路径归还 block。

- [interceptor_fileio.cpp:84](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:84)
  `GetSmallWriteScratch`
  作用：给小写提供线程本地 scratch，避免每次 `new/delete`。

### 3.3 interceptor server 层

- [interceptor_server.cpp:150](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/server/interceptor_server.cpp:150)
  `HandleInterceptorWrite`
  作用：小写 handler。直接从请求包里拿 `req->data`，调 `BioWriteHook`。

- [interceptor_server.cpp:343](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/server/interceptor_server.cpp:343)
  `HandleInterceptorLargeWrite`
  作用：大写 handler。先把 `pid + mrOffset` 翻译成 server 侧 shm 地址，再把这块地址交给 `BioWriteHook`。

- [interceptor_server.cpp:214](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/server/interceptor_server.cpp:214)
  `TransDataMsgMemAddr`
  作用：把 client 传来的 `mrOffset` 翻译成 server 侧实际可访问的 shm 地址。

### 3.4 JuiceFS hook / VFS / writer 层

- [vfs_bio.go:41](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/vfs_bio.go:41)
  `WriteHookJF`
  作用：BIO 对 JuiceFS 的普通写入口。把 `char*` 转成 `[]byte`，继续走 `MountedVFS.Write`。

- [vfs.go:611](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/vfs.go:611)
  `VFS.Write`
  作用：VFS 层统一写入口。做 handle 查找、范围检查、写锁，再调用 `h.writer.Write`。

- [writer.go:429](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/writer.go:429)
  `fileWriter.Write`
  作用：把一次写拆成 chunk 内多次写，核心循环里每次调用 `writeChunk`。

- [writer.go:385](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/writer.go:385)
  `fileWriter.writeChunk`
  作用：找或建当前 chunk 的可写 slice，再把数据交给 slice 的 writer。

- [cached_store.go:399](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/chunk/cached_store.go:399)
  `wSlice.WriteAt`
  作用：当前前台写真正的重活。它负责 page/block page 的组织，并把数据再拷到 JuiceFS 的 page/block 中。

### 3.5 写路径现在真正慢在哪里

从当前打点看：

- `fileWriter.Write` 里的 `lock` 很小
- `flushWait=0`
- `findSlice/newSlice` 基本不是瓶颈
- 主要时间都在 `writeChunk -> wSlice.WriteAt -> copy`

也就是说，当前 3copy 写前台的本质已经很简单：

1. `user -> interceptor shm` 一次拷贝
2. `interceptor shm -> JuiceFS page/block` 一次拷贝

## 4. 读流程

### 4.1 总体流程图

```text
应用 read/pread
  -> ProxyOperations::Pread / Read
  -> 按大小分流

小读:
  -> PreadSmallInner
  -> SendSync(BIO_OP_INTERCEPTOR_READ)
  -> server HandleInterceptorRead
  -> BioReadHook(resp->data)
  -> JuiceFS ReadHookJF
  -> VFS.Read
  -> fileReader.Read
  -> waitForIO
  -> copy(JuiceFS page -> resp->data)
  -> reply
  -> client memcpy(resp->data -> user buf)

大读:
  -> PreadLargeInner
  -> AllocShmBlock
  -> SendSync(BIO_OP_INTERCEPTOR_LARGE_READ)
  -> server HandleInterceptorLargeRead
  -> TransDataMsgMemAddr(pid, mrOffset)
  -> BioReadHook(shmAddr)
  -> JuiceFS ReadHookJF
  -> VFS.Read
  -> fileReader.Read
  -> waitForIO
  -> copy(JuiceFS page -> interceptor shm)
  -> reply(dataLen only)
  -> client memcpy(interceptor shm -> user buf)
  -> ReleaseShmBlock
```

### 4.2 interceptor client 层

- [interceptor_fileio.cpp:125](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:125)
  `PreadSmallInner(void *buf)`
  作用：小读入口。发同步 IPC，让 server 直接把数据放进响应包。

- [interceptor_fileio.cpp:175](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:175)
  `PreadSmallInner(BufVec &bufVec)`
  作用：`readv/preadv64` 小读版本。server 回包后再 scatter 到 `iov`。

- [interceptor_fileio.cpp:228](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:228)
  `PreadLargeInner(void *buf)`
  作用：大读入口。先申请一块 client shm block，再让 server 把数据直接写到这块 shm。

- [interceptor_fileio.cpp:284](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:284)
  `PreadLargeInner(BufVec &bufVec)`
  作用：`readv/preadv64` 大读版本。server 把数据写进 shm 后，再 scatter 到 `iov`。

### 4.3 interceptor server 层

- [interceptor_server.cpp:98](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/server/interceptor_server.cpp:98)
  `HandleInterceptorRead`
  作用：小读 handler。直接给响应包里的 `resp->data` 填数据。

- [interceptor_server.cpp:390](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/server/interceptor_server.cpp:390)
  `HandleInterceptorLargeRead`
  作用：大读 handler。先翻译 `pid + mrOffset -> shmAddr`，再把数据读到这块 shm。

### 4.4 JuiceFS hook / VFS / reader 层

- [vfs_bio.go:27](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/vfs_bio.go:27)
  `ReadHookJF`
  作用：BIO 对 JuiceFS 的普通读入口。把目标 `char*` 转成 `[]byte`，继续走 `MountedVFS.Read`。

- [vfs.go:547](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/vfs.go:547)
  `VFS.Read`
  作用：VFS 层统一读入口。做 handle 查找、范围检查、读锁，再调用 `h.reader.Read`。

- [reader.go:728](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/reader.go:728)
  `fileReader.Read`
  作用：顶层读函数。做 request 准备、slice 复用/创建、等待数据 ready。

- [reader.go:694](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/reader.go:694)
  `fileReader.waitForIO`
  作用：当前读路径的重活函数。等 slice ready 后，把 `sliceReader.page.Data` 里的数据拷到目标 `buf`。

- [reader.go:401](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/reader.go:401)
  `newSlice`
  作用：为读请求创建新的读 slice 和读 page。

- [reader.go:669](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/reader.go:669)
  `prepareRequests`
  作用：把本次读拆成 req 列表，命中已有 slice 就复用，没命中就新建。

### 4.5 读路径现在真正慢在哪里

从当前打点看：

- `alloc` 只在第一轮高，后面趋近于 0
- `prepare` 很小
- `wait=0`
- 两个大头是：
  - `fileReader.waitForIO copy`
  - client 侧 `PreadLargeInner copy`

所以当前 3copy 大读的本质也是：

1. `JuiceFS page -> interceptor shm` 一次拷贝
2. `interceptor shm -> user buf` 一次拷贝

## 5. 逐层职责总结

### 5.1 interceptor client

核心职责只有两个：

- 决定走小包直发还是 shm 大包
- 做第一跳或最后一跳的数据搬运

也就是：

- 写：`user -> shm`
- 读：`shm -> user`

### 5.2 interceptor server

核心职责也只有两个：

- 收到请求后把控制信息翻译成实际地址
- 调 BIO hook 进入 JuiceFS

它本身不做复杂缓存逻辑，更多是转发和地址桥接。

### 5.3 JuiceFS

核心职责：

- 写：把 `interceptor shm` 里的数据组织进 `wSlice/pages`
- 读：把 `slice/page` 里的数据拷回目标 `buf`

所以当前真正“重”的部分，已经不在协议层，而在 `writer/reader` 里的内存拷贝。

## 6. 哪些代码不是当前主路径

这里说的是“不是当前 3copy 主路径”，不是“死代码”。

### 6.1 IPC data channel 通用 net 逻辑

- [net_connector.cpp:32](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/net/net_connector.cpp:32)
  当前 `CONNECT_IPC` 只建 ctrl channel，不再建 data channel。

- [net_engine.h:417](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/net/net_engine.h:417)
- [net_engine.h:473](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/net/net_engine.h:473)
  这些 `SyncRead/SyncWrite(Get/Put)` 是通用 data-plane 接口。

它们对当前 `interceptor IPC` 主路径不使用，但仍然给 RPC/mirror server 用，所以不能叫死代码。

### 6.2 copy-free 写链路

- [vfs_bio.go:54](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/vfs_bio.go:54)
  `WriteCopyFreeHookJF`
- [vfs.go:673](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/vfs.go:673)
  `WriteWithSpace`
- [writer.go:462](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/writer.go:462)
  `WriteWithSpace`
- [cached_store.go:457](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/chunk/cached_store.go:457)
  `WriteAtCopyFree`
- [cached_store.go:600](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/chunk/cached_store.go:600)
  `uploadCopyFree`

这整条链路不是当前 3copy 主路径，但它还被 copy-free 路径、compact、测试代码使用，所以也不是死代码。

### 6.3 不是“无用代码”的点

下面这些虽然不在每次 I/O 都走，但它们是必要的：

- [interceptor_server.cpp:265](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/server/interceptor_server.cpp:265)
  `HandleInterceptorCreateDataMsgMemPool`
  说明：只在初始化走一次，但大读/大写都依赖它。

- [interceptor_fileio.cpp:175](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:175)
- [interceptor_fileio.cpp:284](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp:284)
  `BufVec` 版本
  说明：对应 `readv/preadv64`，不是死代码。

- [handle.go:182](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/handle.go:182)
  `findFirstHandle`
  说明：这是现在的优化后主路径，不是不必要代码。

## 7. 一句话总结

当前 3copy 主路径非常清楚：

- 写：`user -> interceptor shm -> JuiceFS page`
- 读：`JuiceFS page -> interceptor shm -> user`

真正的前台大头已经不是协议逻辑，而是这两次数据拷贝本身。
