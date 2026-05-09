
# UBSIO-KV 使用指导

> 安装部署：[安装指南](install_guide.md)

---

## 1. 配置项

### 1.1 memcache 配置

#### meta_service 配置

修改配置文件：`/usr/local/memcache_hybrid/latest/config/mmc-meta.conf`

```bash
# 启用 UBSIO-KV
ock.mmc.ubs_io.enable = true
```

#### local_service 配置

修改配置文件：`/usr/local/memcache_hybrid/latest/config/mmc-local.conf`

```bash
# 启用 UBSIO-KV
ock.mmc.ubs_io.enable = true
```

### 1.2 BIO 配置

BIO 配置项参考：[UBS-IO 特性指南](../../ubsio-boostio/doc/UBS-IO%20特性指南.md)（表5）

---

## 2. 服务启动

> ⚠️ **请严格遵循以下启动顺序**

### 步骤 1：启动 Zookeeper

```bash
# 一般会安装到 /etc/zookeeper/ 或 /usr/local/zookeeper/ 目录下
cd /path/to/zookeeper-install-path/bin
./zkServer.sh start
```

### 步骤 2：启动 Boostio

```bash
# ⚠️ **建议启动Boostio前清理下缓存**
# device 对应的是 Boostio 中配置的 bio.disk.path
dd bs=8k count=1024 if=/dev/zero of=<device>
/path/to/zkCli.sh -server localhost:2181 deleteall /cm || true
```

```bash
nohup /etc/boostio/bin/bio_daemon > /var/log/boostio/nohup.out 2>&1 &
```

日志文件：`/var/log/boostio/bio.log`

### 步骤 3：启动 Meta Service

```bash
source /usr/local/memfabric_hybrid/set_env.sh
source /usr/local/memcache_hybrid/set_env.sh
export MMC_META_CONFIG_PATH=/usr/local/memcache_hybrid/latest/config/mmc-meta.conf
nohup /usr/local/memcache_hybrid/latest/aarch64-linux/bin/mmc_meta_service > mmc.log 2>&1 &
```

---

## 3. vLLM 示例

### 3.1 单机部署

> 详细配置参考：[Ascend Store Deployment Guide](https://docs.vllm.ai/projects/ascend/en/latest/user_guide/feature_guide/kv_pool.html)

```bash
NIC_NAME="eth0"
LOCAL_IP="127.0.0.1"
MASTER_IP="127.0.0.1"
MODEL_PATH="/data/models/Qwen3-32B-W8A8"

export HCCL_IF_IP=$LOCAL_IP
export GLOO_SOCKET_IFNAME=$NIC_NAME
export TP_SOCKET_IFNAME=$NIC_NAME
export HCCL_SOCKET_IFNAME=$NIC_NAME

export PYTHONHASHSEED=0
export HCCL_BUFFSIZE=1024
export OMP_PROC_BIND=false
export OMP_NUM_THREADS=10
export PYTORCH_NPU_ALLOC_CONF=expandable_segments:True
export VLLM_USE_V1=1

source /usr/local/memcache_hybrid/set_env.sh
source /usr/local/memfabric_hybrid/set_env.sh
export MMC_LOCAL_CONFIG_PATH=/usr/local/memcache_hybrid/latest/config/mmc-local.conf

rm -rf ./connector.log
rm -rf ~/ascend/log/*
rm -rf /var/log/memfabric_hybrid/ptracer_*
rm -rf /dev/shm/*

nohup vllm serve ${MODEL_PATH} \
  --port 8100 \
  --enforce-eager \
  --data-parallel-size 1 \
  --data-parallel-size-local 1 \
  --api-server-count 1 \
  --data-parallel-address $MASTER_IP \
  --data-parallel-rpc-port 13348  \
  --tensor-parallel-size 4 \
  --seed 1024 \
  --served-model-name qwen \
  --max-model-len 40960 \
  --no-enable-prefix-caching \
  --max-num-batched-tokens 16384 \
  --trust-remote-code \
  --gpu-memory-utilization 0.9 \
  --max-num_seqs 16 \
  --enable-chunked-prefill \
  --additional-config \
  '{"ascend_scheduler_config":{"enabled":false}, "torchair_graph_config":{"enabled":false}, "enable_shared_expert_dp":false}' \
  --kv-transfer-config \
  '{
    "kv_connector": "AscendStoreConnector",
    "kv_role": "kv_both",
    "kv_connector_extra_config":{
      "backend": "memcache",
      "lookup_rpc_port": "0",
      "use_layerwise": false,
      "load_async": true
    }
  }' 2>&1 | tee mix.log &
```

### 4.2 参数说明

| 参数 | 说明 |
| :--- | :--- |
| `--tensor-parallel-size` | 张量并行大小 |
| `--max-model-len` | 最大序列长度 |
| `--gpu-memory-utilization` | GPU 显存利用率 |
| `--kv-transfer-config` | KV Cache 配置 |
