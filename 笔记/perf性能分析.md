# perf 性能分析（采样 Profiling）—— 耗时点测试方法二

## 1. 是什么
- Linux 内核自带的性能分析工具，基于硬件性能计数器 + 周期性采样。
- 以固定频率打断 CPU，记录当前执行函数及调用栈，统计"时间都花在哪"。
- 无侵入：不需要改代码，可直接挂载运行中的服务器进程。

## 2. 常用命令
| 命令 | 作用 |
| --- | --- |
| `perf top` | 实时查看当前热点函数（类似 top） |
| `perf stat` | 统计整体性能事件（CPU、cache miss、分支预测、IPC 等） |
| `perf record` | 采样录制，生成 perf.data |
| `perf report` | 分析 perf.data，查看热点与调用链 |
| `perf annotate` | 看热点函数内指令级热区（汇编/源码级） |
| `perf script` | 导出原始采样数据（用于生成火焰图） |
| `perf sched` | 调度/等待分析 |

## 3. 典型流程（挂载服务器进程）
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
- 若二进制没有 frame pointer（如 `-O2` 默认），`-g` 可能采不到调用栈，改用 `--call-graph dwarf`。

## 4. 火焰图怎么看
- 横轴：采样占比（条越宽越热）；纵轴：调用栈（下层是调用者）。
- 找"宽条"：某个函数自身采样最多 = 自耗时热点（self time）。
- 看调用链：宽条是被谁调用起来的，判断是"函数本身慢"还是"被调用次数太多"。
- 优化前后各生成一张，对比宽条变化验证优化效果。

## 5. 关键指标（perf stat）
- `cycles` / `instructions` → **IPC**（每周期指令数）：低说明存在等待（缓存缺失、分支预测失败、访存瓶颈）。
- `cache-misses` / `cache-references` → 缓存命中率：A* 节点对象分散时往往命中率低，是隐藏杀手。
- `branch-misses` → 分支预测失败率：热循环里的复杂分支会显著拖慢。
- `context-switches` → 切换频繁说明锁竞争 / 调度问题。
- `page-faults` → 缺页多说明内存访问模式差或分配频繁。

## 6. 常见问题
- **权限**：`kernel.perf_event_paranoid` 限制采样能力，级别 2 时只能分析自己进程的用户态；必要时 `sudo sysctl kernel.perf_event_paranoid=1` 或安装 debuginfo。
- **符号不显示**：需要 `-g` 调试信息编译（`-O2 -g`），或安装对应 debuginfo 包；strip 过的二进制只能看到地址。
- **采样频率**：99Hz 是常用经验值（与系统定时器 100Hz 错开）；频率越高越准但开销越大。
- **采样误差**：采样是近似统计，短函数/低频函数可能被低估，需结合插桩验证。
- **多线程**：默认跟踪目标进程的所有线程；加 `-t <tid>` 只跟单线程，`-a` 全系统采样。

## 7. 服务器游戏进程分析要点
- 先确认热点在哪个线程（AI 线程 / 逻辑线程 / 网络线程）。
- 区分"每 Tick 固定开销"与"偶发尖峰"：尖峰往往来自 GC、全量遍历、临时分配。
- 控制变量对比：同一压测场景，优化前后各采样一次，比较火焰图与 IPC。
- 与插桩配合：**perf 回答"时间花在哪"，插桩回答"每个逻辑阶段具体多少"**（详见 `插桩测试.md`）。