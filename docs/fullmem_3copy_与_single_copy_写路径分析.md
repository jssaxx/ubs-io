# fullmem_3copy 与 single-copy 写路径分析

## 结论

- `fullmem_3copy` 前台写延迟更低，不是因为“三次拷贝天然更快”，而是因为前台两次主要拷贝都落在**可复用的缓冲区**上。
- `fullmem-0418` 的 single-copy 路径前台更慢，根因不是 `ipc`，而是那一次拷贝直接写进了**新的、长期占用的 bio 页**，首次触页成本落在同步写调用里。
- 这次在 `JuiceFS4Kunpeng` 新分支上做的优化，不改变语义，只优化普通写路径的前台 page 组织，让 `<4MiB` 的写尽量少拆小 page，少做 upload 前的二次拼装。

## 1. fullmem_3copy 为什么前台更快

### 第一跳：应用 buf -> data-msg shm block

- client 大写路径在 [interceptor_fileio.cpp](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_fileio.cpp)
- 先 `AllocShmBlock`
- 然后 `memcpy(shmAddr, buf, count)`
- 最后发送 `BIO_OP_INTERCEPTOR_LARGE_WRITE`

这一跳写入的是 data-msg shm block pool：

- 分配接口在 [interceptor_net.h](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/client/interceptor_net.h)
- free-list 实现在 [net_block_pool.h](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/net/net_block_pool.h)

这块内存是**反复复用的块池**，不是每次新建永久页。

### 第二跳：data-msg shm -> JuiceFS page pool

- server 侧在 [interceptor_server.cpp:306](/Users/lujiajun/Desktop/codex_project/ubs-io/ubsio-boostio/src/interceptor/server/interceptor_server.cpp:306) 把 shm 地址交给 `BioWriteHook`
- JuiceFS 进入普通写路径：
  - [vfs_bio.go:41](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/vfs_bio.go:41)
  - [vfs.go:611](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/vfs.go:611)
  - [writer.go:425](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/writer.go:425)

普通写最终落到 [cached_store.go:327](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/chunk/cached_store.go:327) 的 `wSlice.WriteAt`。

这里写入的是 `wSlice.pages`：

- 标准 page 大小是 `64KiB`
- `64KiB` page 通过 [cached_store.go:255](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/chunk/cached_store.go:255) 的 `pagePool` 复用

也就是说，前台第二跳通常也是写进**可复用 page**。

### 第三跳：upload 前拼装大块

- upload 阶段在 [cached_store.go:523](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/chunk/cached_store.go:523)
- 如果一个 block 里有多个小 page，会 `NewOffPage(blen)` 再把这些 page 拼成一个大块

这一跳会触发新的大块页分配，但它发生在 **异步 upload 线程**，不在当前前台写调用的同步时延里。

所以 `fullmem_3copy` 更快的真正原因是：

1. 前台第一跳写复用的 data-msg shm block
2. 前台第二跳写复用的 page pool
3. 真正的新大页主要出现在异步 upload 阶段

## 2. single-copy 为什么前台更慢

`fullmem-0418` 的 copy-free 路径里：

1. 先 `ALLOC_CACHE_SPACE`
2. 拿到真正的 `CacheSpaceDesc`
3. client 直接把应用 buf 拷进这块最终会长期占用的 bio 页
4. 再发 `LARGE_WRITE`

问题在于这块 bio 页不是临时缓冲区，而是后续会被长期占用的真实缓存页。

所以那一次拷贝承担了：

- 新页首次触达
- 页表建立
- cache miss
- 冷页写入

这就是为什么 single-copy 路径虽然“只拷一次”，前台仍然比 `3copy` 慢。

## 3. 哪些拷贝在复用内存

### fullmem_3copy

第一跳：

- `buf -> data-msg shm block`
- 复用对象：`NetBlockPool`

第二跳：

- `data-msg shm -> wSlice.pages`
- 复用对象：`pagePool` 的 `64KiB` page

第三跳：

- `wSlice.pages -> upload block`
- 这里通常会新建大块页
- 但这一步主要在异步 upload 中完成

### fullmem-0418 single-copy

唯一那次主要拷贝：

- `buf -> CacheSpaceDesc 指向的 bio 页`
- 这块页是新分配且长期占用的
- 不会像前两类缓冲区那样立即回收到复用池

## 4. 这次在 JuiceFS 新分支上的改造

分支：

- `opt/fullmem-3copy-write-path`

改动点 1：

- 新增 `findFirstHandle`
- `ReadHookJF/WriteHookJF/WriteCopyFreeHookJF` 不再每次 `findAllHandles(...)[0]`
- 代码位置：
  - [handle.go](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/handle.go)
  - [vfs_bio.go](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/vfs/vfs_bio.go)

改动点 2：

- 优化普通写 `wSlice.WriteAt`
- 当首块累计写入已经明显变大时，直接把首块提升成一个整 block page
- 避免首块被拆成很多 `64KiB` page，后面 upload 再拼一次
- 代码位置：
  - [cached_store.go](/Users/lujiajun/Desktop/codex_project/JuiceFS/pkg/chunk/cached_store.go)

这个改动对 `<4MiB` 的写都能适配：

- 小写仍然可以继续走原来的小 page 路径
- 中等和较大的写会更早切到整 block page
- 语义不变，不引入空洞

## 5. 这次改造想解决什么

不再追求把 `3copy` 强行改成 single-copy。

现在优化目标更明确：

- 让 `3copy` 的前台热路径继续保持“复用缓冲区优先”
- 尽量减少普通写路径里“前台拆小 page，后台再拼大块”的额外成本

## 6. 当前判断

- 如果数据最终必须长期占用 bio 页，那么 single-copy 前台天然更容易吃到冷页成本。
- `3copy` 的优势不在“拷贝多”，而在“前台主要使用复用缓冲区”。
- 所以后续更值得优化的是 `3copy` 普通写路径的 page 组织方式，而不是继续执着于单次拷贝。
