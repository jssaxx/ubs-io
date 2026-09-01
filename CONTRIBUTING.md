# 贡献指南

感谢你关注 UBS IO。我们欢迎代码、文档、测试、部署经验、性能数据和设计讨论等各类贡献。

## 开始之前

1. 阅读项目 [README](README.md) 和目标组件最近的文档。
2. 搜索已有 Issue 和 Pull Request，确认问题尚未被处理。
3. 对公共 API、配置语义、部署模型、持久化格式或性能关键路径的较大变更，先通过 Issue 或 RFC 与维护者确认方案。
4. 确认目标合入分支，再从该分支创建短生命周期工作分支。

## 分支与提交

工作分支建议使用以下命名：

- `feat/<short-name>`：新增功能。
- `fix/<short-name>`：缺陷修复。
- `docs/<short-name>`：文档修改。
- `test/<short-name>`：测试修改。
- `chore/<short-name>`：构建、脚本和仓库维护。

`release/<version>` 用于版本稳定和发布维护。除非维护者明确指定，不要把常规功能直接提交到发布分支，也不要直接推送受保护分支。

提交标题使用简洁的祈使语气，建议采用以下前缀：

- `feat:` 新功能。
- `fix:` 缺陷修复。
- `docs:` 文档。
- `test:` 测试。
- `refactor:` 重构。
- `perf:` 性能优化。
- `chore:` 构建、脚本或仓库维护。

示例：

```text
docs: update UBSIO-MemStore deployment guide
fix: reject invalid cache configuration
test: cover BoostIO batch read failures
```

## 修改要求

- 一个 Pull Request 只处理一个逻辑问题，不混入无关重构或格式化。
- 遵循目标目录的既有代码风格。
- 公共 C 接口应保持 ABI 兼容；未经明确设计评审，不修改枚举数值、结构体布局、字段宽度和导出符号。
- 新增或修改用户可见接口、配置、部署步骤和输出时，同步更新相关文档。
- 行为变化应增加或更新测试。无法执行测试时，在 Pull Request 中说明原因和环境限制。
- 不提交本地配置、日志、转储、密钥、令牌、设备标识或客户数据。
- 三方依赖应固定版本；新增或升级依赖前应评估许可证、安全、构建和离线交付影响。

## 构建与测试

在 Linux 环境从仓库根目录执行。常用构建命令：

```bash
cd ubsio-boostio
bash build.sh -t debug
```

```bash
cd ubsio-memstore
bash build.sh -t debug
```

对应的低层单元测试入口：

```bash
cd ubsio-boostio/test/llt
bash run_dt.sh
```

```bash
cd ubsio-memstore/test/llt
bash run_dt.sh
```

这些测试会重新构建组件并生成覆盖率报告。运行前请确认构建目录中没有需要保留的手工文件，并准备 CMake、GCC/G++、lcov、genhtml 和组件要求的三方依赖。

文档修改至少应检查 Markdown 结构、本地链接、图片路径和示例命令。提交前执行：

```bash
git diff --check
git status --short
```

## Pull Request 说明

Pull Request 应包含：

- 问题背景和修改摘要。
- 影响的组件、接口、配置或部署场景。
- 实际执行的验证命令和结果。
- 未执行验证的原因与风险说明。
- 公共 API、配置、持久化格式或兼容性影响。
- 关联的 Issue 或设计讨论。

使用自动化编码助手参与修改时，还应遵循仓库根目录的 [AGENTS.md](AGENTS.md)。
