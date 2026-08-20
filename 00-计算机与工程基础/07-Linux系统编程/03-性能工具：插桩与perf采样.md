# 03-性能工具：插桩与perf采样
> 验证与基准：按文中命令执行最小实验，记录结果与边界。

> 知识成熟度：L2（本机未执行采样实验，命令与流程按官方资料核对；执行后按证据升级 L4）。

> 知识基线：Linux perf（`perf_event_open` 内核子系统、硬件性能计数器、周期采样）、C++ 计时（`std::chrono::steady_clock`、`QueryPerformanceCounter`、`rdtsc`）、编译器插桩（`-finstrument-functions` / `-pg`）。
> 版本基准：perf 为 Linux 内核自带工具（`tools/perf`），命令行为随内核版本演进；本文命令在 5.x 内核验证口径下给出，具体发行版以 `perf --version` 与内核文档为准。
> 适用范围：游戏服务器进程（AI 线程/逻辑线程/网络线程）与自研 C++ 服务端的耗时定位；不覆盖 UE 客户端专用工具链（见 [游戏知识/07-UI与性能优化/03-性能分析工具与Profiling](../../游戏知识/07-UI与性能优化/03-性能分析工具与Profiling.md)）。
> 事实边界：本文给出的是方法、命令与判读口径；所有采样结果、IPC 数值与火焰图结论必须在目标机器上复现后才是证据。示例中的二进制名、PID 与路径均为示意。
> 官方参考：[perf(1) man page](https://man7.org/linux/man-pages/man1/perf.1.html)、[perf record(1)](https://man7.org/linux/man-pages/man1/perf-record.1.html)、[Brendan Gregg's Perf Examples](https://www.brendangregg.com/perf.html)、[FlameGraph](https://github.com/brendangregg/FlameGraph)。
> 最后更新：2026-08-14（由笔记/插桩测试.md 与 笔记/perf性能分析.md 收敛为 canonical，2026-08-14 R4-OBS-01）。

## 1. 概述

耗时点测试回答两个问题：**"时间花在哪"** 与 **"每个逻辑阶段具体多少"**。本文给出互补的两类方法：

1. **插桩测试（Instrumentation）**：在代码中插入计时/统计代码，直接测量指定代码段的耗时。属于侵入式测量，精确但需要改代码，能拿到逻辑层信息（调用次数、失败率、分支命中）。
2. **perf 采样（Sampling Profiling）**：Linux 内核自带工具，基于硬件性能计数器与周期采样，无侵入地统计"时间都花在哪"。适合验证插桩结论、发现插桩未覆盖的底层问题（缓存缺失、锁竞争、系统调用、内存分配）。

推荐流程：**插桩粗定位 → perf 细验证/查漏 → 优化 → 再对比**。

目标读者：游戏服务器工程师（自研网关/登录服/匹配服/AI 线程）、C++ 服务端开发者；前置：[01-C++对象生命周期与RAII](../01-C++核心/01-C++对象生命周期与RAII.md)（RAII 计时器的基础）、[05-并发与高性能](../../游戏服务端/01-架构与网络/05-并发与高性能.md)（多线程与锁竞争背景）。

## 2. 核心概念

| 术语 | 含义 | 说明 |
| --- | --- | --- |
| 插桩 | Instrumentation | 代码内插入计时/统计代码，侵入式 |
| 采样 | Sampling | 周期性打断 CPU 记录当前函数与调用栈，无侵入 |
| 热点 | Hotspot | 耗时占比最高的函数/代码段 |
| self time | 自耗时 | 函数自身（不含子调用）的采样占比 |
| IPC | Instructions Per Cycle | 每周期指令数，低说明存在等待 |
| 火焰图 | Flame Graph | 横轴采样占比、纵轴调用栈的可视化 |
| 单调时钟 | Monotonic clock | `steady_clock`，不受系统时间跳变影响 |
| 性能计数器 | Hardware counter | CPU 内置计数器（cycles、cache-misses 等） |

### 2.1 两类方法的定位

| 维度 | 插桩 | perf 采样 |
| --- | --- | --- |
| 侵入性 | 需要改代码 | 无需改代码 |
| 精度 | 精确（微秒级） | 近似（按采样频率） |
| 逻辑层信息 | 调用次数、失败率、分支 | 无（只有调用栈） |
| 覆盖范围 | 只覆盖已插桩代码 | 全进程/全系统 |
| 生产环境 | 需开关机制 | 可直接挂载运行中进程 |
| 典型用途 | 圈定逻辑热点 | 验证与查漏（缓存/锁/系统调用） |

## 3. 插桩测试

### 3.1 手动打点

```cpp
auto t0 = steady_clock::now();
// 被测代码段
auto t1 = steady_clock::now();
acc += duration_cast<microseconds>(t1 - t0);
```

时钟选择：

- C++：`std::chrono::steady_clock`（单调时钟，避免系统时间跳变）；
- Windows：`QueryPerformanceCounter`（QPC，高分辨率）；
- x86：`rdtsc`（注意乱序执行，需配合 `cpuid`/`lfence` 或使用 `rdtscp`）。

统计维度：调用次数、累计耗时、平均、最大、耗时占比。

### 3.2 RAII / 宏封装

进入作用域时构造计时器，退出时自动累加（RAII），避免漏写结束点；用宏开关（如 `#ifdef PROFILE`）控制编译，线上版本直接关闭。

```cpp
#ifdef PROFILE
#define SCOPED_TIMER(name) ScopedTimer _t_##name(#name)
#else
#define SCOPED_TIMER(name)
#endif
```

### 3.3 编译器自动插桩

- GCC/Clang `-finstrument-functions`：在每个函数进出调用 `__cyg_profile_func_enter/exit` 回调；适合全量分析，但开销大、函数粒度粗。
- `-pg`（gprof）：编译期插桩 + 运行时统计，适合小规模单进程程序。
- 动态插桩（Intel Pin / DynamoRIO / 商业 APM）：无需改源码，运行时注入，但部署复杂。

### 3.4 轻量设计要点

- 用线程局部存储（`thread_local`）累加，避免加锁竞争；
- 热路径上只做累加，周期性（每秒）输出一次汇总，避免日志 I/O 干扰测量；
- 环形缓冲区记录耗时分布，供离线分析（分位数、长尾）。

## 4. perf 采样

### 4.1 常用命令

| 命令 | 作用 |
| --- | --- |
| `perf top` | 实时查看当前热点函数（类似 top） |
| `perf stat` | 统计整体性能事件（CPU、cache miss、分支预测、IPC 等） |
| `perf record` | 采样录制，生成 perf.data |
| `perf report` | 分析 perf.data，查看热点与调用链 |
| `perf annotate` | 看热点函数内指令级热区（汇编/源码级） |
| `perf script` | 导出原始采样数据（用于生成火焰图） |
| `perf sched` | 调度/等待分析 |

### 4.2 典型流程（挂载服务器进程）

```bash
# 1) 找到进程 PID
pgrep -f server_binary

# 2) 采样 60 秒：99Hz 采样频率（避开 100Hz 定时器谐振），记录调用栈
perf record -F 99 -g -p <PID> -- sleep 60

# 3) 分析结果
perf report                    # 交互界面：按 self/total 排序，Enter 展开调用栈
perf report --stdio --sort overhead,symbol
perf annotate --stdio          # 查看函数内各指令占比

# 4) 生成火焰图（Brendan Gregg 的 FlameGraph 工具）
perf script > out.perf
./stackcollapse-perf.pl out.perf > out.folded
./flamegraph.pl out.folded > flame.svg
```

若二进制没有 frame pointer（如 `-O2` 默认），`-g` 可能采不到调用栈，改用 `--call-graph dwarf`。

### 4.3 火焰图怎么看

- 横轴：采样占比（条越宽越热）；纵轴：调用栈（下层是调用者）；
- 找"宽条"：某个函数自身采样最多 = 自耗时热点（self time）；
- 看调用链：宽条是被谁调用起来的，判断是"函数本身慢"还是"被调用次数太多"；
- 优化前后各生成一张，对比宽条变化验证优化效果。

### 4.4 关键指标（perf stat）

| 指标 | 含义 | 低/高说明 |
| --- | --- | --- |
| `cycles` / `instructions` → IPC | 每周期指令数 | 低说明存在等待（缓存缺失、分支预测失败、访存瓶颈） |
| `cache-misses` / `cache-references` | 缓存命中率 | A* 节点对象分散时往往命中率低，是隐藏杀手 |
| `branch-misses` | 分支预测失败率 | 热循环里的复杂分支会显著拖慢 |
| `context-switches` | 切换频繁 | 说明锁竞争 / 调度问题 |
| `page-faults` | 缺页多 | 说明内存访问模式差或分配频繁 |

### 4.5 常见问题

- **权限**：`kernel.perf_event_paranoid` 限制采样能力，级别 2 时只能分析自己进程的用户态；必要时 `sudo sysctl kernel.perf_event_paranoid=1` 或安装 debuginfo；
- **符号不显示**：需要 `-g` 调试信息编译（`-O2 -g`），或安装对应 debuginfo 包；strip 过的二进制只能看到地址；
- **采样频率**：99Hz 是常用经验值（与系统定时器 100Hz 错开）；频率越高越准但开销越大；
- **采样误差**：采样是近似统计，短函数/低频函数可能被低估，需结合插桩验证；
- **多线程**：默认跟踪目标进程的所有线程；加 `-t <tid>` 只跟单线程，`-a` 全系统采样。

## 5. 服务器场景的统计模式

### 5.1 按 Tick 统计

- 每 Tick 内 A* 总耗时、超过预算的 Tick 次数（即卡顿点）；
- 分层统计：整体寻路 → Open List 操作 → 邻居扩展 → 路径重建，各阶段占比相加约 100%，一眼看出大头；
- 对比实验：同一压测场景，优化前后各跑相同时间，比较累计耗时与最大值。

### 5.2 游戏进程分析要点

- 先确认热点在哪个线程（AI 线程 / 逻辑线程 / 网络线程）；
- 区分"每 Tick 固定开销"与"偶发尖峰"：尖峰往往来自 GC、全量遍历、临时分配；
- 控制变量对比：同一压测场景，优化前后各采样一次，比较火焰图与 IPC；
- 与插桩配合：**perf 回答"时间花在哪"，插桩回答"每个逻辑阶段具体多少"**。

### 5.3 与压测/容量评估的衔接

插桩与 perf 产出的数据（单 Tick 耗时、P95/P99、CPU 占比）是容量模型与压测验收的输入：

- 单实例 CCU 拐点依赖"单 Tick 预算 × 玩家数"的实测数据（见 [游戏测试与质量/07-UE DS机器人压测与容量评估](<../../游戏测试与质量/07-UE DS机器人压测与容量评估.md>)）；
- 服务端 AI 预算与降频的落地见 [游戏AI/02-移动学习与服务端/04-服务端AI与性能](../../游戏AI/02-移动学习与服务端/04-服务端AI与性能.md)（§3.2 AI 预算与降频）；
- 世界模拟的 Tick 预算分配见 [游戏服务端/06-世界模拟与运行时/11-AI与寻路时间预算](../../游戏服务端/06-世界模拟与运行时/11-AI与寻路时间预算.md)。

## 6. 术语速查

| 术语 | 含义 |
| --- | --- |
| Instrumentation | 插桩：代码内计时/统计 |
| Profiling | 采样分析：周期性打断记录 |
| Hotspot | 热点：耗时占比最高的函数 |
| Self time | 自耗时（不含子调用） |
| Total time | 含子调用的总耗时 |
| IPC | 每周期指令数 |
| Flame Graph | 火焰图：调用栈可视化 |
| Off-CPU | 线程等待（锁/IO/调度）时间分析 |
| `perf_event_paranoid` | 内核采样权限级别 |

## 7. 落地检查清单

- [ ] 插桩用 `steady_clock`（非 `system_clock`）；
- [ ] 热路径只累加、周期输出，无日志 I/O；
- [ ] 插桩统计用 `thread_local`，无锁竞争；
- [ ] 线上版本通过宏开关关闭插桩；
- [ ] perf 采样前确认 `perf_event_paranoid` 权限；
- [ ] 二进制带 `-O2 -g`（保留 frame pointer 或用 `--call-graph dwarf`）；
- [ ] 采样频率 99Hz（避开 100Hz 谐振）；
- [ ] 火焰图优化前后各一张，同场景对比；
- [ ] perf stat 记录 IPC/cache-misses/branch-misses/context-switches；
- [ ] 结论回填容量模型或 Tick 预算文档。

## 8. 常见反模式

1. **用 `system_clock` 计时**：系统时间跳变（NTP/手动改时）导致负数或突变；
2. **插桩里写日志**：I/O 阻塞污染测量结果；
3. **只测平均不测 P95/P99**：尾部体验（卡顿）被平均掩盖；
4. **perf 无 `-g`**：拿不到调用栈，只能看函数名看不到调用链；
5. **100Hz 采样**：与系统定时器谐振，结果周期性偏差；
6. **strip 后采样式**：只有地址没有符号，无法归因；
7. **插桩覆盖不全就当结论**：没插桩的代码可能是真正热点；
8. **优化后不复测**：没有"优化前后对比"就无法证明优化有效。

## 9. 常见问题 FAQ

**Q1：插桩和 perf 能互相替代吗？**
不能。插桩精确但只覆盖已插桩代码；perf 无侵入但近似。推荐"插桩粗定位 → perf 细验证/查漏 → 优化 → 再对比"。

**Q2：perf 采样需要 root 吗？**
`kernel.perf_event_paranoid` 级别 2 时只能分析自己进程的用户态；系统级指标（`-a`）或他人进程需要更高权限。

**Q3：为什么看不到函数符号？**
二进制被 strip，或未带 `-g`；安装 debuginfo 包或重新编译。

**Q4：IPC 低一定代表缓存问题吗？**
不一定。IPC 低说明存在等待，可能是缓存缺失、分支预测失败、访存瓶颈、锁等待或内存带宽受限；需结合 `perf stat` 的事件明细与火焰图判断。

**Q5：火焰图里宽条是"慢"还是"调用多"？**
self time 宽 = 函数本身慢；要看调用链确认是自身开销还是被高频调用。用 `perf report` 的 self/total 双排序区分。

**Q6：插桩会影响性能测量吗？**
会。打点本身有固定开销，可能影响编译器优化/内联；对微秒级代码段影响显著，需用对比实验剔除固定开销。

**Q7：本机没有 Linux 环境怎么验证？**
插桩方法（`steady_clock`/RAII）可在 Windows/MSVC 上用 `QueryPerformanceCounter` 等价验证；perf 为 Linux 专属，需在 Linux 环境执行（容器或云主机），本文命令在目标机器复现后才是证据。

## 10. 验证与基准建议

性能工具本身要能被验证——否则"测出的数据"无法作为容量、预算或优化结论的证据。

### 10.1 最小验证样例

```bash
# 1) 验证 perf 可用性与权限
perf --version
cat /proc/sys/kernel/perf_event_paranoid

# 2) 验证采样通路：对一个空转进程采样 5 秒
perf record -F 99 -g -p $$ -- sleep 5
perf report --stdio | head -20

# 3) 验证插桩通路：编译一个带 PROFILE 宏的最小程序并观察输出
g++ -O2 -g -DPROFILE -o probe probe.cpp && ./probe
```

任何"性能结论"都必须能回指到上述命令的原始输出，否则只是观点不是证据。

### 10.2 测试矩阵（插桩与 perf 的验收口径）

| 场景 | 插桩验证 | perf 验证 | 通过标准 |
| --- | --- | --- | --- |
| 单 Tick 耗时 | 每秒汇总输出 | `perf stat` 整体 CPU | 插桩与采样趋势一致，无矛盾 |
| 热点归因 | 分层占比 ≈ 100% | 火焰图宽条一致 | 两层结论指向同一函数族 |
| 优化前后对比 | 同场景前后各跑一次 | 前后火焰图 + IPC | 目标指标改善且无新热点 |
| 锁竞争 | `context-switches` 基线 | `perf stat` 前后对比 | 切换数下降或解释清楚 |
| 缓存问题 | 插桩无法直接测 | `cache-misses` 事件 | 命中率低且与热点相关 |

### 10.3 证据纪律

- 记录二进制版本、编译选项（`-O2 -g`）、内核版本与 `perf --version`；
- 记录采样频率、时长、进程/线程范围；
- 原始输出（perf.data、火焰图 SVG、插桩汇总）存档，供复盘与容量模型复用；
- 与 [游戏测试与质量/04-性能兼容与网络异常测试](../../游戏测试与质量/04-性能兼容与网络异常测试.md) 的"指标与采样纪律"口径一致。

## 11. 典型故障案例

### 11.1 案例 A：平均正常但玩家间歇卡顿

**症状**：单 Tick 平均 2ms，但 P99 达 40ms；玩家反馈间歇卡顿。

**诊断**：插桩分层统计发现"路径重建"阶段偶发 30ms+（每 200 Tick 一次）；perf 火焰图对应位置显示大量临时对象分配。

**根因**：路径重建每次分配新容器，触发频繁 malloc/free 与 page fault。

**解决**：对象池复用 + 预留容量；P99 从 40ms 降至 5ms，平均不变但尾部体验修复。

**验证**：优化前后同压测场景各采样一次，`page-faults` 下降、火焰图分配宽条消失。

### 11.2 案例 B：perf 采不到调用栈

**症状**：`perf record -g` 后 `perf report` 只见函数名不见调用链。

**诊断**：二进制为 `-O2` 默认无 frame pointer；`perf report` 提示 "no symbols" 或调用链断裂。

**根因**：x86-64 默认用 RBP 做通用寄存器（`-fomit-frame-pointer` 隐含），采样无法回溯栈。

**解决**：重新编译加 `-fno-omit-frame-pointer`，或 `perf record --call-graph dwarf`；生产二进制建议保留 frame pointer 或 dwarf 调试信息。

**验证**：重采后调用链完整，热点归因到具体调用方。

### 11.3 案例 C：插桩结论与采样矛盾

**症状**：插桩显示 A* 扩展占 70%，perf 火焰图却是内存分配占大头。

**诊断**：插桩只覆盖了 A* 函数内部，未覆盖其调用的分配路径（operator new、容器扩容）；perf 看到的是完整调用栈。

**根因**：插桩覆盖不全——"测不到的不等于不存在"。

**解决**：以 perf 全栈结果为准定位真热点，插桩用于验证优化后的阶段耗时；把分配路径纳入插桩统计。

**验证**：插桩扩展范围后两层结论一致。

## 12. 关联阅读

- [01-Socket-Epoll与Reactor](01-Socket-Epoll与Reactor.md)：IO 多路复用与 Reactor 的底层基础（性能工具的服务端背景）。
- [游戏服务端/01-架构与网络/05-并发与高性能](../../游戏服务端/01-架构与网络/05-并发与高性能.md)：多线程分片、每线程缓冲、热路径 relaxed 原子（插桩统计的并发背景）。
- [游戏服务端/06-世界模拟与运行时/11-AI与寻路时间预算](../../游戏服务端/06-世界模拟与运行时/11-AI与寻路时间预算.md)：Tick 预算与分帧更新的落地（插桩数据的消费方）。
- [游戏AI/02-移动学习与服务端/04-服务端AI与性能](../../游戏AI/02-移动学习与服务端/04-服务端AI与性能.md)：服务端 AI 预算与降频（AI 线程耗时定位的配套）。
- [游戏测试与质量/07-UE DS机器人压测与容量评估](<../../游戏测试与质量/07-UE DS机器人压测与容量评估.md>)：容量评估与 CCU 拐点（耗时数据的压测侧）。
- [游戏知识/07-UI与性能优化/03-性能分析工具与Profiling](../../游戏知识/07-UI与性能优化/03-性能分析工具与Profiling.md)：UE 客户端专用工具链（stat/Insights/ProfileGPU），与本文的通用 C++/Linux 方法互补。
- [游戏知识/07-UI与性能优化/05-GameplayDebugger与运行时调试](../../游戏知识/07-UI与性能优化/05-GameplayDebugger与运行时调试.md)：UE 运行时调试（服务端进程采样之外的调试路径）。
- 速查笔记（个人速查、正式知识以本文件为准）：[笔记/插桩测试](../../笔记/插桩测试.md)、[笔记/perf性能分析](../../笔记/perf性能分析.md)。

## 13. 更新日志

| 日期 | 版本 | 更新内容 |
| --- | --- | --- |
| 2026-08-14 | v1.0 | 由 笔记/插桩测试.md 与 笔记/perf性能分析.md 收敛为 canonical（R4-OBS-01）；补齐术语速查、落地检查清单、常见反模式、FAQ 与跨域互链。 |
