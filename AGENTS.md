# UBS IO Agent Guide

This file is the repository-level operating manual for AI coding agents. It is
not a replacement for `README.md`, `CONTRIBUTING.md`, or subsystem design
documents. It applies to the whole repository unless a more specific
`AGENTS.md` is added below a subdirectory.

Before making changes:

1. Read this file and the documentation closest to the target subsystem.
2. Run `git status --short --branch` and preserve all pre-existing changes.
3. Keep the patch scoped to the requested task; do not mix cleanup or broad
   formatting with functional changes.
4. Use the repository scripts below rather than inventing replacement build or
   test flows.
5. Report the exact validation commands run. If the required Linux, hardware,
   or dependency environment is unavailable, state that limitation explicitly.

## Project Overview

UBS IO is an L3 distributed KV Cache for LLM inference. It extends HBM and DRAM
with compute-side NVMe SSD capacity and exposes C and Python KV interfaces for
standalone inference, memcache, Mooncake, and vLLM-Ascend integrations.

The repository is primarily C, C++, Python, Bash, and CMake:

- `ubsio-boostio/`: BoostIO SDK and cache runtime. Production C++ targets use
  C++11. It contains the memory cache, flow mapping, BDM disk backend,
  configuration, security, UnderFS, networking, and standalone server path.
- `ubsio-kv/`: public KV Cache API and its C/Python bindings. Production C++
  targets use C++17 and dynamically load BoostIO and optional Ascend/NDS
  libraries.
- `ubsio-common/`: shared CLI and tracing utilities.
- `examples/`: runnable integration examples, including the minimal Python KV
  flow.
- `docs/`: installation, configuration, API, architecture, and feature guides.
- `patches/`: integration patches for external ecosystems such as Mooncake.
- `scripts/`: operational helpers. Treat scripts that modify disks as
  privileged operations.

The principal public C headers are:

- `ubsio-boostio/src/sdk/bio_c.h`
- `ubsio-kv/include/ubsio_kvc.h`

Public API, configuration semantics, on-disk metadata, and exported library ABI
are compatibility-sensitive. Changes to them require explicit compatibility
analysis and normally meet the RFC threshold in `CONTRIBUTING.md`.

## Build Commands

Build on Linux. The scripts rely on Bash, GNU userland, CMake, Make, GCC/G++,
and Linux linker and I/O behavior. CMake 3.16 or later is required. Do not claim
a successful build from a Windows-only inspection environment.

Common prerequisites:

- `bash`, `cmake >= 3.16`, `make`, `gcc`, `g++`, and `git`.
- RDMA development packages when RDMA is enabled: `rdma-core-devel` on
  openEuler, or `libibverbs-dev` and `librdmacm-dev` on Ubuntu.
- Python 3, pip, wheel, and pybind11 when building the Python binding.
- Internet access only when preparing missing third-party source trees.

Run these commands from the repository root unless a different directory is
shown.

Build BoostIO and the default UBSIO-KV component:

```bash
cd ubsio-boostio
bash build.sh -t debug
bash build.sh -t release
```

Build release libraries for deployment:

```bash
cd ubsio-boostio
bash build.sh -t release --pkg
```

The `--pkg` path defaults to `ubsio-boostio/dist/lib`. It also defaults RDMA off
unless `UBSIO_HCOM_ENABLE_RDMA` was set explicitly. To control the setting:

```bash
UBSIO_HCOM_ENABLE_RDMA=OFF bash ubsio-boostio/build.sh -t release --pkg
```

Build only BoostIO, skipping UBSIO-KV packaging:

```bash
cd ubsio-boostio
bash build.sh -t debug --build_kv OFF
```

Build UBSIO-KV independently. Use `--build_boostio ON` if a matching BoostIO
build is also required, and disable Python for a smaller C/C++-only build:

```bash
cd ubsio-kv
bash build.sh -t debug --build_python OFF
bash build.sh -t release --build_boostio ON --build_python ON
```

Build the shared CLI tools directly:

```bash
cd ubsio-common/cli
bash build.sh
```

Sanitizer-enabled BoostIO/LLT build:

```bash
cd ubsio-boostio
bash build.sh -t debug --ut --san=asan
```

Clean through the owning subsystem's script:

```bash
bash ubsio-boostio/build.sh -t clean
bash ubsio-kv/build.sh -t clean
```

Build outputs are generated under subsystem `Build/`, `dist/`, and sometimes
`coverage_report/` directories. They are not source files and must not be
committed.

`bash ubsio-boostio/build.sh --prepare_3rdparty` is not a normal build step. It
uses the network and runs `git reset --hard` plus `git clean -fd` inside managed
third-party checkouts before pinning and packaging them. Run it only when the
user explicitly asks to prepare third-party sources and after confirming that
those checkout directories contain no work that must be preserved.

## Test Commands

The canonical low-level test commands are:

```bash
cd ubsio-boostio/test/llt
bash run_dt.sh
```

```bash
cd ubsio-kv/test/llt
bash run_dt.sh
```

Both scripts rebuild their subsystem, run GoogleTest binaries, collect lcov
coverage, and write HTML reports to the subsystem's `coverage_report/`.

Important test behavior:

- The scripts delete and recreate their subsystem's `Build/` and
  `coverage_report/` directories. Do not store manual work there.
- BoostIO LLT produces `bio_test` and uses stubbed/local files. Never replace
  its test disk paths with production block devices.
- UBSIO-KV LLT produces `kv_loader_test` and `kv_test`, uses fake Ascend and
  BoostIO libraries, and enforces line coverage greater than 85% and branch
  coverage greater than 60%.
- LLT requires `cmake`, `g++`, `lcov`, and `genhtml`, plus the test tools
  installed by the build scripts.
- Hardware, integration, or performance tests are not substitutes for LLT and
  must be run only in an explicitly provisioned environment.

For a small change, run the affected subsystem's LLT. For changes spanning
`ubsio-kv` and `ubsio-boostio`, run both. Documentation-only changes require at
least link, path, command, and Markdown structure review. There is no
repository-level test aggregator; do not report one unless it is added.

## Code Style

- Follow the style of adjacent files. The repository has no root formatter
  configuration, so do not run a repository-wide formatter or make unrelated
  whitespace changes.
- Preserve the existing Mulan PSL v2 copyright/license header in C and C++
  source files. Use the current year and project-approved owner for new files;
  do not invent ownership text.
- Use four-space indentation where the surrounding file does. Preserve local
  brace, include-order, naming, and comment conventions.
- Keep BoostIO production code compatible with C++11 and UBSIO-KV production
  code compatible with C++17. Do not raise a language standard merely to use a
  convenience feature.
- Keep C sources under the disk and third-party boundaries valid C; do not
  introduce C++ constructs into `.c` files.
- Match existing namespaces (`ock::bio`, `ock::ubsio`) and nearby naming. C ABI
  symbols use the established `Bio...` and `Ubsio...` forms.
- Preserve `extern "C"`, enum numeric values, structure layout, field widths,
  reserved fields, and symbol visibility in public headers unless an approved
  API/ABI change explicitly requires otherwise.
- Validate inputs and propagate the subsystem's established result codes. Use
  existing logging and guard/check macros rather than adding a parallel error
  handling convention.
- Do not log keys, credentials, TLS material, raw cache contents, or other
  customer data. Include only the context needed to diagnose a failure.
- Add or update tests with behavior changes. User-visible API, configuration,
  deployment, or output changes must update the corresponding documentation.
- Shell changes should quote paths and variables, fail on errors consistently
  with the surrounding script, and avoid unbounded deletion targets.

## Dev Environment Tips

- Preserve the sibling layout `ubsio-boostio/`, `ubsio-kv/`, and
  `ubsio-common/`; build scripts resolve dependencies using these relative
  paths.
- Prefer the subsystem `build.sh` entry points over direct CMake invocations;
  the scripts set feature flags, install test tools, assemble runtime
  libraries, and package outputs.
- Debug BoostIO builds enable CLI and tracepoint support by default. Release
  builds disable release-inappropriate diagnostic features.
- `UBSIO_HCOM_ENABLE_RDMA=ON|OFF` controls the ubs-comm RDMA build. Use `OFF` in
  environments without RDMA headers/libraries.
- `UBSIO_CONFIG_PATH` selects a runtime configuration file. If unset, runtime
  code expects `/etc/ubsio/ubsio.conf`. Tests should use isolated test configs,
  not modify the system file.
- `LD_LIBRARY_PATH` may be needed for locally built libraries. Keep it scoped to
  the test or example command rather than changing a user's shell profile.
- Python wheel output is under `ubsio-kv/dist/pkg/`; release library packaging
  is under `ubsio-boostio/dist/lib/`.
- Use `rg`/`rg --files` for source discovery. Read the nearest CMake target and
  tests before changing implementation code.
- Do not edit generated files under `Build/`, `dist/`, `coverage_report/`,
  Python `build/`, wheel `dist/`, or `*.egg-info/`. Regenerate them from source.
- Third-party code belongs under `ubsio-boostio/3rdparty/`. Avoid editing
  vendored sources directly; prefer a pinned version change or an explicit
  patch with license and compatibility review.

## Architecture

The primary standalone data path is:

```text
Application / memcache / Mooncake / vLLM-Ascend
    -> UBSIO-KV C or Python API
    -> key validation, hashing, location, and batch organization
    -> dynamically loaded libbio_sdk
    -> BoostIO SDK and standalone client/server path
    -> Cache + Flow
    -> DRAM WCache/RCache and optional BDM-managed NVMe SSD
```

Key BoostIO areas:

- `src/sdk/`: public and internal client API boundaries.
- `src/cache/` and `src/flow/`: object lifecycle, cache tiers, mapping, and data
  movement.
- `src/disk/`: BDM allocation, metadata recovery, synchronous/asynchronous raw
  I/O, and disk state. Changes here can affect persistent-format compatibility
  and data integrity.
- `src/config/` and `configs/`: configuration definitions, validation, and
  shipped defaults.
- `src/server/` and `src/daemon/`: runtime service orchestration.
- `src/net/` and `src/cluster/`: networked and cluster functionality. These
  remain in the tree even though the current standalone inference path is
  primarily in-process.
- `src/underfs/`: optional backing-file-system integration.
- `src/security/`: TLS, certificate, decryption, and expiration checks.
- `src/interceptor/`, `src/io_interceptor/`, and `src/htracer/`: diagnostics,
  interception, and tracing.

Key UBSIO-KV areas:

- `include/ubsio_kvc.h`: public C API and ABI.
- `src/csrc/kvc/`: KV-to-BoostIO translation and dynamic loader logic.
- `src/csrc/nds/`: optional NDS dynamic integration.
- `src/python/` and `python_whl/`: Python binding and wheel packaging.
- `test/llt/`: loader and API unit tests with fake external libraries.

When changing a cross-layer request, trace it from the public API through KV,
SDK, Cache/Flow, and BDM before editing. Maintain ownership boundaries rather
than bypassing one layer for a local shortcut.

## Security Guidelines（安全指南）

- 将 `ubsio.disk.path` 和所有 BDM 目标视为可能造成数据破坏的裸存储。未经用户明确授权，且未以只读方式核实目标的解析路径、归属、挂载状态以及 LVM、RAID、swap 使用情况，不得对真实 `/dev/*` 设备执行格式化、分区、截断、覆盖、写性能测试或初始化。
- 生产缓存设备必须按照部署设计预先分配并由 UBS IO 独占。不得使用已挂载文件系统的底层设备或其他服务正在使用的 LUN 进行测试。
- 不得对仓库根目录、用户主目录、`/` 或尚未解析的变量执行大范围删除命令。清理操作只能作用于对应构建或测试脚本明确拥有的生成目录。
- 不得为了通过测试而削弱 TLS、证书校验、路径校验、安全编译选项、CRC/完整性校验或权限检查。
- 将配置路径、环境变量、key、对象长度、磁盘偏移和动态库路径视为不可信输入，检查边界、整数溢出、规范化后的路径与类型、对象生命周期以及错误返回值。
- 不得提交密钥、私钥、令牌、客户配置、日志、转储、缓存数据或设备标识。文档和测试必须使用含义明确的占位符。
- 未经用户授权，不得安装软件包、访问私有服务、拉取依赖或修改外部系统。
- 三方依赖必须固定版本。新增或升级依赖前，检查许可证、安全性、构建影响和离线交付影响。
- 漏洞处理遵循 `SECURITY.md`。修复发布前，不得在公开 Issue 或普通 PR 讨论中披露可利用细节或 PoC。

## Commit Guidelines（提交规范）

- 遵循 `CONTRIBUTING.md`。常规修改从 `develop` 创建短生命周期的 `feat/`、`fix/`、`docs/`、`test/` 或 `chore/` 分支，并通过聚焦单一主题的 PR 合入。不得将普通变更直接推送到 `master` 或发布分支。
- 提交标题使用既有前缀：`feat:`、`fix:`、`docs:`、`test:`、`refactor:`、`perf:` 或 `chore:`。标题应使用祈使语气，具体且简洁。
- 每个 commit 和 PR 只处理一个逻辑问题，不得混入无关重构、生成产物、本地配置或大范围格式化修改。
- 提交前检查 `git diff --check`、`git diff` 和 `git status --short`。在 PR 描述中记录实际执行的测试命令，并说明因环境限制而未执行的验证。
- 公共 API、配置、部署、打包或用户可见行为发生变化时，必须同步更新文档并明确说明兼容性影响。
- 涉及架构、公共 API/配置语义、部署模型、持久化格式、性能关键数据路径、新增外部依赖/服务/协议、跨子系统修改，或预计超过约 500 行非测试代码时，应先提交 RFC。
- 未经用户明确要求，不得执行 amend、rebase、force-push、创建 commit 或 push；不得丢弃其他作者尚未提交的修改。
