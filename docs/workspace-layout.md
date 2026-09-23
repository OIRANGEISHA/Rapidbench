# Workspace layout and cleanup

源码保留在 `app/`、`packages/`、`native/`，脚本在 `tools/`，设计与发布记录在
`docs/`。不把本地构建缓存或测试证据提交到 Git。

| Directory | Purpose / retention |
| --- | --- |
| `.toolchains/` | Installed SDK/NDK/Flutter/JDK, package caches and local signing/authentication configuration. Do not bulk-delete this directory. |
| `release/<version>/` | Final published APK, checksum, release notes and provenance/component records. Keep a local verified copy. |
| `artifacts/` | Current QA logs, screenshots, Debug APK and publication checks. Keep evidence referenced by release records. |
| `artifacts/archive/` | Archived legacy packages, root patch backups and historical QA files. An index maps previous locations. |
| `build/`, `app/benchmark_app/build/`, native `.cxx/` | Regenerable compiler/intermediate output; safe to rebuild after a verified artifact is retained. |
| `.toolchains/downloads/` | Downloaded tool installation archives; removable only after confirming the installed tool works. |

清理规则：先列出绝对路径、大小和用途，确认没有 Git 跟踪文件及目录联接越界，再操作。
旧签名 APK 与历史证据优先归档，重复文件先核对 SHA-256；过期未发布 Debug APK、
下载测速文件与可再生缓存可以删除。删除清单、体积和归档位置保留在本地
`artifacts/workspace-cleanup-2026-09-23.md`。不能为了“精简”删掉现有 ABI 兼容字段、
回退实现、测试、依赖许可证或未知用途的业务代码。

构建缓存被清理后，首次重新构建会较慢。工具、依赖缓存、Beta 密钥和 GitHub 登录配置
继续保存在 H 盘，不迁移到系统盘。密钥和凭据绝不能进入公开发布资产。
