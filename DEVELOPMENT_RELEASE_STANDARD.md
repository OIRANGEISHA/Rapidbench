# RapidBench 开发、版本迭代与发布规范

> 文档版本：1.1
> 适用范围：Beta、Preview、Release Candidate、Stable、Hotfix 版本
> 最后更新：2026-08-29

本文档用于规范 RapidBench 后续版本的需求整理、增量开发、测试验证、Git 提交、Android 构建和 GitHub 发布流程。目标是保证每个版本可复现、可验证、可追踪，并避免未经验证的性能结论进入公开版本。

## 1. 规范级别

本文使用以下关键词：

- **必须（MUST）**：发布前必须满足；不满足时不得继续发布，除非存在书面豁免记录。
- **应该（SHOULD）**：默认应执行；无法执行时必须在 Release Notes 中说明原因和风险。
- **可以（MAY）**：按版本规模和实际需要选择执行。

## 2. 版本类型与命名

### 2.1 语义化版本

版本号遵循 Semantic Versioning：

```text
MAJOR.MINOR.PATCH[-PRERELEASE]
```

| 版本类型 | versionName 示例 | Git Tag 示例 | GitHub 状态 |
| --- | --- | --- | --- |
| Beta / Preview | `1.1.0-beta.1` | `v1.1.0-beta.1` | Pre-release |
| Release Candidate | `1.1.0-rc.1` | `v1.1.0-rc.1` | Pre-release |
| Stable | `1.1.0` | `v1.1.0` | Latest release |
| Hotfix Beta | `1.1.1-beta.1` | `v1.1.1-beta.1` | Pre-release |
| Hotfix Stable | `1.1.1` | `v1.1.1` | Latest release |

版本含义：

- `MAJOR`：存在不兼容的公共接口、数据格式或评分体系变更。
- `MINOR`：新增兼容功能、测试模块或硬件能力检测。
- `PATCH`：缺陷修复、兼容性修复、稳定性优化或不改变评分口径的小幅调整。
- `beta.N`：功能可测试，但仍可能存在设备兼容性问题。
- `rc.N`：功能冻结，只接受阻断发布的缺陷修复。

补充规则：

- 预发布版本的优先级低于对应正式版，例如 `1.1.0-rc.1 < 1.1.0`。
- 预发布序号必须递增，不能用 `beta.2` 覆盖 `beta.1`，也不能重新发布同名版本。
- `+build.metadata` 只用于记录构建信息，不参与版本优先级，也不得代替新的修复版本号。
- 一个版本一经公开，其代码、Tag 和发布资产不得原地修改；任何修正都必须发布新版本。

### 2.2 Android 版本字段

- `versionName` **必须**与 Release 版本一致。
- `versionCode` **必须**单调递增，不得复用已经公开发布过的值。
- 使用 ABI Split 时，必须同时检查最终 APK 中经过 ABI 偏移后的实际 `versionCode`。
- 改变评分算法或工作量定义时，至少提升 `MINOR` 版本，并在 Release Notes 中声明新旧分数不可直接比较。

### 2.3 统一命名

```text
Tag:          v1.1.0-beta.1
Release:      RapidBench 1.1.0 Beta 1
APK:          RapidBench-1.1.0-beta.1-arm64-v8a-release.apk
Checksum:     RapidBench-1.1.0-beta.1-arm64-v8a-release.apk.sha256
```

## 3. 分支与基线管理

### 3.1 发布基线

开始迭代前必须确认：

```powershell
git status --short --branch
git fetch --prune origin
git log -5 --oneline
git tag --list
```

基线要求：

- 工作区必须干净；已有未提交修改必须先确认归属。
- 本地基线必须与目标远端分支一致。
- 上一个公开版本必须存在对应 Tag。
- 不得在来源不明或包含用户未确认改动的工作区上发布。

### 3.2 分支策略

- 小型、低风险修复可以直接在可发布的 `main` 上完成，但提交前必须完整验证。
- 涉及多个模块、评分算法、公共接口或较高风险的变更，应该创建独立分支：

```text
feature/<short-name>
fix/<short-name>
perf/<short-name>
release/<version>
hotfix/<version>
```

- 分支合并前必须通过对应发布门禁。
- 所有已公开的 Stable、Beta 和 RC Tag 都禁止移动、删除或覆盖。
- 发布错误时必须撤下或标记受影响版本，并以新的版本号、Tag、资产和校验值重新发布。

## 4. 需求与计划

### 4.1 需求规格必须包含

每次迭代开始前，必须形成可检查的 Spec：

```markdown
# <version> 需求规格

## 目标
- 本版本要解决什么问题？

## 新增功能
- [ ] 功能、入口、预期结果

## 缺陷修复
- [ ] 复现条件
- [ ] 当前错误表现
- [ ] 预期修复结果

## 兼容性范围
- Android 最低版本
- CPU/GPU/存储类型
- 可获得的真实测试设备
- 只能模拟验证的设备或路径

## 评分与数据兼容性
- 是否改变测试工作量、计时、单位或评分标准？
- 新旧结果是否可以比较？

## 测试要求
- [ ] 单元测试
- [ ] 回归测试
- [ ] Debug 真机测试
- [ ] Release 安装与启动测试

## 已知限制
- 当前无法验证的机型、系统或驱动
```

### 4.2 先计划后修改

计划至少应说明：

1. 根因假设和需要收集的证据。
2. 将修改的模块和不应触碰的模块。
3. 兼容性策略和回退行为。
4. 测试方案、真实设备与模拟边界。
5. 版本号、文档和发布资产变化。

未经确认，不得把诊断任务自动扩大成架构重写、评分体系重做或依赖升级。

### 4.3 发布负责人和批准

- 每个公开版本必须指定一名 Release Owner，负责版本号、门禁、Tag、资产和发布后复核。
- Beta 可以由 Release Owner 自检发布；RC 和 Stable 应由另一名维护者复核。
- 个人项目没有第二名维护者时，可以用公开 Issue 或签字的发布清单代替，但必须保留检查证据和时间。
- Release Owner 不得在测试未完成、远端基线未知或签名来源不明时发布。

## 5. 增量开发原则

### 5.1 修改范围

- 必须只修改与当前需求直接相关的代码。
- 禁止无理由重写已稳定模块。
- 禁止把格式化改动、重命名和功能修改混在同一个大范围 Diff 中。
- 发现无关缺陷时，应记录为独立 Issue 或后续任务，不得顺手扩大当前发布范围。
- 不得删除或覆盖用户已有但与当前任务无关的修改。

### 5.2 公共接口与兼容性

- 公共 API、FFI ABI、持久化数据或结果字段发生破坏性变化时，必须提升对应版本并提供迁移说明。
- 新增字段应优先采用向后兼容方式，并保留结构大小或 ABI 版本检查。
- 不得静默改变已有指标的单位、统计口径或含义。
- 弃用接口必须标记弃用原因、替代方案和计划删除版本。

### 5.3 依赖管理

- 未经明确评审不得新增第三方依赖。
- 新依赖必须说明用途、许可证、体积、维护状态和替代方案。
- Android SDK、NDK、Flutter、Gradle 或 Kotlin 版本升级必须单独提交，不得隐藏在普通功能修复中。
- 构建不得在未声明的情况下向系统盘安装工具或写入长期缓存。

## 6. 代码质量要求

### 6.1 结构

- 复杂逻辑应拆分为职责单一、可测试的辅助函数。
- 计时、验证、拓扑发现、UI 展示和平台适配应保持边界清晰。
- 避免厂商、型号或核心数量的硬编码。
- 不得用硬编码分数上限掩盖错误计时或错误工作量统计。

### 6.2 错误处理

- 禁止空 `catch`、忽略返回码或静默吞掉错误。
- 错误必须转换为明确的状态、错误码或用户可理解的信息。
- 回退路径必须可见，例如 `HOST_FALLBACK`、`Affinity warning`、`Buffered I/O`。
- 回退不得伪装成成功绑定、原生特性或完整硬件能力。
- 外部接口失败时必须保留足够诊断信息，但不得输出令牌、签名密钥或个人数据。

### 6.3 日志

- 关键初始化、平台能力检测和失败路径应该提供结构化诊断信息。
- 性能测试的正式计时循环内不得输出日志，避免影响结果。
- Debug 日志不得默认进入 Release 用户界面。
- 发布前必须确认没有凭据、设备隐私数据或临时路径写入公开日志。

### 6.4 注释与文档

- 公共 API、FFI 接口、评分公式和非显而易见的兼容逻辑必须有注释。
- 注释应解释“为什么”，不要重复代码表面行为。
- 简单私有函数不要求机械补充无意义注释。
- 临时兼容或迁移逻辑可以使用：

```cpp
// VERSION-NOTE(v1.1.0): Explain why this compatibility path exists
// and the condition under which it may be removed.
```

- 版本标记必须包含删除条件；不得永久堆积 `BETAx-UPDATE` 注释。

## 7. Benchmark 可信度规范

### 7.1 通用要求

- 评分必须来源于已完成的确定性工作量和实际测量时间。
- 工作量必须保持可观察，防止编译器删除或折叠计算。
- Warm-up、正式测量、Peak、Sustained 和单位定义必须明确。
- 不得把理论峰值直接当成实测结果。
- 不得针对特定型号硬编码结果、倍率或通过分数上限掩盖异常。
- 不得隐瞒温控、Affinity、计时回退或设备能力缺失。

### 7.2 CPU

- CPU 数量和性能簇必须动态检测，不得假定固定为 8 核或固定簇数。
- 必须区分 `present`、`online`、`allowed` 和实际 Affinity 结果。
- Affinity 失败时可以继续施加负载，但必须明确报告未成功绑定。
- 多核 worker 数量、实际可运行 CPU 和所选簇必须能够被诊断。
- 性能提示只能作为调度请求，不得描述成必然达到最高频率。

### 7.3 GPU

- FLOPS/GOPS 必须按 Shader 的真实操作数计算。
- GPU Timestamp 必须检查驱动支持、有效位数、查询结果和合理性。
- 建议使用独立 Host Fence 时间进行交叉验证。
- 驱动时间戳异常时必须回退到 Host Timing，并在结果中标明。
- 不得按照 Adreno、Mali、Xclipse、PowerVR 等名称硬编码成绩修正。

### 7.4 内存与存储

- 必须明确 payload 带宽和总内存流量的区别。
- Copy 按双向流量统计时，应明确 Read 与 Write 各计一次。
- Storage 必须标明块大小、队列深度、线程数、Direct/Buffered I/O 和 Flush 行为。
- 缓存命中、文件系统状态和同步策略不得被隐藏。

### 7.5 测试结论边界

- 只有实际运行过完整测试的设备才能标记为“真机验证”。
- 模拟拓扑、静态代码检查或逻辑单元测试必须标记为“模拟验证”。
- 没有设备时不得推断某型号已经修复，只能说明对应代码路径已覆盖。
- 发布说明必须列出真实测试设备和暂未覆盖的设备。

## 8. Git 提交规范

### 8.1 Conventional Commits

每个提交必须使用：

```text
<type>(<scope>): <short description>

[optional body]

[optional footer]
```

允许的常用类型：

| Type | 用途 |
| --- | --- |
| `feat` | 新增功能 |
| `fix` | 修复缺陷 |
| `perf` | 性能优化 |
| `refactor` | 不改变外部行为的重构 |
| `test` | 测试新增或调整 |
| `docs` | 文档更新 |
| `build` | 构建系统或依赖调整 |
| `ci` | CI/CD 调整 |
| `chore` | 不属于以上类型的维护工作 |
| `revert` | 回滚已有提交 |

示例：

```text
fix(cpu): detect all present cores on legacy kernels
fix(gpu): reject implausible Vulkan timestamps
perf(memory): increase memcpy worker density
test(native): cover restricted-affinity fallback
docs(release): document v1.1.0 beta 1 changes
chore(release): prepare v1.1.0-beta.1
```

### 8.2 提交粒度

- 一个提交应表达一个逻辑变化。
- 功能、测试、文档可以分开提交；紧密关联且无法独立工作的测试可以与修复放在同一提交。
- 禁止使用 `update`、`fix stuff`、`final`、`new version` 等无法追踪目的的提交信息。
- Release Commit 应使用 `chore(release): prepare <version>`，不能只写 `Release <version>`。
- 提交前必须执行：

```powershell
git diff --check
git diff --stat
git diff
```

### 8.3 Tag 规范

- Beta/RC 至少使用附注 Tag；Stable 应在密钥基础设施可用时使用签名附注 Tag。
- Tag 消息应包含版本、目标 Commit 和发布类型，禁止用工作树当前状态代替明确 Commit。
- 创建 Tag 前必须记录 `git rev-parse HEAD`；推送后必须验证远端 Tag 解析到同一 Commit。
- 如果仓库启用了 GitHub Immutable Releases，应在发布前通过草稿 Release 完成资产准备，再发布并锁定 Tag 与资产。

## 9. 测试与发布门禁

### 9.1 所有版本必须通过

- [ ] 静态检查无错误。
- [ ] 新增逻辑有单元或回归测试。
- [ ] 已注册的原有测试全部通过。
- [ ] Debug APK 构建成功。
- [ ] Release APK 构建成功。
- [ ] Release APK 可以覆盖安装或全新安装。
- [ ] App 可以启动，崩溃缓冲无新增记录。
- [ ] APK 的 package、versionName、versionCode、minSdk、targetSdk 正确。
- [ ] APK 签名校验通过。
- [ ] APK 只包含计划发布的 ABI。
- [ ] SHA-256 已生成并复核。
- [ ] Git Diff 无意外文件、无临时文件、无大范围无关格式化。
- [ ] Release Notes 与实际代码和测试边界一致。

### 9.2 Beta / Preview 额外要求

- 可以存在已知兼容性限制，但必须在 Release Notes 中披露。
- 至少完成一台目标架构真机的安装、启动和核心流程验证。
- 未获得的机型可以使用模拟测试，但不得描述为真机通过。
- GitHub Release 必须设置为 Pre-release。

### 9.3 RC 额外要求

- 功能冻结，只接受阻断性修复。
- 应覆盖至少两个不同厂商或不同 GPU 驱动家族的真实设备。
- 不得存在已知的崩溃、数据损坏或明显虚高/虚低评分问题。
- 发布签名、包名和升级路径必须与 Stable 计划一致。

### 9.4 Stable 额外要求

- 必须使用受保护、可持续保存的正式签名证书。
- 禁止使用 Android Debug 证书作为公开 Stable 签名。
- 必须完成跨设备回归矩阵，并保存测试版本和结果摘要。
- 必须确认从上一个 Stable 版本可以正常升级。
- 必须清除临时诊断入口、调试资源和测试证书说明之外的调试配置。
- GitHub Release 不得标记为 Pre-release，并应设置为最新稳定版本。

### 9.5 发布阶段与冻结规则

| 阶段 | 允许变化 | 禁止变化 |
| --- | --- | --- |
| Beta | 功能、算法、UI、兼容性修复 | 未说明的评分口径变化 |
| RC | 阻断缺陷、文档、测试、发布配置修复 | 新功能、非必要重构、依赖大版本升级 |
| Stable 候选 | 只接受发布阻断修复 | 任何未重新走 RC 的功能或算法变化 |
| Hotfix | 单一高优先级修复及对应测试 | 顺带功能、无关清理、评分体系重做 |

- RC 开始后进入 Feature Freeze；任何新增功能都必须推迟到下一开发版本。
- Stable 应尽量与最后一个通过门禁的 RC 使用同一 Commit；如果代码发生变化，必须创建新的 RC 并重新验证。
- RC/Stable 发布前至少进行一次完整 Dry Run：从干净检出开始构建、签名、校验、安装并生成草稿 Release，确认无误后再公开。

## 10. Android 构建与签名

- 所有工具链、缓存和构建输出必须位于项目指定目录，不得擅自安装到系统盘。
- Debug、Beta Release 和 Stable Release 必须明确区分。
- 面向外部长期测试的 Beta 应尽早使用稳定的 Beta/Production Keystore，避免后续因签名变化无法覆盖安装。
- Keystore、密码、服务令牌和私钥禁止提交到 Git。
- Release 构建应启用既定的优化、混淆和资源压缩配置，但必须验证 FFI、反射和平台通道未被错误裁剪。
- 对外 Release 必须确认 `android:debuggable=false`，并清除仅供内部使用的诊断开关。
- GitHub 直接分发使用 APK；需要提交应用商店时优先生成 AAB。两者不能混用同一文件名或校验值。
- 如果启用 `--obfuscate` 或拆分调试符号，必须把符号文件按版本和 Commit 私下归档；不得把包含敏感路径的符号包公开发布。
- 构建记录必须保存 Commit SHA、Flutter/JDK/Android SDK/NDK/CMake 版本、构建命令、目标 ABI 和签名证书摘要。
- RC/Stable 应从干净检出构建；条件允许时由 CI 再构建一次并比较产物或至少核对来源证明。
- 发布前应执行类似检查：

```powershell
flutter analyze
flutter build apk --release --target-platform android-arm64 --split-per-abi
apksigner verify --verbose --print-certs <apk>
aapt dump badging <apk>
```

## 11. 文档与 Release Notes

### 11.1 仓库文档

每个公开版本必须同步：

- `README.md`
- `README.zh-CN.md`
- `CHANGELOG.md`
- 项目版本字段

文档必须描述实际实现，不得保留已经失效的算法、单位或兼容性说明。

`CHANGELOG.md` 是面向用户的长期变更记录，不能只依赖 GitHub Releases，也不能直接粘贴原始 Git 日志。必须遵循：

- 顶部保留 `[Unreleased]`，开发中的用户可见变化先进入该节。
- 发布时把对应内容移入 `[版本] - YYYY-MM-DD`。
- 按需使用 `Added`、`Changed`、`Deprecated`、`Removed`、`Fixed`、`Security` 分类。
- 每条记录说明用户可观察到的变化；内部重构仅在影响行为、兼容性或维护风险时记录。
- 底部维护 `[Unreleased]` 和各版本的 Git Compare 链接。
- GitHub Release Notes 可以复用该版本 Changelog，但应额外包含验证设备、资产、签名和已知限制。

### 11.2 Release Notes 模板

```markdown
# RapidBench <version>

## Highlights / 主要变化
- 本版本最重要的变化

## Fixed / 修复
- 问题表现
- 根因或修复方式
- 用户可观察到的变化

## Changed / 调整
- 算法、UI、单位或兼容性变化

## Validation / 验证
- 真机验证设备
- 自动化和回归测试
- 仅模拟验证的设备或路径

## Known limitations / 已知限制
- 尚未解决或无法验证的问题

## Artifact / 发布文件
- 文件名
- ABI
- versionName / versionCode
- 签名类型
- SHA-256
```

发布说明禁止：

- 宣称未执行过的真机测试。
- 隐瞒评分口径变化。
- 使用无法验证的“完全修复”“所有设备兼容”等绝对表述。
- 出现自动化工具、模型或代写来源的署名字样。

## 12. GitHub 发布流程

### 12.1 发布前身份校验

- 必须确认 Git Commit Author、Tagger、GitHub 登录账号和 Release Uploader 属于仓库所有者或获授权维护者。
- 多账号环境必须显式选择正确账号，不能依赖默认凭据。
- 发布前必须确认目标仓库 URL 和目标分支。

### 12.2 标准发布顺序

1. 完成测试和 Release APK 验证。
2. 更新版本号、README 和 CHANGELOG。
3. 创建符合 Conventional Commits 的提交。
4. 合并或快进到 `main`。
5. 从干净检出执行最终构建，生成 APK、SHA-256 和来源记录。
6. 创建 GitHub Draft Release，并在草稿阶段上传全部资产。
7. 验证 Release Notes、资产、签名、校验值和目标 Commit。
8. 创建不可变 Tag，并原子推送 `main` 与 Tag。
9. Beta/RC 勾选 Pre-release；Stable 不勾选并设置为 Latest。
10. 发布 Draft；启用 Immutable Releases 的仓库在此时锁定 Tag 和资产。
11. 重新读取 GitHub 状态，核对作者、Tag、Commit、资产大小、来源证明和下载链接。

推荐使用原子推送：

```powershell
git push --atomic origin main refs/tags/<tag>
```

如果 Git 传输不可用而必须使用 GitHub API：

- 必须先确认远端分支没有移动。
- 必须验证生成的 Tree SHA 与本地 Tree SHA 一致。
- 禁止以 `force=true` 覆盖未知远端提交。
- API 创建的 Commit 必须与本地文件树、父提交、作者和消息一致。
- 发布后必须重新读取远端分支、Tag、Release 和资产进行校验。
- 应在维护记录中注明使用了 API 回退方式。

### 12.3 发布后核对

- [ ] `main` 指向预期 Commit。
- [ ] Tag 指向同一 Commit。
- [ ] Commit Author 正确。
- [ ] Release Author 和资产 Uploader 正确。
- [ ] Pre-release/Latest 状态正确。
- [ ] APK 文件名、大小和 SHA-256 正确。
- [ ] 下载链接可用。
- [ ] 本地工作区干净且与 `origin/main` 同步。

### 12.4 不可变发布、来源证明与验证

- 仓库支持时，Stable **必须**启用 GitHub Immutable Releases；Beta/RC **应该**启用。
- 发布前必须先用 Draft 准备好全部资产。不可变发布公开后，不得替换 APK、校验文件、Tag 或 Release 目标。
- 使用 GitHub Actions 构建时，Stable **必须**为最终 APK/AAB 生成 Artifact Attestation；Beta/RC **应该**生成。
- 来源证明用于确认“哪个仓库、工作流和 Commit 生成了该产物”，不等同于安全审计，也不能替代签名、测试和代码审查。
- Stable 应生成并保留 SBOM；Beta/RC 在依赖或原生库发生变化时应该生成。
- 发布后至少执行一次 Release 与资产完整性验证：

```powershell
gh release verify <tag> --repo OIRANGEISHA/Rapidbench
gh release verify-asset <tag> <apk> --repo OIRANGEISHA/Rapidbench
gh attestation verify <apk> --repo OIRANGEISHA/Rapidbench
```

- 如果当前 GitHub 套餐、仓库可见性或构建环境不支持某项能力，必须记录原因，并保留 SHA-256、签名证书摘要、Commit SHA 和构建环境清单作为最低替代证据。

## 13. 回滚与 Hotfix

- 发布后发现普通缺陷时，创建新 Patch/Beta 版本，不得替换已有 APK。
- 发现严重崩溃、安全问题、数据损坏或评分体系错误时，应立即把 Release 标记为 Draft、撤下或明确增加警告，并启动 Hotfix。
- 回滚代码优先使用 `revert` 提交，避免重写公开历史。
- Hotfix 必须重新执行完整 Release 门禁，不能因为改动小而跳过签名、安装和启动检查。
- 新 APK 必须使用新版本号、新 `versionCode`、新 Tag 和新 SHA-256。

## 14. 不符合项与豁免

某条必须项无法满足时，Release Notes 或维护记录必须包含：

```markdown
## Release waiver
- 未满足的条款：
- 原因：
- 风险：
- 临时缓解措施：
- 计划修复版本：
- 批准人：
```

以下情况不得豁免：

- Release APK 无法安装或启动。
- 签名无效或来源不明。
- 已知成绩由错误计时、错误工作量或硬编码生成。
- 包含令牌、私钥、密码或用户隐私数据。
- Tag、Commit 或上传账号不属于授权维护者。

## 15. 最终发布清单

### 代码

- [ ] 需求范围明确，无无关重构。
- [ ] 公共接口兼容性已检查。
- [ ] 没有新增未经批准的依赖。
- [ ] 没有空错误处理或静默失败。
- [ ] 评分与计时逻辑有测试和说明。

### 测试

- [ ] 静态检查通过。
- [ ] 单元与回归测试通过。
- [ ] Debug 测试通过。
- [ ] Release 构建、安装、启动通过。
- [ ] 真机和模拟验证边界已记录。

### Git

- [ ] 工作区干净。
- [ ] Commit Message 符合 Conventional Commits。
- [ ] Commit Author 正确。
- [ ] `main` 与远端同步。
- [ ] Tag 名称和指向正确。

### 发布

- [ ] 版本号和 `versionCode` 正确。
- [ ] APK ABI 和签名正确。
- [ ] SHA-256 正确。
- [ ] README 与 CHANGELOG 已更新。
- [ ] Pre-release/Stable 状态正确。
- [ ] Release Author、Uploader 和下载链接已复核。
- [ ] Draft 阶段已经核对全部资产，发布后没有原地替换文件。
- [ ] 来源证明、SBOM 或对应豁免记录已保存。
- [ ] Release 和资产完整性验证通过。

只有所有必须项完成，或存在合规豁免记录时，版本才可以宣布发布完成。

## 16. 参考依据

本规范结合 RapidBench 的 Android Benchmark 特性，并参考以下公开标准和成熟项目实践：

- [Semantic Versioning 2.0.0](https://semver.org/)：版本号、预发布优先级和“已发布版本不得修改”。
- [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/)：可追踪的提交消息格式。
- [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/)：`[Unreleased]`、面向用户的分类变更记录和 Compare 链接。
- [GitHub Immutable Releases](https://docs.github.com/en/code-security/concepts/supply-chain-security/immutable-releases)：草稿准备、发布后锁定 Tag 和资产、Release Attestation。
- [GitHub Artifact Attestations](https://docs.github.com/en/actions/concepts/security/artifact-attestations)：构建来源证明和验证边界。
- [Android App Signing](https://developer.android.com/studio/publish/app-signing)：APK/AAB 签名、证书连续性和密钥保护。
- [Flutter Android Release Guide](https://docs.flutter.dev/deployment/android)：Android Release 构建、签名、混淆和发布产物。
- [Kubernetes Cluster API Release Cycle](https://github.com/kubernetes-sigs/cluster-api/blob/main/docs/release/release-cycle.md)：Beta、RC、Feature Freeze 和 Stable 阶段控制。

外部规范用于建立最低质量线；当外部通用规则与 RapidBench 的测试可信度要求冲突时，以更严格、可验证、可追踪的要求为准。
