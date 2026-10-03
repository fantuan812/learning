---
type: Tutorial
title: "03-性能工具：插桩与perf采样"
status: stable
verified: []
maturity: L2
updated: 2026-10-03
---
# 03-性能工具：插桩与perf采样

> 知识成熟度：L2。本文维护方法、命令和判读合同；本轮没有执行 Linux perf 采样；第 10.1 节的普通运行仅核验编译、目录隔离与退出状态，不作为新增性能结论。既有[插桩实验](../../../evidence/labs/profiling/README.md)仅证明其记录的环境与用例，不代表 Linux perf 或线上结果，不能据此自动提升成熟度。
> 适用范围：Linux/C++ 游戏服务端的 CPU 热点、逻辑阶段耗时与观测开销；UE 客户端工具见[性能分析工具与 Profiling](03-性能分析工具与Profiling.md)。
> 知识基线：命令选项按 Linux v6.12 的 perf 官方文档核对；目标机必须记录 `uname -r`、`perf --version`、CPU/PMU、编译器与事件支持情况。本页不声称命令已在所有 5.x/6.x 内核验证。来源核对日期：2026-10-03，链接与适用边界见第 12 节。
> 最后更新：2026-10-03（收敛旧笔记并修正测量、归因与权限边界）。

## 1. 概述

先定义问题：要知道的是**代码段的经过时间、CPU 上的工作分布、调用次数，还是排队/锁/IO 等待**？这些数值不是同一个分母。

- **插桩（Instrumentation）**：手动、编译期或动态加入计时/计数逻辑，可记录 Tick、请求、分支、失败原因等业务维度。无需依靠随机命中某段代码，但仍有时钟分辨率、读时钟成本、调度与测量扰动
- **perf 计数/采样**：利用 Linux perf_events 支持的硬件、软件或 tracepoint 事件。常规 CPU 采样可不修改源码，但中断、栈采集、缓冲及输出都有开销；“不改源码”不等于“零侵入/零成本”

推荐从粗粒度阶段计时与请求计数开始，用采样查热点及未覆盖调用，再提出假设、改变一个因素、重复验证。没有线索时也可先采样；不必固定为“插桩先、perf 后”。

先修：[C++ 对象生命周期与 RAII](../../01-编程与计算机基础/C%2B%2B语言与对象模型/01-C%2B%2B对象生命周期与RAII.md)、[并发与高性能](../../../游戏服务端/01-架构与网络/05-并发与高性能.md)。完整实践接[性能问题定位完整链路](10-性能问题定位完整链路.md)。

## 2. 核心概念

| 术语 | 本文口径 | 不能直接推出 |
| --- | --- | --- |
| elapsed / wall time | 开始到结束的经过时间，可包含被抢占与阻塞 | 全部时间都在消耗 CPU |
| CPU 采样 | 对选定事件收集执行位置，按配置可带调用链 | 每次函数调用耗时、准确调用次数或全部等待时间 |
| Self overhead | 归到函数自身采样位置的事件占比 | 单次调用很慢 |
| Children / inclusive | 把子调用样本向父调用累计后的占比 | 各层相加仍为 100% |
| IPC | `instructions / cycles`，范围与事件口径须一致 | 低 IPC 唯一由缓存或锁导致 |
| 火焰图 | 聚合相同调用栈的事件权重可视化 | 横轴是时间顺序 |
| 单调时钟 | 例如 `steady_clock`，适合计算时间差 | 纳秒单位等于纳秒精度或零测量误差 |

### 2.1 两类方法的定位

| 维度 | 插桩 | 常规 perf CPU 采样 |
| --- | --- | --- |
| 程序改动 | 手动/编译期会改变执行代码；动态方式可不改源码 | 一般不需修改源码；符号和栈质量可能要求不同构建 |
| 信息 | 选定区间耗时、次数、业务标签 | 所选事件的执行位置和可选调用链；其他事件有其他语义 |
| 误差与扰动 | 读时钟、聚合、日志、优化变化、调度 | 样本有限、丢失/限频、回溯失败、事件归因偏差 |
| 覆盖 | 仅埋点覆盖范围 | 限于 PID/TID/CPU、权限、事件及采集窗口 |
| 生产使用 | 有界缓冲、降采样/开关、开销预算 | 最小采集范围、许可、时长及开销预算 |

## 3. 插桩测试

### 3.1 手动打点

下面是放入函数内的计时片段；需要 `<chrono>`，`elapsed` 再交给有界统计器，避免每次打印：

```cpp
const auto t0 = std::chrono::steady_clock::now();
// 被测代码段；明确是否包含子调用、排队或锁等待
const auto t1 = std::chrono::steady_clock::now();
const auto elapsed = t1 - t0;
// 汇总时再转换单位；不要逐次截断为整数微秒后累加
```

[C++ WG21 N4981](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/n4981.pdf) 的 `[time.clock.steady]` 给出不回退的时钟合同，并不承诺特定硬件的精度。短区间应先测空打点开销，并用批量重复、结果校验和多轮分布评估；空测量差值也不能被当成恒定误差直接从每条长尾样本扣掉。

统计至少包括调用次数、累计、平均、最大和分位数，注明总体及窗口。计时包住的同步子调用通常已计入 inclusive 耗时；异步任务在提交函数返回后继续执行，需要另测完成区间。

旧笔记提到的 Windows `QueryPerformanceCounter` 与 x86 `rdtsc` / `rdtscp` 保留为平台相关线索：它们不是这里的可移植计时合同；尤其不能把某个 `cpuid/lfence` 组合当作跨 CPU 通用配方。本页不提供或声称验证这些平台专用实现。

### 3.2 RAII / 宏封装

RAII 计时器可在正常离开作用域或异常展开时归还记录，减少漏写终点；析构路径应不抛异常，不在热路径做字符串分配或阻塞 IO。宏/构建开关可按需关闭详细探针，线上也可保留经过预算验证的低成本计数。

```cpp
// 示例接口；ScopedTimer 需由项目实现，不是独立可编译程序
#ifdef PROFILE
#define SCOPED_TIMER(name) ScopedTimer _t_##name(#name)
#else
#define SCOPED_TIMER(name)
#endif
```

同一作用域重复使用相同 name 会重名；实际封装要采用唯一局部名或直接命名 RAII 对象。开关两种构建都要测试，不能只验证启用版本。

### 3.3 编译器与动态插桩

- GCC `-finstrument-functions` 在函数入口/出口插入 `__cyg_profile_func_enter/exit` 调用，内联函数也可能生成相应探针；回调及其不安全调用链需排除插桩，避免递归。适用选项和排除机制见[GCC 官方文档](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)
- `-pg` 为 gprof 生成统计代码，编译与链接阶段都需匹配选项；不是透明的线上无开销采样
- Clang 的兼容性需按目标版本另行核对；Intel Pin、DynamoRIO、商业 APM 等动态插桩可作为不改源码的备选，但仍改变运行行为。本页未核对其当前部署/许可，不给出性能保证

### 3.4 轻量设计要点

- 线程局部累加避免每次更新争用共享计数器；汇总线程不能无同步读取另一个线程仍在写的普通内存。可采用线程退出后合并、双缓冲移交等有明确同步的设计
- 热路径只更新预注册数值 ID 和计数，周期性输出汇总；“每秒输出”是起始配置，仍须按数据量和时效需求评估
- 环形缓冲/直方图有固定容量和溢出策略；记录丢弃数。只保留最近 N 条时，不要把窗口分位数称为全程分位数
- 测量关闭、只计数、计时、带标签/输出几个档位；维护开关与开销预算，避免探针成为新的热点

## 4. perf 采样

### 4.1 常用命令

| 命令 | 用途与边界 |
| --- | --- |
| `perf list` | 查询本机可用事件，不能把其他 CPU 的事件名直接照搬 |
| `perf top` | 实时采样热点，仍应限定合法采集范围 |
| `perf stat` | 聚合计数，适合前后重复比较，不等于调用栈采样 |
| `perf record` / `perf report` | 记录数据 / 展示事件分布及已采集调用链 |
| `perf annotate` | 在符号/源码可匹配时定位指令或源码附近的样本 |
| `perf script` | 导出采样记录，供离线分析或火焰图工具处理 |
| `perf sched` | 分析已记录的调度事件；普通 CPU 火焰图不能替代等待分析 |

### 4.2 典型流程（挂载服务器进程）

以下仅为命令模板，需替换已核实且获准分析的 PID，并在独立输出目录运行。采样会写数据；不得覆盖历史实验结果：

```bash
# 只读确认版本、权限和候选进程；核实 PID 对应的实例
uname -r
perf --version
cat /proc/sys/kernel/perf_event_paranoid
pgrep -af server_binary
perf list

# 用户态 cycles 示例，假设目标保留了可用的 frame pointer
perf record -o ai.perf.data -e cycles:u -F 99 --call-graph fp -p <PID> -- sleep 60

# 两种归因视图；同一事件下区分自身样本和含子调用的累计
perf report -i ai.perf.data --stdio --no-children --sort comm,dso,symbol
perf report -i ai.perf.data --stdio --children --sort comm,dso,symbol
perf annotate -i ai.perf.data --stdio

# 在已安装并核对版本的 FlameGraph 工具目录中处理
perf script -i ai.perf.data > out.perf
./stackcollapse-perf.pl out.perf > out.folded
./flamegraph.pl out.folded > flame.svg
```

`cycles:u` 限定用户态，无法据此解释内核 CPU 开销；事件不支持或权限拒绝时保留报错，不自动换成全系统采样。`99` 是可试的频率，既不保证避开业务周期，也不保证实际每秒恰好取得 99 个有效样本；应检查采集告警、样本量、频率敏感性和额外负载。选项按[perf record 官方 v6.12 文档](https://raw.githubusercontent.com/torvalds/linux/v6.12/tools/perf/Documentation/perf-record.txt)核对。

### 4.3 调用链与火焰图怎么看

**先确认用户态回溯方式，再解释图。** `--call-graph` 隐含开启调用链；`fp` 依赖可回溯的 frame-pointer 链，`dwarf` 依赖 perf 构建支持与匹配的展开信息，并采集有限大小的用户栈。例如可单独尝试 `--call-graph dwarf,8192`，同时比较文件体积、开销及栈截断；`lbr` 受硬件/平台和模式限制，不能通用替代。内核栈回溯还受内核配置影响。[perf record](https://raw.githubusercontent.com/torvalds/linux/v6.12/tools/perf/Documentation/perf-record.txt)

- GCC 的 `-fno-omit-frame-pointer` 有助于 fp 路线，但并非保证每个函数都有 frame pointer；目标、叶函数及优化有例外。`-g` 提供调试信息，不等于恢复 frame pointer。[GCC 优化选项](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)
- 火焰图通常是同调用栈聚合的包含式宽度，下层为调用者；宽条可以来自自身工作，也可以来自子调用。以 `perf report` 的 Self / Children 分开检查，不把父层宽度直接叫 self time
- 宽度表示选定事件的样本/权重，CPU cycles 图不是每次调用耗时图，也不是调用次数图。横向相邻不代表执行时间顺序
- 前后图要有相同事件、负载、时长和过滤口径，同时看绝对耗时、吞吐与调用次数；比例下降也可能仅因另一个函数变慢。Self / Children 语义见[perf report 官方文档](https://raw.githubusercontent.com/torvalds/linux/v6.12/tools/perf/Documentation/perf-report.txt)，火焰图转换工具见[作者仓库](https://github.com/brendangregg/FlameGraph)

### 4.4 关键指标（perf stat）

下表给出诊断线索，不能靠一个计数器判定根因。先用 `perf list` 核对事件和 PMU；硬件、虚拟化及内核不同可能缺少事件。

| 指标 | 计算/口径 | 判读限制 |
| --- | --- | --- |
| IPC | `instructions / cycles` | 与指令混合、依赖链、分支及访存有关；低值本身不能证明缓存问题或锁等待 |
| 缓存未命中比 | 匹配语义的 `cache-misses / cache-references` | 这是未命中比；只有分子分母确实匹配时，1 减该值才可解释为相应事件的命中比，不是整个缓存层级总命中率 |
| 分支未命中比 | `branch-misses / branches` | 次数不是比率，须看工作量及分母 |
| `context-switches` | 上下文切换次数 | 睡眠、IO、抢占等都可能贡献，不能单独证明锁竞争 |
| `page-faults` | 缺页事件数 | 需区分场景及 minor/major 等事件；不必然代表低效分配或磁盘 IO |

同一窗口可从少量事件开始，避免一开始请求过多计数器；记录计数有效运行比例、缩放、未支持/未计数及错误信息。对可重复启动的负载可使用 `perf stat -r N` 的重复统计；不要把多轮均值当作长尾分布。[perf stat 官方文档](https://raw.githubusercontent.com/torvalds/linux/v6.12/tools/perf/Documentation/perf-stat.txt)

### 4.5 权限、符号与采集边界

- **权限**：`perf_event_paranoid` 只是其中一层限制。上游文档的 `>=2` 限制普通用户的进程级内核采集，不能推出所有发行版、容器或安全策略都允许本进程用户态采样。被拒绝时记录范围/事件/错误，由管理员按组织政策评估最小必要权限；不要默认下调全局安全值或授予宽泛权限。[Linux perf 安全文档](https://docs.kernel.org/admin-guide/perf-security.html)
- **符号与调用栈是两件事**：保留匹配二进制、build ID、共享库和独立调试文件；strip 后并不必然只剩地址，但缺失匹配符号/调试信息会影响归因与源码行。先分别检查地址解析与栈回溯
- **线程范围**：`-p` 选择进程，`-t` 选择 TID，`-a` 扩大到系统范围；还要核对继承、线程产生/退出和实际采样 TID，不要只看总图猜“AI 线程”
- **统计盲区**：短函数、低频任务可能样本不足；被阻塞的时间不会自动成为 CPU cycles 热点。需要等待分析时另选择调度/应用事件，并再次核实权限
- **数据处理**：调用栈、地址、路径和 DWARF 栈数据可能包含敏感内容；限制输出访问与保留时间，分享前审核。没有样本、符号缺失、截断或丢样都应写入结论限制

## 5. 服务器场景的统计模式

### 5.1 按 Tick 统计

为每个 Tick 记录总墙钟、寻路调用数、阶段耗时、超预算标志和队列等待；另记录无请求 Tick，避免只统计“有工作”的窗口。

- 整体寻路 → Open List 操作 → 邻居扩展 → 路径重建可以形成层级，但 inclusive 区间会重叠。只有同一线程、同一时间窗口内互斥且覆盖完整的阶段才可相加近似 100%；异步并行和嵌套阶段不能这样加
- 同样长的采集窗口不一定有相同请求量；同时报告总量、每请求指标、P50/P95/P99、最大值和样本数
- 分配、GC、全量遍历等是尖峰候选原因，不是看到尖峰即可确定的根因；以关联事件、代码与对照实验确认

### 5.2 游戏进程分析要点

按 AI、逻辑、网络等线程及固定/突发工作分别看；结合 Tick/请求 ID 对齐同一窗口。A* 的入队、出队、邻居扩展、启发计算和重建应分开计数，开放队列峰值、失败率与请求次数帮助解释“每次慢”还是“次数多”。算法指标见[A* 数值与实验合同](../../02-数学与游戏算法/路径搜索与导航/02-A星算法与优化.md#123-数值比较与性能实验合同)。

观测建议：插桩回答“这个逻辑区间经过多久/执行几次”，所选 perf 事件回答“这些样本对应哪些执行位置”；两者有条件地互相校验，不强求不同分母的占比一致。

### 5.3 与压测/容量评估的衔接

实测阶段耗时与负载曲线可供[DS 机器人压测与容量评估](<../测试策略与自动化/07-UE%20DS机器人压测与容量评估.md>)、[服务端 AI 预算](../../../游戏AI/02-移动学习与服务端/04-服务端AI与性能.md)和[AI 与寻路时间预算](../../../游戏服务端/06-世界模拟与运行时/11-AI与寻路时间预算.md)使用。容量拐点还依赖线程并行、排队、网络、内存和可接受延迟，不可仅把单次耗时乘玩家数就当容量结论。

## 6. 术语速查

- Profiling 是性能剖析的总称，包含采样与插桩等方法
- On-CPU 指 CPU 执行期间；Off-CPU 指未执行的等待区间，须说明来源和统计定义
- Self 与 inclusive / Children 依赖事件归因方式；不等同于准确逐次计时
- `perf_event_paranoid` 是权限策略参数，不是遇到失败就降低的“性能开关”

## 7. 落地检查清单

- [ ] 明确观测问题、事件、PID/TID、时间窗口和分母
- [ ] 计时用合适的单调时钟，写明单位、聚合、嵌套和异步边界
- [ ] 热路径探针有开关/开销预算；缓冲有上限，汇总有同步，丢弃数可见
- [ ] 保存二进制与匹配符号，明确 fp/dwarf 等回溯模式并检查完整性
- [ ] 核实权限，权限不足时保留失败记录，不擅改全局安全配置
- [ ] 检查样本不足、丢失、限频、计数缩放及不支持事件
- [ ] 同场景重复比较，记录绝对延迟、吞吐、调用量与 P95/P99，不仅看图宽
- [ ] 原始数据与编译/系统配置对应，记录未执行边界及分享限制

## 8. 常见反模式

1. 把“不改源码”写成零开销，把“无采样误差”写成无测量误差
2. 每次计时截成整数微秒再累加，或在探针里拼字符串、同步写日志
3. 把 nested inclusive 阶段相加、把 CPU cycles 占比当墙钟占比
4. 认为 `-g` 必然有完整调用栈，或把符号丢失与回溯断裂当同一问题
5. 把 99Hz 当准确性保证，或把 100Hz 直接判为必有偏差
6. 仅凭低 IPC、切换数或 page fault 就宣布某个根因
7. 前后各跑一次，看归一化图宽变化就宣布性能提升
8. 未执行的教学场景写成真实线上改善结果；优化后只看均值，不核对长尾和正确性

## 9. 常见问题 FAQ

**Q1：插桩和 perf 能互相替代吗？** 不能。需要业务次数、失败原因和区间时长时用相应插桩；需要执行位置/事件分布时选采样。先统一窗口、范围及统计口径，再解释差异。

**Q2：一定要 root 吗？** 不一定，也不能保证普通用户可用。实际取决于内核、发行版、容器、策略与事件；由管理员评估最小权限，见第 4.5 节。

**Q3：`-O2 -g` 是否足够？** 不保证。`-g`、frame pointer、展开信息、build ID 与匹配共享库负责不同部分；先确定回溯模式，再做目标函数链的验证。

**Q4：低 IPC 就是缓存/锁问题吗？** 不是。IPC 只是特定范围的指令/周期比；阻塞等待还可能未被 cycles 计入，应结合工作量、事件和调度信息。

**Q5：宽条说明单次函数很慢吗？** 不说明。它可能反映高频调用或子调用工作；用自身/包含式归因配合调用计数。

**Q6：如何量化探针影响？** 对同负载比较关闭探针与不同观测档位的延迟、吞吐及资源消耗；短代码段采用批量重复。开销不保证恒定，也不能无条件扣掉。

**Q7：没有 Linux 怎么办？** 可在其他平台验证 C++ 插桩合同；Linux perf 的事件、权限和调用链必须在受支持且有权限的 Linux 环境验证。容器/云主机不保证暴露所需 PMU 或权限。

## 10. 验证与基准建议

### 10.1 可复现练习入口与未执行边界

已有[插桩开销与卡顿预算实验](../../../evidence/labs/profiling/README.md)保存 Windows/MinGW 的历史原始结果：7 条机器相关的插桩比较与 10 条卡顿/预算功能断言；它们不能合称 17 条可移植功能断言。历史文件保持不变，明确未用 Linux perf。若要在 Linux 复现，从仓库根解析源文件绝对路径，再把编译产物、工作目录和日志都隔离到临时目录；原程序会访问相对路径 `build/profiling_overhead_tmp.log`，因此必须先在临时目录创建 `build`：

```bash
source_file="$(pwd -P)/evidence/labs/profiling/src/profiling_overhead.cpp"
out=$(mktemp -d)
printf 'output=%s\n' "$out"
mkdir -p "$out/build"
(
  cd "$out" || exit 1
  g++ -std=c++17 -O2 -g -fno-omit-frame-pointer \
    "$source_file" -o profiling_overhead || exit 1
  ./profiling_overhead > instrumentation.txt
  run_rc=$?
  cat instrumentation.txt
  test "$run_rc" -eq 0 || exit "$run_rc"
  # 功能检查通过后，才另测采样通路；观察比例不充当功能门禁
  perf record -o profiling.perf.data -e cycles:u -F 99 \
    --call-graph fp -- ./profiling_overhead || exit 1
  perf report -i profiling.perf.data --stdio --no-children
)
```

**修复前普通运行的实际边界（2026-10-03）**：原版源码在 Linux x86_64、g++ 14.2.0 下编译成功（退出 0）；从新建临时目录执行，日志样式未跳过、P6 通过，仓库根未生成 `build`。普通运行整体退出 **1**，结果为 **pass=6、fail=1**：P3 要求“格式化耗时 > 作用域计时的 5 倍”，本机该次观测约 3.6 倍，因此失败。[旧失败原始输出](../../../evidence/labs/profiling/results/profiling_overhead_legacy_linux_2026-10-03.stdout.txt)及[命令/源码哈希](../../../evidence/labs/profiling/results/profiling_overhead_legacy_linux_2026-10-03.provenance.json)保留这次真实失败；不能反写为通过，也不以该比值建立普适性能结论。

**功能与性能观察分离后的验证**：实验修复保留 P1–P7 全部原比较阈值，改为本机 `OBSERVATION`；另立计数、格式化、文件 I/O、测量有效性的 13 条功能检查，只有真实功能失败决定非零退出。[新记录](../../../evidence/labs/profiling/README.md#2026-10-03-新功能契约记录)正常运行功能为 **13/0、退出 0**，而 P3 仍为 **3.605170x、UNSUPPORTED**。缺少 `build` 的负例真实退出 1；确定性边界回归 27 项通过。两次构建标志不同且新增功能检测成本，不能当成单变量性能 A/B；新旧原始输出分开保存，没有放宽阈值求通过。

**perf 两条命令未执行**，模板遇功能失败会停止后续步骤。该模板明确启动真实负载，避免把附加到等待中的 shell `$$` 说成“采样空转进程”。程序过短、事件不可用或样本不足均不能通过采样验收；需在获准的负载上延长代表性工作窗口，保留原始错误。本实验的插桩微基准也不是服务端吞吐或 perf 开销基准。

### 10.2 测试矩阵与通过条件

| 练习 | 要做的对照 | 判定与限制 |
| --- | --- | --- |
| 插桩开销 | 关闭 / 计数 / 计时 / 输出，多轮同负载 | 检查结果正确、记录开销分布；不预设某档必快多少 |
| 测量口径 | 同步父子区间、互斥阶段、包含等待区间 | 说明重叠、等待与 CPU 分母；不强求和为 100% |
| 调用栈质量 | 固定构建/负载，分别选 fp 与可用 dwarf | 检查已知调用链、未知符号和截断；报告所付开销，不以有图即通过 |
| 频率敏感性 | 相同事件下尝试两三个许可频率并重复 | 样本量、热点排序、延迟及告警可解释；99Hz 不作基准答案 |
| 优化有效性 | 原版/改版，同输入、调用量和运行环境 | 正确性不退化，目标绝对指标改善超过观察波动，记录长尾及副作用 |
| 锁/缓存假设 | 事件线索结合代码与一次针对性改变 | 无法支持归因就保留为假设，切换数/未命中数下降本身不等于根因被证明 |

### 10.3 证据纪律

保存原始命令、退出码、日期、工具链/内核/CPU、二进制或 build ID、事件、回溯方式、频率/时长、线程范围、输入/预热、原始数据和告警；分开“采集成功”“符号可用”“因果解释成立”。性能报告要写清单位、分母、样本数、重复波动、未验证环境。数据留存与隐私要求见第 4.5 节；指标口径对齐[性能兼容与网络异常测试](../测试策略与自动化/04-性能兼容与网络异常测试.md)。

## 11. 典型故障案例（教学场景，非实测报告）

### 11.1 案例 A：平均正常但玩家间歇卡顿

假设均值正常而尾延迟异常：按 Tick 分层并关联分配、队列和系统事件，检查热点是否只在尖峰窗口出现。若怀疑路径重建的临时容器，比较预留容量/复用与原实现，同时验证内存峰值及输出一致。只有拿到可复核前后记录，才写改善数值；旧版的“P99 40ms 降至 5ms”等示意数字没有原始来源，不能用作结果。

### 11.2 案例 B：perf 采不到调用栈

先分开“无符号”和“链断裂”：检查匹配二进制/共享库、实际回溯模式、构建选项和展开数据，再检查样本量与截断。fp 不适合当前构建时可评估支持的 dwarf；不把 `-O2` 或缺少 `-g` 单独当作所有故障的根因。验收需看到预期调用链，并记录仍缺失的帧。

### 11.3 案例 C：插桩结论与采样看似矛盾

假设 A* 阶段占比大，而火焰图分配条很宽：先看分配是否是 A* 的同步子调用。若是，两者可能完全一致；包住整个函数的墙钟计时已包含子调用，并不会因未单独给 `new` 打点就漏掉这段经过时间。再核对线程、窗口、CPU/墙钟分母、异步边界和探针开销。必要时细分分配计数与 self/inclusive，不能一律宣布“perf 更准”。

## 12. 来源与关联阅读

来源核对日期：2026-10-03。上游文档支撑工具合同，不能代替目标机执行；v6.12 选项需与安装版本匹配。

- [Linux perf record v6.12](https://raw.githubusercontent.com/torvalds/linux/v6.12/tools/perf/Documentation/perf-record.txt)：事件、频率、采集范围和回溯模式
- [Linux perf report v6.12](https://raw.githubusercontent.com/torvalds/linux/v6.12/tools/perf/Documentation/perf-report.txt)、[perf stat v6.12](https://raw.githubusercontent.com/torvalds/linux/v6.12/tools/perf/Documentation/perf-stat.txt)、[perf list v6.12](https://raw.githubusercontent.com/torvalds/linux/v6.12/tools/perf/Documentation/perf-list.txt)：归因、计数与事件选择
- [Linux perf 安全](https://docs.kernel.org/admin-guide/perf-security.html)：权限与数据泄露风险
- [C++ WG21 N4981](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2024/n4981.pdf)：`[time.clock.steady]`、`[time.duration.cast]`；时钟/转换合同不等于平台精度保证
- [GCC 插桩选项](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)、[优化选项](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)：编译器行为，其他版本/编译器需复核
- [FlameGraph 作者仓库](https://github.com/brendangregg/FlameGraph)：转换工具；不视作 Linux perf 本身
- [Socket/Epoll 与 Reactor](../../01-编程与计算机基础/操作系统与系统I-O/01-Socket-Epoll与Reactor.md)、[并发与高性能](../../../游戏服务端/01-架构与网络/05-并发与高性能.md)：线程、IO 与同步先修
- [性能问题定位完整链路](10-性能问题定位完整链路.md)、[插桩实验与原始结果](../../../evidence/labs/profiling/README.md)：方法落地与证据边界
- [GameplayDebugger 与运行时调试](05-GameplayDebugger与运行时调试.md)：UE 运行时调试的互补路径
- 旧入口：[插桩测试](插桩测试.md)、[perf 性能分析](perf性能分析.md)，仅保留主题/章节导航

## 13. 更新日志

| 日期 | 版本 | 更新内容 |
| --- | --- | --- |
| 2026-08-14 | v1.0 | 从两篇短笔记收敛为主文，补术语、检查清单和跨域链接 |
| 2026-10-03 | v1.1 | 短笔记改为引用入口；纠正精度、开销、IPC、火焰图、权限、回溯和样例结果表述；补隔离练习、保留旧失败并关联新功能/观察分离证据；L2 / verified[] 不变 |
