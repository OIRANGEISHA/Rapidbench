# Changelog

## [Unreleased]

## [1.1.1-beta.7] - 2026-09-23

### Fixed

- Rank CPUs using one consistent evidence source: complete capacity data, otherwise complete maximum-frequency data, otherwise an explicitly unknown fallback. Do not mix missing capacity values with unrelated frequency ranks. Infer distinct performance groups when a constant cluster/policy ID hides different ranks.
- Label frequency-inferred and unknown automatic single-core choices. Keep automatic Single / all-present-core Multi for CPU application workloads, without adding selectors.
- Isolate Vulkan pipeline availability by test; fall back to explicitly labelled FP32 emulation when native FP16 pipeline creation fails. Skip unavailable items in All, retain strict numerical validation, and stop globally on device loss/submission failures.
- Add per-test GPU state and host/timestamp batch diagnostics. Aggregate timing no longer hides earlier host-fallback batches. GPU workload, operation counting, workgroup size and timing acceptance thresholds are unchanged (`gpu-throughput-v2`).
- Recheck available CPUs after Memory warm-up with at most two re-preparations, then freeze workers before measurement. Report restricted/unstable CPU availability; retain read/write kernels and bidirectional system `memcpy()` accounting.
- Fix Peak falling below the final CPU score on short or scheduler-delayed runs with too few score windows: use the measured whole-run throughput as a peak fallback, consistent with the existing final-score fallback. The main scoring formula is unchanged.
- CPU 排名统一使用完整 capacity、完整最高频率或明确标记的未知回退，不再混用缺失数据；常量簇/策略编号掩盖性能差异时推断性能分组，并提示自动选核的依据与不确定性。
- GPU 按项目隔离管线不可用，原生 FP16 失败时明确标注 FP32 模拟；保留数值校验与全局故障停止，增加逐项状态和计时回退诊断，不修改工作负载、操作计数或计时阈值。
- 内存预热后有界重检核心，测量前固定 worker；显示系统限制或核心可用性变化，不修改读写/复制算法和统计口径。
- 修复极短或调度延迟测试中 Peak 偶尔低于最终分数的边界问题，不修改 CPU 主分数公式。

### Changed

- Set Android version to `1.1.1-beta.7` / `4021`. Existing C ABI structures, storage routes, signing and dependencies remain unchanged; diagnostics APIs are additive.
- 跨品牌场景采用合成拓扑与故障注入验证，不等同于其他机型实测；验证范围见 `docs/compatibility-beta7.md`，发布记录见 `docs/releases/1.1.1-beta.7.md`。

## [1.1.0-beta.6] - 2026-09-12

### Added

- Add optional CPU application-style workloads: 65,536-key `std::sort`, fixed-schema ASCII JSON record parsing, and a 1024×1024 grayscale Sobel filter. Report independent Mkeys/s, MB/s and MPix/s, with no composite score or change to legacy CPU scores.
- Add automatic single-worker execution on the detected highest-performance core or independent-multi-worker execution on all present cores, without core/cluster selectors for these new tests. Keep 700 ms warm-up and 3-second measurement, full reference checks outside timing, and cancellation-safe partial results.
- Add a collapsible CPU-page section with separate units, per-worker input size, method version and affinity fallback notices; reuse global run ownership and background-stop rules.
- 新增可选 CPU 应用型负载：65,536 键 `std::sort`、固定结构 ASCII JSON 记录解析、1024×1024 灰度 Sobel 滤镜，分别报告 Mkeys/s、MB/s、MPix/s，不生成综合分数，不改变原 CPU 主分数。
- 新增子项的单 worker 自动选择检测到的最高性能核，多 worker 自动使用全部 present 核心，不提供核心/簇选择，不受旧卡片选择影响；预热 700 ms、测量 3 秒，计时外完整参考校验，停止后保留已验证的部分结果。
- CPU 页面新增可折叠入口，单位单独排布，显示每 worker 输入规模、方法版本与亲和性回退；复用全局互斥和后台停止规则。

### Fixed

- Correct GPU output-ring addressing in the 8-chain FP32/FP16, INT32 and mixed shaders. Bound the mixed shader's final integer-to-float contribution to prevent NaN/Inf outputs.
- Validate every available FP variant and sampled results in every written output region outside the measured interval. Use scalar CPU references for FP/INT/memory, and explicitly separate bounded, same-device repeatability checks for mixed compute. Invalid output clears the active provisional result.
- Publish CPU worker checksums atomically; the existing CPU workload and scoring formula are unchanged.
- Validate completed GPU batches even when stopped; clear the requested test's stale value at restart, and associate allocation errors with the correct active item.
- 修复 8 路 FP32/FP16、INT32、Mixed Shader 的输出环形区域寻址；限制 Mixed 最终位转换的范围，避免 NaN/Inf。
- 在计时外校验全部可用浮点变体，以及每个已写输出区的采样结果；FP/INT/Memory 使用独立 CPU 参考，Mixed 明确采用数值边界与同设备重复性验证。无效输出不保留当前项目的临时成绩。
- CPU worker 校验值改为原子发布；原 CPU 工作负载和计分公式不变。
- GPU 中途停止也会校验已完成批次；重启清除本轮旧分数，分配失败准确归属当前项目。

### Changed

- Identify the corrected GPU method as `gpu-throughput-v2`. Compare GPU results only within the same method; this does not establish the cause of historical results on untested GPU drivers.
- GPU 方法标识更新为 `gpu-throughput-v2`。GPU 成绩应在同一方法版本内比较；本次修复不能证明未实测驱动上的历史异常分数根因。
- Set Android version to `1.1.0-beta.6` / `4020`. Memory/Storage algorithms, existing score units, published FFI layouts and dependencies are unchanged. The new CPU API is additive.
- Android 版本更新为 `1.1.0-beta.6` / `4020`。Memory/Storage 算法、已有分数单位、已发布 FFI 结构与依赖不变，新 CPU API 为增量接口。
- The four-stage improvement plan is not complete: memory latency/scale sweeps, unified run metadata/export and sustained/repeated statistics remain future work.
- 四阶段计划尚未全部完成：内存延迟/规模扫描、统一运行条件/导出、持续/重复测试统计仍未实现。

## [1.0.4-beta.5] - 2026-09-08

### Fixed

- Prevent simultaneous CPU, Memory, Storage, and GPU runs, including preparation, multi-item sequences, cancellation, and cleanup. Page navigation remains available while other start controls are disabled.
- Request a cooperative stop when the App enters the background; keep ownership until native work has finished and do not automatically resume a sequence.
- Reclaim the large Memory buffer and Storage scratch file before their completed/stopped state can unlock another module.
- Correct final memory bandwidth timing to include every counted pass through its actual completion, without deadline truncation or coordinator polling/join overhead. Affected results may decrease; kernels and byte counting are unchanged.
- Wire Release signing to the explicitly configured keystore and alias instead of validating the environment but still selecting the Debug signing configuration. Retain the existing Beta certificate for upgrades.
- 防止 CPU、Memory、Storage、GPU 同时跑分；互斥覆盖准备、连续项目、取消及清理阶段，页面仍可切换。
- 应用进入后台时请求停止，等待原生工作结束后释放占用，不自动续跑。
- 在发布内存或存储终态前回收大缓冲区及临时文件，避免其回收开销影响刚启动的下一模块。
- 修正内存最终带宽的计时分母，完整计入末次已完成 pass 的耗时，排除轮询与线程回收等待；受影响结果可能降低，测试内核及字节口径不变。
- Release 签名真正采用显式配置的密钥库及别名，继续沿用原 Beta 证书以保持覆盖升级兼容。

### Changed

- Clarify sequential Direct QD8 versus Buffered Q1T1 in both READMEs; all 11 Storage tests and their IDs remain unchanged, including 4K Q1T1, Q8T1, Q1T4 and SQLite Insert/Update/Delete.
- Add automated ownership, timing, Storage route/fallback, narrow-screen, large-number, and text-scaling regression coverage, plus an Android UI smoke script.
- Set the Android version to `1.0.4-beta.5` / `4019`. No dependency, CPU/GPU workload, or FFI ABI changes.
- 中英文文档明确顺序 Direct QD8 / Buffered Q1T1；保留全部 11 项 Storage 测试和既有 ID，没有遗漏 Q1T1、Q8T1、Q1T4 或 SQL Update。
- 新增互斥、计时、Storage 路径/回退、窄屏、大数字和字体缩放回归，以及 Android 界面冒烟脚本。
- Android 版本更新为 `1.0.4-beta.5` / `4019`，未变更依赖、CPU/GPU 工作负载或 FFI ABI。

## [1.0.3-beta.4] - 2026-09-02

### Added

- Added an independently runnable SQLite Update benchmark to the Storage module, alongside SQLite Insert and Delete.
- The update workload reuses prepared statements and deterministic row selection while changing indexed, text, timestamp, and payload fields.
- 在 Storage 模块新增可单独运行的 SQLite Update 测试，与 SQLite Insert、Delete 并列显示。
- Update 测试复用预编译语句和确定性行选择，同时更新索引值、文本、时间戳与负载字段。

### Changed

- Prepared the Android app version as `1.0.3-beta.4` with build number `4018`.
- Android 应用版本更新为 `1.0.3-beta.4`，构建号更新为 `4018`。

## [1.0.2-beta.3] - 2026-09-02

### Added

- Added an About App entry to the Device module. It displays the installed package version and the RapidBench GitHub repository URL.
- The GitHub action opens the repository in the device's browser through Android's native `ACTION_VIEW` intent without adding a third-party Flutter dependency.
- 在 Device 模块新增 About App 入口，显示实际安装包版本和 RapidBench GitHub 仓库地址。
- 点击 GitHub 按钮会通过 Android 原生 `ACTION_VIEW` 唤起手机浏览器，不新增第三方 Flutter 依赖。

### Changed

- Prepared the Android app version as `1.0.2-beta.3` with build number `4017`.
- Android 应用版本更新为 `1.0.2-beta.3`，构建号更新为 `4017`。

## [1.0.1-beta.2] - 2026-08-29

### English

This preview focuses on CPU topology compatibility and GPU timing integrity.

- Fixed 10-core and other non-eight-core devices being limited to eight multi-core workers. CPU benchmark worker selection now follows every kernel-reported present CPU in the selected group or across the device.
- Fixed missing or unselectable CPU clusters on older kernels by detecting global CPUFreq `policy*` directories and their `related_cpus` / `affected_cpus` masks when per-CPU CPUFreq links are unavailable.
- Changed CPU affinity failures from a fatal benchmark error into a visible best-effort fallback. Workers continue producing load, periodically retry binding, and keep the affinity warning and failure count available for diagnosis.
- Updated single-core and multi-core selectors to use dynamically detected present cores and performance groups instead of assuming a fixed cluster count.
- Added defensive Vulkan timing validation. GPU timestamp results are compared with the independently measured host fence duration; zero, invalid, or implausible samples fall back to host timing instead of producing an inflated compute score.
- Added regression tests for a simulated 10-core topology with two initially unavailable cores, restricted-affinity behavior, legacy CPUFreq policy discovery, and invalid or distorted GPU timestamps.

Real-device validation was completed on a PJZ110 with Snapdragon 8 Elite and Adreno 830. Xring O1, Exynos 2600, Snapdragon 865, Kirin 990, and Kirin 980 compatibility paths were validated through topology and timing simulations because those devices were not available for direct testing.

### 简体中文

本预览版本主要修复 CPU 拓扑兼容性和 GPU 计时可信度问题。

- 修复部分 10 核及其他非 8 核设备在多核测试中最多只能创建 8 个 worker 的问题。现在会按照内核报告的全部 present CPU，为所选簇或全核心测试创建 worker。
- 修复部分旧内核中 CPU 簇缺失、无法选择的问题。逐 CPU CPUFreq 链接不可用时，会改为读取全局 CPUFreq `policy*` 目录以及 `related_cpus` / `affected_cpus` 掩码。
- CPU affinity 失败不再直接导致跑分报错。Worker 会继续施加负载并定期重试绑定，同时保留亲和性警告和失败次数供排查。
- 单核及多核选择器改为使用动态检测到的 present CPU 和性能分组，不再假设固定的核心数或 CPU 簇数量。
- 增加 Vulkan GPU 计时防护。GPU Timestamp 会和独立测得的 Host Fence 时间交叉校验；时间戳为零、无效或明显不合理时会回退到 Host Timing，避免出现异常虚高的浮点分数。
- 新增回归测试，覆盖模拟 10 核但初始有两个核心不可用、亲和性受限、旧式 CPUFreq policy，以及 GPU 时间戳无效或失真的情况。

本版本已在搭载骁龙 8 Elite 和 Adreno 830 的 PJZ110 上完成真机验证。由于暂时没有对应设备，玄戒 O1、Exynos 2600、骁龙 865、麒麟 990 和麒麟 980 仅完成了拓扑及计时路径模拟验证，没有将其描述为真机实测。

[Unreleased]: https://github.com/OIRANGEISHA/Rapidbench/compare/v1.1.1-beta.7...HEAD
[1.1.1-beta.7]: https://github.com/OIRANGEISHA/Rapidbench/compare/v1.1.0-beta.6...v1.1.1-beta.7
[1.1.0-beta.6]: https://github.com/OIRANGEISHA/Rapidbench/compare/v1.0.4-beta.5...v1.1.0-beta.6
[1.0.4-beta.5]: https://github.com/OIRANGEISHA/Rapidbench/compare/v1.0.3-beta.4...v1.0.4-beta.5
[1.0.3-beta.4]: https://github.com/OIRANGEISHA/Rapidbench/compare/v1.0.2-beta.3...v1.0.3-beta.4
[1.0.2-beta.3]: https://github.com/OIRANGEISHA/Rapidbench/compare/v1.0.1-beta.2...v1.0.2-beta.3
[1.0.1-beta.2]: https://github.com/OIRANGEISHA/Rapidbench/releases/tag/v1.0.1-beta.2
