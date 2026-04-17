# 读写 Copy-Free 改造总结

## 目标

本轮改造的目标是把 `interceptor -> JuiceFS -> ubs-io` 的主路径尽量收敛成：

- 写：应用 `buf/iov -> bio shm` 只拷一次
- 读：服务端/JuiceFS 直接写 `shm`，客户端只做 `shm -> 应用 buf/iov` 一次拷贝

这里的“一次拷贝”只按当前用户态链路定义，不包含内核态和更底层设备路径。

## 已完成改动

### 写路径

- `write/pwrite/pwrite64` 统一走 `copy-free` 主路径。
- `writev/pwritev/pwritev64` 不再先拍平成临时 buffer，而是直接把 `iov` 散拷到 `bio shm`。
- 超过 `4M` 的写会在客户端按段拆分，每段单独申请 `cache space` 并提交。
- 服务端会校验 `spaceInfo` 的地址范围和总长度，防止非法地址直接下传。
- JuiceFS `WriteWithSpace` 支持跨 `meta.ChunkSize` 把同一段 `spaceInfo` 切成多个子视图继续下传。

### 读路径

- `read/pread/pread64` 统一走 `BIO_OP_INTERCEPTOR_LARGE_READ + shm`。
- `readv/preadv64` 直接从 `shm` 散写回 `iov`，去掉了临时平坦 buffer。
- 服务端新增 `BioReadCopyFreeHook` 路径，把现有 data-message shm 包装成 `spaceInfo`，直接传给 JuiceFS。
- JuiceFS 新增独立的 `ReadWithSpace` / `ReadAtWithSpace` 旁路，不再复用原来的 `page.Data -> copy(buf)` 读路径。
- 当 chunk 启用了压缩时，新读旁路会显式回退旧路径，不强行走 copy-free。

### 本轮补上的读路径缺口

- 客户端读分段长度改成 `min(data-message shm block size, 4M)`，避免运行时 block size 大于协议上限时被服务端拒绝。
- 服务端大读入口新增 `mrOffset + nbytes` 的完整 shm 范围校验，不再只检查起始地址。
- 服务端大读在 `RET_CACHE_NOT_SUPPORTED` 时自动回退旧 `BioReadHook`。
- `LARGE_READ` 保留了 `readLen == 0` 表示 EOF 的语义，不再把 EOF 当错误。

## 当前读写主路径

### 写

当前写主路径是：

1. 应用 `buf/iov` 拷贝到 `bio shm`
2. `interceptor server` 翻译 `spaceInfo`
3. JuiceFS `WriteWithSpace`
4. `PutWithSpaceNew -> BioPutWithCopyFree`

结论：当前用户态主路径上，写已经基本收敛成一次拷贝。

### 读

当前读主路径是：

1. `interceptor client` 申请 data-message shm block
2. `interceptor server` 把这段 shm 包装成 `spaceInfo`
3. JuiceFS `ReadWithSpace -> ReadAtWithSpace`
4. 底层对象存储/磁盘缓存直接把数据写到 shm
5. 客户端从 shm 拷回应用 `buf/iov`

结论：

- 非压缩 chunk 的读主路径，已经绕过了 JuiceFS 原来的 `page.Data -> buf` 那次拷贝
- 压缩 chunk 会显式回退旧路径

所以现在可以说：

- 写主路径基本做到一次拷贝
- 读主路径在非压缩场景下也基本做到一次拷贝
- 压缩读场景还不是一次拷贝

## 从 interceptor 重新检查后的结论

### 已确认正确的点

- 客户端读写大包都会按顺序分段，偏移累加逻辑正确。
- `readv/writev` 维持原始 `iov` 顺序，没有重排。
- 大读返回的 `dataLen` 会在客户端再次校验，防止超过本次请求长度。
- 服务端读路径现在会校验整段 shm 范围，避免 `mrOffset` 合法但 `mrOffset + len` 越界。
- 运行时 data-message block size 已经和协议上限对齐，不会再因为 block 太大而被 server 拒绝。
- 新读旁路只挂在大读路径上，没有重构普通 JuiceFS reader。

### 仍然保留的限制

#### 1. 压缩读会回退旧路径

这是刻意保守处理。

原因是当前 `ReadAtWithSpace` 旁路直接把对象存储/缓存内容写到 shm，没有再经过原来的解压和 page 体系；如果压缩开启，强行走新路径会直接破坏数据语义。

所以当前策略是：

- 非压缩：走 copy-free 读旁路
- 压缩：返回 `not supported`，回退旧读路径

#### 2. 跨 chunk 的 copy-free 写仍需实测

当前写路径跨 `64MiB` chunk 时，是把一个 `spaceInfo` 切成多个子视图分别交给多个 slice。

静态代码无法完全证明底层 `BioPutWithCopyFree` 一定支持“同一 descriptor/location 的子区间写”。

所以：

- 不跨 chunk 的 copy-free 写，当前逻辑基本成立
- 跨 chunk 的 copy-free 写，仍然需要真实写回并读回做字节级校验

## 建议的最小验证

建议至少补下面几组回归：

1. 非压缩场景下的大读顺序校验
2. 非压缩场景下的 `readv/preadv64` 校验
3. 跨 `64MiB` 的 copy-free 写后再读回做字节对比
4. 压缩开启时确认大读确实回退旧路径且数据一致

## 静态检查现状

- `git diff --check` 已通过
- 当前环境没有 `go` 命令，所以没做 JuiceFS 的包级编译验证
- 还没有做真实读写回归

## 结论

- 写路径当前用户态主路径已经基本做到一次拷贝
- 读路径新增了一套独立 copy-free 旁路，非压缩场景已经基本做到一次拷贝
- 这次重新检查后补上了两个实际缺口：读分段大小和 shm 全范围校验
- 当前最大的剩余风险不是读，而是跨 chunk copy-free 写的真实语义还需要实测
