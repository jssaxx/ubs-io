# UBS IO Agent Guide

This file defines repository-wide operating rules for automated coding agents. It complements `README.md`, `CONTRIBUTING.md`, and subsystem documentation.

## Before Changing Files

1. Read this file and the documentation closest to the target subsystem.
2. Run `git status --short --branch` and preserve all pre-existing changes.
3. Keep the patch scoped to the requested task. Do not mix functional changes with unrelated cleanup or broad formatting.
4. Use the owning subsystem's build and test scripts.
5. Report the exact validation commands run and any environment limitations.

## Repository Layout

- `ubsio-boostio/`: UBSIO-BoostIO SDK, cache runtime, Cache/Flow layers, BDM disk backend, UnderFS, networking, security, and service processes.
- `ubsio-memstore/`: UBSIO-MemStore distributed in-memory KV implementation, shared-memory access, cluster and replica management, networking, notifications, and recovery.
- `ubsio-common/`: UBSIO-Common CLI and tracing utilities shared by the main components.
- `docs/`: top-level product documentation organized by component.

UBSIO-KV is documented on the `release/1.2` branch. Always inspect the target branch before assuming a component or path exists.

## Build Commands

Build on Linux with Bash, CMake, Make, GCC/G++, and the dependencies documented by each component.

```bash
cd ubsio-boostio
bash build.sh -t debug
bash build.sh -t release
```

```bash
cd ubsio-memstore
bash build.sh -t debug
bash build.sh -t release
```

```bash
cd ubsio-common/cli
bash build.sh
```

Clean only through the owning component script:

```bash
bash ubsio-boostio/build.sh -t clean
bash ubsio-memstore/build.sh -t clean
```

## Test Commands

```bash
cd ubsio-boostio/test/llt
bash run_dt.sh
```

```bash
cd ubsio-memstore/test/llt
bash run_dt.sh
```

The test scripts rebuild their subsystem and recreate owned build or coverage directories. Documentation-only changes require link, path, command, and Markdown structure review; they do not require a product build.

## Code and Documentation Style

- Follow adjacent code style and preserve existing license headers.
- Preserve public C ABI details, including `extern "C"`, enum values, structure layout, field widths, reserved fields, and symbol visibility unless an approved API change requires otherwise.
- Match existing namespaces, naming, include order, error handling, logging, and guard macros.
- Keep C files valid C and avoid introducing C++ constructs into them.
- Update tests for behavior changes and update documentation for user-visible API, configuration, deployment, packaging, or output changes.
- Use the component names `UBSIO-BoostIO`, `UBSIO-MemStore`, `UBSIO-Common`, and `UBSIO-KV` in user-facing documentation.

## Safety

- Treat every configured disk path as potentially destructive raw storage. Never format, partition, truncate, overwrite, benchmark, or initialize a real `/dev/*` device without explicit user authorization and read-only verification of its resolved path, ownership, mount state, and LVM, RAID, or swap use.
- Never weaken TLS, certificate checks, input validation, CRC/integrity checks, permissions, or secure build options to make a test pass.
- Treat configuration paths, environment variables, keys, object lengths, disk offsets, and dynamic-library paths as untrusted input.
- Do not log or commit keys, credentials, TLS material, cache contents, customer configuration, dumps, logs, tokens, or device identifiers.
- Do not modify vendored third-party sources unless the task explicitly requires a pinned dependency change or reviewed patch.
- Do not run broad deletion commands against the repository root, a home directory, `/`, unresolved variables, or globs. Clean only directories owned by the relevant build/test script.

## Git Workflow

- Preserve unrelated and untracked user changes.
- Do not commit, amend, rebase, force-push, or push unless the user explicitly requests it.
- Before committing, run `git diff --check`, inspect the complete diff, and run `git status --short`.
- Keep each commit and Pull Request focused on one logical topic, using the prefixes documented in `CONTRIBUTING.md`.
