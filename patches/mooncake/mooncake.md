# Mooncake KV Backend Patch 合入指南

## 基线版本

| 项目 | 值 |
|------|-----|
| **上游仓库** | `https://github.com/kvcache-ai/Mooncake` |
| **基线 Commit** | `218f2ffdcf2ec10bca28c44af7ce994b18288cca` |
| **基线 Commit 信息** | `[] feat: add Hygon DCU/DTK and Iluvatar CoreX platform support (#2118)` |

> 确保 Mooncake 本地仓库已 checkout 到上述基线 commit，再 apply 本 patch。

## Patch 文件

`mooncake-kv-backend.patch` — 位于本目录下，包含对 `mooncake-store/` 模块的全部修改。

## 包含的 Commit 列表（共 7 个）

| # | Commit Hash | 说明 |
|---|------------|------|
| 1 | `63f3082` | add kv backend |
| 2 | `86bfaef` | 对接ubsio接口 |
| 3 | `8300180` | fix bug |
| 4 | `ebe829f` | add kv_backend test |
| 5 | `1bdfe0a` | fix: resolve compile problem |
| 6 | `7a7986e` | fix: resolve the functional problem |
| 7 | `cd46a2e` | fix: skip OffloadObjects call when there is nothing to offload |

## 修改文件清单

```
mooncake-store/include/dl_ubsio_api.h         (+158 行, 新增)
mooncake-store/include/storage_backend.h      (+86 行,  修改)
mooncake-store/src/CMakeLists.txt             (+1 行,   修改)
mooncake-store/src/client_buffer.cpp          (+2/-1,   修改)
mooncake-store/src/dl_ubsio_api.cpp           (+97 行,  新增)
mooncake-store/src/file_storage.cpp           (+11/-1,  修改)
mooncake-store/src/master_service.cpp         (+2/-1,   修改)
mooncake-store/src/storage_backend.cpp        (+334 行, 修改)
mooncake-store/tests/storage_backend_test.cpp (+560 行, 新增)
```

## Apply 步骤

### 1. 克隆 Mooncake 仓库并切换到基线版本

```bash
git clone https://github.com/kvcache-ai/Mooncake.git
cd Mooncake
git checkout 218f2ffdcf2ec10bca28c44af7ce994b18288cca
```

### 2. Apply Patch

```bash
git apply /path/to/ubsio-kv/mooncake-kv-backend.patch
```

如果遇到 whitespace 警告，可使用：

```bash
git apply --whitespace=fix /path/to/ubsio-kv/mooncake-kv-backend.patch
```

### 3. 验证 Patch 应用成功

```bash
git diff --stat
```

应看到 9 个文件被修改，与上述「修改文件清单」一致。

### 4. （可选）提交 Patch

```bash
git add mooncake-store/
git commit -m "feat: add KV backend (ubsio integration) to mooncake-store"
```

## 注意事项

1. **编译依赖**：本 patch 新增了 `libubsio_kvc.so` 的动态库依赖（通过 `dlopen` 加载），编译 Mooncake Store 时需确保 ubsio SDK 头文件和库文件在编译/运行时可用。
2. **CMakeLists**：patch 中已将 `dl_ubsio_api.cpp` 加入 `MOONCAKE_STORE_SOURCES`，无需手动修改 CMake 配置。
3. **测试**：新增的测试文件 `storage_backend_test.cpp` 依赖 `libubsio_kvc.so`，需在运行时环境中存在该库。
