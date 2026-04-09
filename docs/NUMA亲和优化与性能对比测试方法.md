# UBS-IO NUMA亲和优化说明与性能对比测试方法

## 1. 本次改动说明

本次优化目标：
- 让 `bio_daemon` 的线程尽量固定在目标CPU范围运行。
- 让大块缓存内存尽量落在目标NUMA节点，减少远端访存。
- 支持配置化开关，方便灰度、回滚。

已修改文件：
- `ubsio-boostio/src/config/bio_config_instance.h`
- `ubsio-boostio/src/config/bio_config_instance.cpp`
- `ubsio-boostio/configs/bio.conf`
- `ubsio-boostio/src/server/bio_server.cpp`
- `ubsio-boostio/src/common/bio_execution.h`
- `ubsio-boostio/src/net/net_engine.cpp`
- `ubsio-boostio/src/disk/common/bdm_disk.c`

核心能力：
1. 新增 NUMA 配置项
- `bio.numa.enable`：是否启用NUMA优化
- `bio.numa.mem_node`：目标内存节点（-1为不指定）
- `bio.numa.cpu_start`：CPU起始核号（-1为不指定）
- `bio.numa.cpu_span`：可用CPU跨度（0为自动）
- `bio.numa.mem_policy`：内存策略，`preferred` 或 `bind`

2. 服务启动时将NUMA配置写入运行时环境
- 统一透传给线程池、BDM线程池、NetEngine内存池。

3. 通用线程池默认NUMA绑核
- 未显式设置CPU绑定时，按 `cpu_start + cpu_span` 计算线程CPU落点。

4. NetEngine内存池NUMA内存策略
- 对注册的大块内存应用 `mbind` 策略（`preferred/bind`）。

5. BDM线程池默认NUMA绑核
- BDM事件线程与提交线程不再默认 `-1` 漂移，启用NUMA时按配置落核。

---

## 2. 推荐配置

编辑 `/etc/boostio/bio.conf`：

```ini
bio.numa.enable = true
bio.numa.mem_node = 0
bio.numa.cpu_start = 0
bio.numa.cpu_span = 32
bio.numa.mem_policy = preferred
```

建议：
- 先用 `preferred`，稳定后再评估 `bind`。
- `cpu_start/cpu_span` 需要和机器真实拓扑一致（用 `lscpu -e`、`numactl -H` 确认）。

---

## 3. A/B性能对比测试方法

### 3.1 测试原则

- 同一台机器、同一版本代码、同一压测负载、同一时长。
- 只改变 NUMA 开关与参数。
- 每组至少重复 3 次，取平均和P95/P99。

### 3.2 两组配置

- 基线组（A）：`bio.numa.enable = false`
- 优化组（B）：`bio.numa.enable = true`（按推荐配置）

### 3.3 关键指标

业务指标：
- 吞吐（MB/s 或 OPS）
- 时延（P50/P95/P99）

系统指标：
- `numastat -p <bio_daemon_pid>`（重点看远端访存相关项）
- `pidstat -t -p <pid> 1`（线程CPU使用是否更稳定）
- `/proc/<pid>/numa_maps`（页分布是否更集中）

判定方向：
- 吞吐上升、P99下降。
- NUMA远端访存占比下降。

---

## 4. 可执行测试脚本模板

> 说明：将脚本中的 `WORKLOAD_CMD` 替换成你的真实压测命令（业务回放、SDK压测、JuiceFS场景压测均可）。

```bash
#!/usr/bin/env bash
set -euo pipefail

CONF=/etc/boostio/bio.conf
OUT_ROOT=/tmp/ubsio_numa_ab
mkdir -p "$OUT_ROOT"

# 你自己的压测命令（固定并发、固定数据集、固定时长）
WORKLOAD_CMD="/path/to/your_benchmark --duration 600 --threads 64"

set_conf() {
  local enable="$1"
  local mem_node="$2"
  local cpu_start="$3"
  local cpu_span="$4"
  local policy="$5"

  sed -i "s/^bio.numa.enable = .*/bio.numa.enable = ${enable}/" "$CONF"
  sed -i "s/^bio.numa.mem_node = .*/bio.numa.mem_node = ${mem_node}/" "$CONF"
  sed -i "s/^bio.numa.cpu_start = .*/bio.numa.cpu_start = ${cpu_start}/" "$CONF"
  sed -i "s/^bio.numa.cpu_span = .*/bio.numa.cpu_span = ${cpu_span}/" "$CONF"
  sed -i "s/^bio.numa.mem_policy = .*/bio.numa.mem_policy = ${policy}/" "$CONF"
}

restart_service() {
  # 如果你用systemd：
  systemctl restart bio_daemon
  sleep 5
}

collect_env() {
  local out_dir="$1"
  lscpu > "$out_dir/lscpu.txt"
  numactl -H > "$out_dir/numactl_H.txt"
}

run_case() {
  local case_name="$1"
  local out_dir="$OUT_ROOT/$case_name"
  mkdir -p "$out_dir"

  restart_service
  local pid
  pid=$(pidof bio_daemon)

  echo "pid=$pid" > "$out_dir/meta.txt"
  collect_env "$out_dir"

  # 预热
  timeout 120 bash -c "$WORKLOAD_CMD" || true

  # 采集线程和NUMA统计（后台）
  pidstat -t -p "$pid" 1 > "$out_dir/pidstat.txt" &
  local pidstat_bg=$!

  # 正式压测
  timeout 600 bash -c "$WORKLOAD_CMD" > "$out_dir/workload.txt" 2>&1 || true

  numastat -p "$pid" > "$out_dir/numastat.txt" || true
  cat "/proc/$pid/numa_maps" > "$out_dir/numa_maps.txt" || true

  kill "$pidstat_bg" || true
}

# A: 基线组
set_conf false -1 -1 0 preferred
run_case baseline

# B: NUMA优化组
set_conf true 0 0 32 preferred
run_case numa_opt

echo "完成，结果目录：$OUT_ROOT"
```

---

## 5. 结果记录模板

| 指标 | 基线组A | 优化组B | 变化 |
|---|---:|---:|---:|
| 吞吐(MB/s) |  |  |  |
| 平均时延(ms) |  |  |  |
| P99时延(ms) |  |  |  |
| CPU利用率(%) |  |  |  |
| NUMA远端访存相关项 |  |  |  |

建议结论门槛（可按业务调整）：
- 吞吐提升 >= 5%，或
- P99下降 >= 10%，且
- 远端访存指标明显下降。

---

## 6. 回滚方法

将以下配置恢复并重启服务：

```ini
bio.numa.enable = false
bio.numa.mem_node = -1
bio.numa.cpu_start = -1
bio.numa.cpu_span = 0
bio.numa.mem_policy = preferred
```

