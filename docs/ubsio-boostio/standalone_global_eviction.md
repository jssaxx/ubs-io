# Standalone WCache 全局淘汰

WCacheManager 统一管理 standalone 普通后台淘汰。Manager 选择 Flow，WCache 执行有限批次，WCacheTier 保留原有 Slice 队列和每 Flow 的 truncate 游标。公共 C/Python API、配置项和磁盘元数据布局不变。cluster 模式及已有强制 Flush/ExpiredClear 使用原有入口。

## 调度规则

有盘模式按磁盘组织最终淘汰，每盘同一时刻最多执行一个 Manager 批次，不同盘可并发：

1. 恢复 Flow、加盘/rejoin 后标为只读的 Flow，按进入历史队列的顺序处理。
2. 存在历史 Flow 时只选择历史队头。队头失败、忙碌、等待 reader 或延迟回调时不越过它。
3. 每批至多处理 32 条 Slice；正常对象和 tombstone 均计入预算。
4. 每处理一条都重新检查原有水位，水位达标立即停止。**选中历史 Flow 不意味着强制排空整个 Flow。**
5. 历史队头还未排空时留在原位置，下次需要淘汰继续处理它。
6. 只有队头数据、worker、已准入 I/O 和延迟回调全部收敛，完成 WCache/Flow 退役后，才切换到下一个历史 Flow。
7. 历史队列为空时，在符合当前 PT 的活跃 Flow 之间按 flowId 游标轮转；一个 Flow 处理一批后，下一批从后续 Flow 开始。

活跃判定使用可写状态、完整 PT 版本、所属磁盘、本地主节点和正常 PT 状态。恢复 Flow 即使与当前 PT 编码相同，也仍为历史 Flow。flowId 中截断后的 PT 版本不能代替完整版本比较；同一 PT 代际的多个合法 Flow 都参与轮转。

历史队列由 Flow 生命周期入口维护：`RecoverCache()` 在恢复成功后入队；`MarkPtFlowsReadOnly()` 在同一个 Manager 锁内完成只读标记和入队，加盘/rejoin 及创建 Flow 时发现 PT 版本变化均复用此入口。全局淘汰轮次只选择候选，不再扫描全部 Flow 重新分类；后续新增只读转换路径也必须同步完成历史队列登记。

FIFO 的顺序是运行时进入历史队列的顺序。恢复时沿已有扫描顺序入队，同一次 PT 转换中用 flowId 排序作为稳定的并列次序；没有新增跨重启时间戳，因此不保证重启前跨 Flow 的精确写入时间排序。

## 内存与磁盘

有盘模式的内存下刷单独在全部可处理 Flow 间轮转，沿用全局内存水位和目标盘容量检查。内存到磁盘仍使用同一个 SliceRef，不产生最终 DELETE。

磁盘最终淘汰使用所属盘水位，不受其他盘空闲容量稀释。历史队头如果仍有内存数据或延迟下刷，保留队头等待该 Flow 的正常下刷收敛，不强制 Flush，也不越过队头删除后续历史 Flow。严格 FIFO 因此可能等待队头的内存下刷、I/O 或 reader。

无盘模式将 MEMORY 作为最终淘汰层，应用相同的历史 FIFO、活跃轮转和内存水位停止规则。

无盘且启用 UnderFS 时，保留 MEMORY 直接写入 UnderFS 的路径；PENDING Slice 等待索引发布后才允许最终淘汰。该路径使用紧凑元数据，truncate 游标初始化和元数据截断偏移均按对应 tier 的实际记录大小计算。

## 触发与并发

- 新写入的 WCache SliceRef 在内存写完后、入队前调用现有 `WCacheIndex::Insert()`。插入成功即 `SLICE_VALID`，完整内存数据立即对 Get/Exist 可见；重复 key 保留旧索引，新引用转 `SLICE_INVALID`。INVALID 对象在入队前设置内存 `hasEvict=1`，有效对象不重写 0；失败对象通过 tombstone 清理其 Flow 区间。RCache 的默认 VALID 状态不变。
- 普通 Put 不再把 PENDING 对象加入淘汰队列，同步路径保留队头推进语义，不保证本次 Slice 到达目标介质。同步 UnderFS 不再需要等待索引发布的 `deferred` 分支。恢复等入口遇到 PENDING 时仍返回重试，不跳过队头；reader 未释放导致的延迟 Slice 切换仍保留。
- MEMORY → DISK 切换回调在释放旧内存前读取旧 metadata，必要时将 SSD `hasEvict` 补写为 1，不将 1 改回 0。标记读取或补写失败只记录 warning，继续回收旧内存并加入磁盘队列，不保留完成上下文或新增补写重试；该缓存策略接受后续恢复读到过期记录。同步和后台搬运均与 Delete 使用 Slice 操作锁协调。
- Put 入队及下刷完成后继续请求 MEMORY/DISK 调度。已发布对象不会因其他队头下刷失败而撤销；最终淘汰不要求整个 Flow 的 Put 在途计数归零。
- 原有下刷完成和 tombstone 入口通过 WCache 注册的回调请求 Manager 调度。
- scheduled/pending 合并重复触发；任务提交失败保留 pending。
- 批次有进展后请求下一轮，由下一轮重新检查水位；失败由原重试线程延后处理。
- 完成全部启动恢复、发布 Cache 初始化完成状态后才启用调度，避免扫描与淘汰交错，并确保故障处理已能接管恢复 Flow。
- 原重试线程每秒重试失败请求，并检查历史队头是否可以退役，使最后一个 reader 退出后不依赖新的 Put 才能回收空 Flow。
- 队列只保存 flowId；worker 临时持有 WCache 引用。任务准入与故障标记共用 Manager 锁；故障标记后不再准入普通批次。`FlyIo` 只统计 Put，覆盖索引发布、入队及本次队头推进；全局淘汰使用独立的 `mEvictOnFlyRef[tier]`，不影响 Put 完成判断。
- 每次淘汰批次准入时增加对应 tier 的在途计数，成功、失败或异常退出均归还。失败 Slice 放回队头并登记重试后，本批次归还计数；等待重试期间不占计数，下次准入重新计数。
- PT 视图在取得 Manager 锁之前复制，只用于活跃候选检查，不在淘汰轮次中改变 Flow 的读写状态。

## 回收与故障

恢复 truncate 游标从 Meta Flow 已释放位置后的完整记录开始。magic 不匹配或 data 范围无效的记录登记为已处理空洞，以便连续截断跨过这些索引。截断仍按每 Flow 独立推进，空间释放仍按完整 segment 执行。

空历史 Flow 的删除条件继续包含内存/磁盘队列、worker、在途 I/O 及引用计数。暂时不能删除返回可重试结果，不能把“暂未删除”当作销毁任务成功。

故障盘仍由 CleanupFaultedDiskFlows 处理；收集或注销故障 Flow 时移除对应历史队列 ID。故障清理不依赖普通读取/水位淘汰完成，也不会把故障 Flow 重新放回队列。

禁止新 Put 和普通淘汰批次后，故障线程在 Manager 锁外同时等待 Put 与 MEMORY/DISK 批次在途计数归零，每 100 ms 检查一次、每个 Flow 等待约 1 秒；超时返回 `BIO_INNER_RETRY`，保留故障处理中状态以阻止 rejoin。等待的是已准入批次退出，不要求重试队列清空或故障盘下刷成功。`mEvictRef` 仍用于 worker 互斥，不能代替独立在途计数；延迟 Slice 回调仍由引用计数保护，批次归零不表示回调全部完成。旧 Flush/ExpiredClear 路径不纳入普通全局批次计数。

元数据删除继续复用 SliceRef 延迟回调、WCacheIndex 删除及共享事件 batch。watermark 达标、Slice 被取出、reader 退出、segment 释放和整个 Flow 销毁是不同完成时点。

Delete 成功不承诺立即崩溃后的重启安全性：迁移期间的删除标记可能仍在等待最后一个 reader，补写失败后也不保证最终同步成功。发布顺序、兼容性和失败处理详见 [WCache 内存发布与删除标记同步设计](wcache_publication_and_delete_markers.md)。

## 代码与验证入口

- `src/cache/write/wcache_manager.cpp/.h`：历史队列、活跃候选检查、任务合并、轮转、重试和退役。
- `src/cache/write/wcache.cpp/.h`：正常调度回调、批次准入、水位循环、任务在途计数。
- `src/cache/write/wcache_tier.cpp/.h`：恢复游标起点、空洞与连续截断。
- `src/server/bio_server.cpp`：全部恢复完成并发布 Cache 就绪状态后启用调度。
- `test/llt/unit-tests/cache/write/test_wcache_global_evict.cpp`：水位停止与继续队头、活跃轮转、队头失败/reader 延迟、PT 代际、迁移、按盘水位、故障准入、tombstone 预算、无盘模式及重复请求合并。

验证使用模拟元数据和 allocator 回调，不操作真实磁盘。设备故障、实际 SSD 持久化恢复和压力性能仍需在专用环境验证。
