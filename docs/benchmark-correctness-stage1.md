# 正确性改进：阶段 1 验证记录

日期：2026-09-12。基线：`baf36fd`（1.0.4 Beta 5）。开发分支：`geisha/benchmark-correctness`。
本记录属于 Unreleased 开发验证，不是新 Release 的验收公告。

## 改动

- 修复 5 个 Compute Shader 漏用输出 offset；16 区回绕前保持互不重叠。
- GPU 在计时外清空输出，验证全部可用 FP 8/12/16 变体，测量后校验每个已写区。
- FP32、FP16、INT32、GPU Memory 使用独立标量参考；FP16 允许 RTE/RTZ 舍入范围。
- Mixed 的浮点位反馈会放大驱动舍入差异，因此单独采用数学边界与同设备重复性验证；最终位转换限制为有限值。
- 每个 Compute 输出区采样 8 个 invocation × 4 个分量，完整 16 区共 512 值；Memory 采样 8 值。
- 增加 Shader 写入到 Host 读取的可见性屏障并检查映射失效返回值；不计入吞吐分母。
- 结果错误时清空当前项目临时成绩，保留其他已完成项目。
- CPU worker checksum 改为原子发布，消除快照与最终写入之间的数据竞争。
- 后续停止路径复查：GPU 部分结果也经过校验；新运行清除本轮旧分数，准备阶段取消不会冒用旧结果；分配错误归属正确当前项目。

CPU 主分数公式、Memory/Storage 算法、FFI 结构大小和现有 UI 不变。GPU 方法标识为
`gpu-throughput-v2`，不要直接与旧方法结果比较，也不人为调整结果以贴近理论峰值。

## 已执行验证

所有输出位于本地 `artifacts/correctness-stage1-*`，不作为发布附件自动上传。

| 验证 | 结果与边界 |
| --- | --- |
| Android arm64 Release 配置原生构建 | 通过；存在 25 条 Vulkan 结构聚合初始化告警，不是零告警构建 |
| Shader 源码结构检查 | 12 个通过；检查输出 offset、64 次迭代、8/12/16 路 FMA 与 Memory 加载数 |
| `gpu_validation_test` | 通过；区域回绕、运算数、独立 INT32 golden、参考值、末区破坏、NaN/Inf/零、Mixed 重复性反例 |
| `cpu_publication_test` | 通过；12 次交替单/多线程与两个并发快照观察者；不是 ThreadSanitizer |
| 其余原生回归 | `phase2_smoke_test`、`gpu_timing_test`、`memory_timing_test`、`topology_grouping_test`、`memory_engine_test`、`storage_routes_test`、`storage_sqlite_test` 均通过 |
| 真机 GPU 完整测试 | `gpu_device_smoke --full` 通过；约 6 秒/项；含 Stop/Restart、全部 FP 变体、INT32、Mixed 和 256 MiB Memory |
| Flutter 静态检查 | 通过，无问题 |
| Flutter 测试 | 15 项通过，包括 320px 窄屏与 2 倍字体 |
| Debug APK 构建 | arm64-v8a 构建通过，未安装；SDK XML 版本提示不影响构建 |

当次实际设备：OnePlus PJZ110，Snapdragon 8 Elite / Adreno 830，Android 16，Vulkan 1.3.284。
第一阶段 Debug 已归档到：`artifacts/RapidBench-correctness-stage1-arm64-debug.apk`；
构建日志：`artifacts/correctness-stage1-debug-build.log`。这是本地开发包，未替换公开 Beta 5 资产。
共 10 个原生可执行测试全部返回 0；另有 1 个 Host Shader 检查。完整 GPU 回归日志为
`artifacts/correctness-stage1-gpu_device_smoke.log`。

该次完整 GPU 功能测试样本：FP32 3028.784 GFLOPS，FP16 3149.065 GFLOPS，INT32 1475.794 GOPS，
Mixed 109.283 Gwork/s，Memory 60.386 GB/s。这些数值用于确认测试流程有有效输出，
不是控制温度/电量后的正式横向性能结论。

## 尚未覆盖

- 第一阶段初次验收仅运行 ADB 原生程序，未安装第一阶段归档 APK；后续已安装包含阶段 2-A 的开发包并完成所列 App 端到端回归，见下方增量记录。
- 没有 Exynos、Mali、PowerVR、旧麒麟或其他机型真机，模拟拓扑不等于对应设备实测。
- 样机支持原生 FP16；模拟 FP16 的参考/Shader 构建通过，但本次未在不支持 FP16 的真实驱动执行。
- 未运行 Vulkan Validation Layers、ThreadSanitizer 或 GPU 反汇编审计；采样验证不能证明所有 invocation 的正确性。
- CPU 应用型负载已在后续阶段 2-A 实现并单独验证；内存延迟/扫描、完整结果元数据、持续性统计仍未完成。
- 未变更公开版本、签名、Tag、Release 或 GitHub 内容。发布时按发布规范另行验收。

## 后续增量回归

加入 CPU 应用型子项后，最终回归记录使用 `artifacts/improvement-final-*` 前缀，
与本页初次验收日志分开保存。新的本地 Debug 使用 `0.0.0-dev` 开发标记，不修改源文件中的
公开 Beta 5 版本信息，不发布。该合并开发包已安装到 PJZ110（同签名、保留数据），完成原 CPU
互斥/后台取消、Memory 全流程、Storage 全部 11 项（含 QD8/Q1T1/Q1T4 与 SQL 三项）、GPU 全流程。
随后又安装自动选核更新的 Debug 包，完成新增 CPU 子项专项回归。
详细证据、包摘要和测试边界见 [开发版真机验证记录](benchmark-improvement-device-validation.md)。
