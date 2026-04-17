# 读写 Copy-Free 改造总结

## 1. 目标

本轮改造的目标是把 `interceptor -> JuiceFS -> ubs-io put/get` 的主路径尽量收敛成：

- 写：
  应用 `buf/iov` -> `bio shm` 拷贝一次
- 读：
  服务端/JuiceFS -> `shm`，客户端再从 `shm` 拷贝到应用 `buf/iov` 一次

其中“只拷一次”是按当前用户态链路来定义，不包含底层设备、RDMA、内核态细节。

## 2. 已完成修改

### 2.1 写路径

- `write/pwrite/pwrite64` 统一收敛到 `copy-free` 主路径。
- `writev/pwritev/pwritev64` 不再先拍平到临时平坦 buffer，而是直接把 `iov` 散拷到 `bio shm`。
- 大于 `4M` 的写会在客户端按段拆分，每段单独申请 `cache space` 并提交。
- 服务端增加了 `spaceInfo` 地址范围与长度校验，避免非法地址或长度不一致。
- JuiceFS `WriteWithSpace` 支持跨 `meta.ChunkSize` 拆成多个子视图，不再因为跨 chunk 把数据重新复制进 JuiceFS page。

### 2.2 读路径

- `read/pread/pread64` 统一走 `BIO_OP_INTERCEPTOR_LARGE_READ + shm`。
- `readv/preadv64` 直接从 `shm` 散写回 `iov`，不再先读到临时平坦 buffer。
- 大于单个 data-message shm block 的读会按 block size 分段读取。
- 服务端 `LARGE_READ` 修正了 EOF 语义，允许 `readLen == 0` 作为正常文件尾返回。

### 2.3 运行时安全性

- 读路径不再硬编码 data-message shm block 为 `4M`，改为使用运行时返回的 `segment` 大小。
- 写路径和读路径都保留了长度/地址校验，避免直接把非法 `spaceInfo` 或异常 `dataLen` 继续下传。

## 3. 当前路径判断

### 3.1 写路径是否做到主路径一次拷贝

结论：在当前用户态主路径上，基本做到了。

当前写主路径是：

1. 应用 `buf/iov` 拷贝到 `bio shm`
2. 服务端把 `spaceInfo` 翻译成真实地址
3. JuiceFS `WriteWithSpace` 持有这段 `spaceInfo`
4. `PutWithSpaceNew -> BioPutWithCopyFree` 直接引用这段空间

在这条链路上，`interceptor` 和 JuiceFS 不再把数据重新 materialize 成普通 page 再上传。

### 3.2 读路径是否做到主路径一次拷贝

结论：客户端侧做到了，但 JuiceFS 内部还没有做到“绝对一次拷贝”。

当前读主路径是：

1. 服务端/JuiceFS 把数据写到 data-message `shm`
2. 客户端从 `shm` 拷贝到应用 `buf/iov`

但 JuiceFS 内部读链路仍然会经过 `slice/page` 缓冲，再通过 `copy(buf, s.page.Data[...])` 拷到目标缓冲区，所以：

- `interceptor client` 侧只剩一次拷贝
- `JuiceFS reader` 内部仍存在一次 `page -> target buf` 拷贝

因此读路径目前不能说是“端到端只有一次拷贝”。

## 4. 正确性检查结论

### 4.1 静态检查确认没问题的部分

- 大于 `4M` 的读写都按偏移顺序分段，偏移累加逻辑正确。
- `writev/readv` 保持 `iov` 顺序，没有乱序拼接。
- EOF 语义在 `LARGE_READ` 上已经修正，`readLen == 0` 不再被误判为错误。
- data-message shm 的分段长度已经改为运行时 block size，不再依赖固定常量。
- `git diff --check` 已通过，没有明显格式或空白问题。

### 4.2 当前仍然存在的主要风险

#### 风险 1：跨 chunk 的 copy-free 写，静态上无法完全证明安全

当前 `WriteWithSpace` 跨 `meta.ChunkSize` 的实现，是把一个 `spaceInfo` 切成多个子视图，分别交给多个 JuiceFS slice。

这个实现的前提是假设：

- `BioPutWithCopyFree` 接受“同一份 descriptor / location 的子区间写”
- 并且允许多个 JuiceFS slice 复用同一底层 descriptor 的不同字节区间

从现有静态代码无法完全证明这个假设一定成立。

换句话说：

- 不跨 chunk 的 copy-free 写，当前逻辑基本成立
- 跨 chunk 的 copy-free 写，仍然需要实际回写验证

建议必须补一组运行时验证：

- 连续写跨 `64MiB` 边界
- 再顺序读回做字节级比对
- 再做 `writev/pwritev` 跨 `64MiB` 边界校验

#### 风险 2：读路径还不是端到端一次拷贝

JuiceFS `reader.go` 内部仍然通过 page cache / `copy` 把数据拷到目标缓冲区。

因此当前只能说：

- `interceptor` 层已经统一到 `shm`
- 但 JuiceFS 内部读链路还没改成“直接写目标缓冲区”

## 5. 后续可继续优化的点

### 5.1 优先级最高

- 给跨 `64MiB` 的 copy-free 写补运行时回归验证，确认 descriptor 子视图语义是否真的成立。

### 5.2 读路径进一步降拷贝

- 在 JuiceFS `reader` 增加“直接读到目标 buffer”的能力，尽量绕过 `page -> buf` 这次 copy。
- 如果底层对象读取接口允许，也可以增加“直接读到 shm 地址”的 reader 分支。

### 5.3 清理项

- 老的 `PreadSmallInner/PreadLargeInner` 现在已经不是主路径，可在验证完成后清理。
- 服务端 `HandleInterceptorRead` 也不再是主路径，可在验证完成后考虑降级或移除。
- 客户端未使用的辅助函数可以在稳定后再清理，当前先保留，避免影响回滚和对比。

## 6. 本轮结论

### 写

- 主路径已经基本收敛到一次拷贝
- 但跨 chunk 的 `spaceInfo` 子视图语义仍需要运行时证明

### 读

- `interceptor` 层已经统一走 `shm`
- 客户端侧只剩 `shm -> buf/iov` 一次拷贝
- JuiceFS 内部读链路仍有额外一次 `page -> buf` 拷贝

### 验证现状

- 已完成静态代码检查
- 未完成编译和真实读写回归验证

