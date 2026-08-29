# Changelog

## 1.0.1 Beta 2

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
