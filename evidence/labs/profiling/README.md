---
type: Evidence
title: "性能定位可运行证据（插桩开销 / 卡顿检测 / Tick 预算与降级）"
description: "记录机器相关的插桩开销观察，独立验证插桩功能、文件 I/O、卡顿检测与预算降级规则。"
tags:
  - evidence
  - profiling
  - performance
  - gameplay
status: stable
verified: []
maturity: L0
updated: 2026-10-03
---

# 性能定位可运行证据

> 证据范围：两个标准 C++ 程序。插桩基准现在分开报告 **13 条功能检查**与 **7 条机器相关观察**；卡顿/预算程序的历史记录为 10 条功能断言通过。2026-10-03 本次只运行插桩回归，未重跑卡顿程序，未执行 Linux perf、UE Insights/Trace 或线上测试。`evidence/` 是维护基础设施，README 的 maturity 不参与知识成熟度门禁。

本目录回答：在热路径上留证据要花多少、卡顿事件如何判定、Tick 超预算后怎样用迟滞控制降级。计时结果是当前机器、工具链和这一次样本的观察；功能检查才决定 `profiling_overhead` 的退出码。

## 问题与输入

1. 比较裸计数、原子计数、两次时钟读取、字符串查找、格式化、缓冲文件写入与 1/100 采样。每种样式 200 轮 × 20 000 次调用；每轮归一成 ns/次，再取排序后的上中位数 p50。
2. 按 1 000 与 100 000 次/帧、16.6 ms 预算作线性换算。这些是假设调用量下的算术外推，**没有测量真实帧，也不构成预算门禁**。
3. 卡顿程序使用 30 秒、60 fps 序列（1 800 帧），基线 7–9 ms，注入 12 次 25–80 ms 卡顿事件，其中含连续 2–3 帧级联；另有纯抖动对照。
4. 卡顿阈值使用 `max(2.5 × median, 1.5 帧)`，连续 2 帧低于阈值 60% 才结束事件。预算模型有 ai 2 ms、physics 3 ms、network 2 ms、gameplay 4 ms；连续 3 次超预算升级、连续 30 次达标降级，级别上限 3。
5. 预算记账基准为 200 000 Tick × 4 系统，样本只表示该静态模型。

## 为什么拆分观察与功能检查

历史 `profiling_overhead` 把 P1–P7 的速度比例或预算外推全部写成 PASS/FAIL。Windows 存档恰好 7/0，但 Linux 上未改动源码的一次隔离运行得到 **6/1、退出 1**：P3 的格式化/计时比例是 **3.6x**，未满足原来的 `>5x` 假设。这是真实的旧假设失败，不能改写成旧运行成功，也不能通过放宽阈值消除它。

修复保留全部七个原阈值，输出 `OBSERVATION Pn SUPPORTED/UNSUPPORTED/UNAVAILABLE`。`UNSUPPORTED` 表示本次观测不支持该旧假设，**不表示功能错误**；`UNAVAILABLE` 表示没有合法测量，不能拿零值代替成功的文件写入。另用 `FUNCTIONAL PASS/FAIL` 与 `FUNCTIONAL_RESULT` 报告功能契约。任何功能失败都返回非零；本程序没有可配置性能预算门禁。

| 观察 | 原比较的精确含义（保留阈值） |
| --- | --- |
| P1 | atomic / bare `>= 0.5`；原名称“原子不比裸计数便宜”并不准确，实际阈值允许低至一半 |
| P2 | scoped / bare `> 1` |
| P3 | format / scoped `> 5` |
| P4 | format+write / format `> 1` |
| P5 | scoped / sampled `> 10`，等价于原 sampled `< scoped/10`（正数测量） |
| P6 | 文件日志开销外推至 100 000 次/帧后，log_ms / 16.6 `> 1` |
| P7 | 全量计时开销同样外推后，timer_ms / 16.6 `< 1` |

输出同时给出 numerator、denominator、ratio 和 threshold。它们都不承诺跨机器、编译选项、运行负载或采样轮次成立；比例也不能当作可迁移常数。

## 环境与运行方式

| 记录 | 环境 |
| --- | --- |
| 2026-09-11 历史结果 | Windows MINGW64_NT-10.0-26200，x86_64，16 逻辑核；MSYS2 MinGW-w64 g++ 16.1.0，C++17 `-O2` |
| 2026-10-03 修复前失败 | Linux 6.18.44 x86_64，Debian g++ 14.2.0，C++17 `-O2 -g -fno-omit-frame-pointer` |
| 2026-10-03 功能回归 | Linux 6.18.44 x86_64，9 逻辑核，glibc 2.41；Debian g++ 14.2.0，C++17 `-O2 -Wall -Wextra -pedantic`；Python 3.12.14 |

推荐从仓库根运行隔离回归（只依赖 Python 标准库和 `g++`）：

```bash
python3 evidence/labs/profiling/scripts/test_profiling_contract.py
```

脚本把源码按原字节复制到临时工作区，在那里编译并创建正常用例的 `build/`，随后运行一次正常基准、一次缺少 `build/` 的负例和一个确定性 fixture。临时目录结束时清理；不在仓库创建 build、不写现有结果文件。保存新证据时指定尚不存在的目录，已有目录会被拒绝：

```bash
python3 evidence/labs/profiling/scripts/test_profiling_contract.py \
  --record-dir evidence/labs/profiling/results/my-new-contract-run
```

原有两种运行脚本保留：

```powershell
& (Join-Path $RepoRoot 'evidence/labs/profiling/scripts/build_run.ps1')
```

```bash
bash evidence/labs/profiling/scripts/run_all.sh
```

**旧脚本会重新编译两个程序，并覆盖 `results/profiling_overhead.txt` 和 `results/hitch_and_budget.txt`**；不要把它们用于保全历史记录的回归。本次未运行或修改这些脚本。旧 Bash 脚本的总退出码只汇总编译失败，程序退出码写在日志中；需要可靠的插桩功能门禁时使用上面的新回归脚本。

## 功能契约与回归覆盖

- F01–F03：8 个裸计数器均恰好 500 000 次；原子计数器恰好 4 000 000 次；查找全部已知名称/不存在名称，验证查找后的全部计数器总数
- F04：格式化没有失败或截断
- F05–F12：文件必须打开、缓冲设置成功、文件格式化没有失败/截断、每次写入完整且总字节数匹配、flush 无错误、flush 后文件长度匹配、close 成功、临时文件删除且路径不存在
- F13：七种样式的每轮总时长及归一值必须为有限正数；无效参数不会运行测量回调。已移除无用途的 `p50 × calls × rounds` 伪“总耗时”，它并不是实际总时长
- 正常用例要求 13/0、退出 0、七个有效观察、文件清理；不要求任何特定的计时假设成立
- 缺少 build 的负例要求实际打开文件失败、退出非零、P4/P6 不可用，不能静默跳过后报成功
- 确定性 fixture 的 27 个检查覆盖原阈值的边界、3.6x 反例、非法比值、非法测量参数与退出码语义；不靠反复跑计时来得到有利结果。fixture 原始输出中的一次 `FUNCTIONAL FAIL fixture intentional failure` 是故意验证非零功能判定，fixture 自身最终为 `cases=27 fail=0`

文件写入的计数/错误检测会增加部分循环工作，样式与样本规模保留，但新旧程序不应当作仅一个变量不同的性能 A/B 测试。此回归真正注入的是“文件无法打开”；写/flush/close/删除的失败分支有检查，但本次没有分别注入这些故障，也未验证掉电持久性或文件内容逐字回读。

## 原始结果与解释

### 2026-09-11 历史 Windows 记录（原文件不改写）

历史记录中旧计时比较为 7/0，卡顿/预算功能断言为 10/0；这不能合并成“17 条可移植功能断言通过”。下表按原始日志准确转录，替换此前 README 中与日志不一致的手工数字：

| 样式 | p50 ns/次 | 100 000 次/帧的外推 ms |
| --- | ---: | ---: |
| 裸计数 | 0.32 | 0.032 |
| 原子计数 | 3.84 | 0.384 |
| 全量计时 | 50.03 | 5.003 |
| 字符串查找 | 5.68 | 未在原预算表输出 |
| 格式化 | 274.67 | 27.467 |
| 格式化 + fwrite | 308.45 | 30.845 |
| 1/100 采样 | 0.96 | 0.096 |

该历史样本里格式化/计时为 5.5x；文件日志的外推占预算 185.82%，全量计时为 30.14%，采样为 0.58%。这些说明该样本下每调用日志的成本值得注意，不证明所有机器都会超预算，也不证明每种负载都应该用相同采样策略。

卡顿记录中 12 个注入事件得到 12 个检测事件、18/18 个卡顿帧命中、纯抖动样本 0 误报；迟滞用例均通过。预算记账原始输出是 **1.8 ns/Tick、0.5 ns/系统**，不应外推为所有实际项目均可忽略。

原档：[profiling_overhead.txt](results/profiling_overhead.txt) ｜ [hitch_and_budget.txt](results/hitch_and_budget.txt)

### 2026-10-03 原源码 Linux 失败记录

原始输出保留 `RESULT pass=6 fail=1`，P3 为 timer=49.42 ns、format=178.36 ns、ratio=3.6x，退出 1。文件样式实际测得 197.91 ns，文件成功打开且清理，不能把这次失败归因于缺少 build。

[未改写的旧失败 stdout](results/profiling_overhead_legacy_linux_2026-10-03.stdout.txt) ｜ [来源、源码 SHA-256 与重现命令](results/profiling_overhead_legacy_linux_2026-10-03.provenance.json)

### 2026-10-03 新功能契约记录

一次正常运行得到 **FUNCTIONAL_RESULT pass=13 fail=0、退出 0**；仍有 **OBSERVATION_RESULT supported=6 unsupported=1 unavailable=0**，P3 为 **3.605170x**，旧 `>5x` 假设仍不成立。文件写入返回字节数和 flush 后文件长度均为 165 334 000。负例缺少 build 时功能失败、退出 1，P4/P6 标记不可用。回归没有选择性重跑直到比值变好。

[正常运行原始 stdout](results/profiling-contract-2026-10-03/normal.stdout.txt) ｜ [缺少 build 的负例 stdout](results/profiling-contract-2026-10-03/missing_build.stdout.txt) ｜ [确定性 fixture stdout](results/profiling-contract-2026-10-03/fixture.stdout.txt) ｜ [环境、命令、退出码和源码/测试 SHA-256](results/profiling-contract-2026-10-03/provenance.json)

## 局限

- 单机、单线程、合成 CPU 逻辑，没有真实渲染/物理或 DS 负载；无 CPU 固定、隔离负载或置信区间。p50 不描述尾延迟
- `fwrite` 使用标准库缓冲，请求 1 MiB 不等于实现保证该实际缓冲大小；测量可能包括缓冲填满时的写入，受文件系统、页缓存、存储与环境负载影响。最终 flush/close/清理在计时区外，未做 fsync 或持久性保证
- C++17、优化器、CPU、时钟实现、标准库格式化与运行噪声都可改变绝对值及相对比例；不能把 Windows/Linux 差异单独归因于某一个因素
- 未覆盖 UE Insights、Trace 通道、Stats、ProfileGPU、Stat NamedEvents 或 Linux perf。运行此基准不需要 perf 权限，也未读取硬件性能计数器
- 卡顿检测是简化模型，不覆盖 GPU 等待/加载/GC 分型、多阈值分档；静态系统预算不代表不同玩法场景的真实预算
- 未覆盖采样偏差置信区间、线上采样配置、故障注入全矩阵或真实帧级预算回归

## 关联知识文档

- [系统实战/10-性能问题定位完整链路](../../../知识/08-工程实践与质量/调试与性能分析/10-性能问题定位完整链路.md)（本证据的主要使用者）
- [笔记/插桩测试](../../../知识/08-工程实践与质量/调试与性能分析/插桩测试.md)、[笔记/perf性能分析](../../../知识/08-工程实践与质量/调试与性能分析/perf性能分析.md)（方法速查）
- [00-计算机与工程基础/07-Linux系统编程/03-性能工具：插桩与perf采样](../../../知识/08-工程实践与质量/调试与性能分析/03-性能工具：插桩与perf采样.md)
- [游戏知识/07-UI与性能优化/03-性能分析工具与Profiling](../../../知识/08-工程实践与质量/调试与性能分析/03-性能分析工具与Profiling.md)
- [游戏服务端/06-世界模拟与运行时/14-运行时背压与过载保护](../../../知识/07-网络与游戏服务端/运行调度与过载保护/14-运行时背压与过载保护.md)（预算与降级的上层策略）
- [evidence/tests/damage-core](../../tests/damage-core/README.md)、[evidence/tests/gameplay-core](../../tests/gameplay-core/README.md)（同样使用"分批计时再归一"的度量方法）
