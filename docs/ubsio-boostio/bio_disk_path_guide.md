# bio.disk.path 配置指南

`bio.disk.path` 是 UBSIO 的磁盘配置项，用于指定三级池化缓存层使用的块设备或分区，多个路径以冒号（`:`）分隔，最多 **16** 条。

推荐使用 `partition_disks.sh` 一键完成分区并输出配置。


**[partition_disks.sh](https://gitcode.com/openeuler/ubs-io/blob/develop/scripts/ubsio-boostio/partition_disks.sh)脚本下载：**

```bash
wget -O /path/to/partition_disks.sh https://raw.gitcode.com/openeuler/ubs-io/blobs/b81de180c6a15059a28155f17c8c96f502ef70cf/partition_disks.sh
chmod +x /path/to/partition_disks.sh
```

## 场景一：有盘部署（推荐）

**背景：** 单节点多块 NVMe 盘，分区总数需提前规划，每个分区对应一个推理实例的缓存存储路径。

### 1. 确认磁盘状态

通过以下命令确认哪些磁盘为可用裸盘（无分区、无挂载、无文件系统签名）。

```bash
# 1. 列出所有磁盘，确认哪些是裸盘（无分区即为裸盘）
lsblk

# 输出示例：
# NAME        MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS
# nvme0n1     259:0    0   3.5T  0 disk              # <-- 裸盘，可用
# nvme1n1     259:1    0   3.5T  0 disk              # <-- 裸盘，可用
# nvme2n1     259:2    0   1.8T  0 disk
# +-nvme2n1p1 259:3    0   1.8T  0 part /data        # <-- 已分区挂载，不可用

# 2. 确认目标盘为裸盘（以 /dev/nvme0n1 为例，替换为实际磁盘）
lsblk /dev/nvme0n1                    # 应无分区
mount | grep nvme0n1                  # 应无挂载
sudo blkid /dev/nvme0n1               # 应无文件系统签名
```

### 2. 使用 partition_disks.sh 分区

> <span style="color:red"> ⚠️ 高危操作： 选错磁盘将造成不可逆的数据丢失。请务必在执行前通过 `lsblk` 反复确认目标磁盘路径。</span>

脚本创建 GPT 分区表，均分磁盘并输出 `bio.disk.path`。默认 dry-run，加 `--yes` 才写入。

### 2.1 单推理实例

`--parts` 一般与节点卡数一致。如果有多块盘，建议 `--parts` 设为卡数 × 盘数（`-c` 仍填卡数），以优化性能。

**示例：1 个推理实例 4 张卡，1 块盘**

```bash
bash /path/to/partition_disks.sh --parts 4 --disk /dev/nvme0n1 -f

sudo bash /path/to/partition_disks.sh \
    --parts 4 --disk /dev/nvme0n1 --yes -f -w

# 输出：
# bio.disk.path = /dev/nvme0n1p1:/dev/nvme0n1p2:/dev/nvme0n1p3:/dev/nvme0n1p4
```

**`vim /etc/boostio/bio.conf`，增加或修改下述配置：**

```
bio.disk.path = /dev/nvme0n1p1:/dev/nvme0n1p2:/dev/nvme0n1p3:/dev/nvme0n1p4
```

**示例：1 个推理实例 4 张卡，2 块盘（`--parts = 卡数 × 盘数 = 8`）**

```bash
sudo bash /path/to/partition_disks.sh \
    --parts 8 --disk /dev/nvme0n1 --disk /dev/nvme1n1 -c 4 --yes -f -w

# 输出：
# bio.disk.path = /dev/nvme0n1p1:/dev/nvme0n1p2:/dev/nvme0n1p3:/dev/nvme0n1p4:/dev/nvme1n1p1:/dev/nvme1n1p2:/dev/nvme1n1p3:/dev/nvme1n1p4
```

**`vim /etc/boostio/bio.conf`，增加或修改下述配置：**

```
bio.disk.path = /dev/nvme0n1p1:/dev/nvme0n1p2:/dev/nvme0n1p3:/dev/nvme0n1p4:/dev/nvme1n1p1:/dev/nvme1n1p2:/dev/nvme1n1p3:/dev/nvme1n1p4
bio.standalone.device_count = 4
```

### 2.2 多推理实例

`--parts` 一般与节点卡数一致。多实例容器隔离，独立配置文件。

**示例：8 分区，2 个推理实例各占 4 张卡，2 块盘**

```bash
sudo bash /path/to/partition_disks.sh \
    --parts 8 \
    --disk /dev/nvme0n1 \
    --disk /dev/nvme1n1 \
    --yes -f -w

# 输出全部分区：
# bio.disk.path = /dev/nvme0n1p1:/dev/nvme0n1p2:/dev/nvme0n1p3:/dev/nvme0n1p4:/dev/nvme1n1p1:/dev/nvme1n1p2:/dev/nvme1n1p3:/dev/nvme1n1p4
```

**`vim /etc/boostio/bio.conf`（实例 0），增加或修改下述配置：**

```
bio.disk.path = /dev/nvme0n1p1:/dev/nvme0n1p2:/dev/nvme0n1p3:/dev/nvme0n1p4
```

**`vim /etc/boostio/bio.conf`（实例 1），增加或修改下述配置：**

```
bio.disk.path = /dev/nvme1n1p1:/dev/nvme1n1p2:/dev/nvme1n1p3:/dev/nvme1n1p4
```

### 参数说明

| 参数 | 说明                                     |
|------|----------------------------------------|
| `-n, --parts N` | 总分区数，一般与卡数一致；单推理实例多盘时可为卡数 × 盘数；最大不超过64 |
| `-d, --disk PATH` | 磁盘路径                                   |
| `-c, --dev-cnt N` | 占用的卡数；单节点多推理实例场景可不填（默认 0）              |
| `-y, --yes` | 实际写入分区表                                |
| `-f, --force` | 覆盖已有分区表                                |
| `-w, --wipe-metadata` | 擦除旧文件系统签名和 BDM 头                       |

---

## 场景二：模拟盘部署（loop 设备方案）

无可用裸盘时，用 `dd` + `losetup` 创建 loop 块设备模拟，默认开启 direct_io 直通模式。

> <span style="color:red">**⚠️ 高危操作：** 创建 loop 设备前，务必确认目标 `/dev/loopN` 未被占用，否则可能导致数据丢失。</span>

先查看哪些 loop 设备可用：

```bash
# 1. 查看系统有哪些 loop 设备
ls /dev/loop*

# 输出示例：
# /dev/loop0  /dev/loop1  /dev/loop2  ...  /dev/loop7

# 2. 查看已占用的 loop 设备
losetup -a

# 输出示例：
# /dev/loop1: [2049]:123456 (/data/other.img)   # <-- 已被占用
# （若无输出则表示所有 loop 设备均空闲）
```

确认空闲后，再执行以下操作（假设 /dev/loop0 空闲，创建 1TB 镜像）：

```bash
# 创建 1TB 镜像文件（count=1024，可根据需求调整）
sudo dd if=/dev/zero of=/data/boostio_disk.img bs=1G count=1024 status=progress

# 挂载 loop 设备，默认开启 direct_io 直通
sudo losetup --direct-io=on /dev/loop0 /data/boostio_disk.img

# 验证 direct_io 已开启
losetup -l /dev/loop0 | grep -i direct
# 期望输出包含：Direct I/O: on

# 分区
sudo bash /path/to/partition_disks.sh \
    --parts 4 --disk /dev/loop0 -c 4 --yes -f -w

# 输出：
# bio.disk.path = /dev/loop0p1:/dev/loop0p2:/dev/loop0p3:/dev/loop0p4
```

**`vim /etc/boostio/bio.conf`，增加或修改下述配置：**

```
bio.disk.path = /dev/loop0p1:/dev/loop0p2:/dev/loop0p3:/dev/loop0p4
```

---

## 场景三：无盘部署

当不配置 SSD 缓存层时，`bio.disk.path` 保持为空即可，无需执行分区操作。

```
# bio.disk.path 留空，不启用磁盘缓存
bio.disk.path =
```

> 不配置磁盘时，UBSIO 仅使用内存作为缓存，重启后缓存数据不持久化。

---

## 取消 loop 设备

> **⚠️ 注意：** 仅在确认不再需要 loop 模拟盘时执行，卸载后缓存数据将丢失。

```bash
# 1. 查看当前 loop 设备及其关联文件
losetup -a

# 2. 卸载 loop 设备（假设要卸载的是 /dev/loop0）
sudo losetup -d /dev/loop0

# 3. 删除镜像文件（可选）
sudo rm -f /data/boostio_disk.img
```

---

## 注意事项

| 事项 | 说明 |
|------|------|
| 磁盘独占 | UBSIO / BoostIO 独占配置的磁盘/分区，不可与其他服务共享 |
| 物理盘隔离 | 同一物理盘的分区不会分配给同一 deviceId（算法自动打散） |
| 路径上限 | `bio.disk.path` 最多 16 条路径 |
| 动态加盘 | standalone 模式不支持运行时加盘 |
| 复用磁盘 | 不同 deviceId 会写入 BDM disk head，不匹配将拒绝恢复 |
| 权限 | 进程需对块设备有读写权限，建议 root 运行 |
