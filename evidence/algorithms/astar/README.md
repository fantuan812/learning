---
type: Evidence
title: "Evidence · astar：A* 与路径缓存的正确性及测量合同"
status: stable
verified: []
maturity: L0
updated: 2026-10-04
sources:
  - resource: https://eel.is/c%2B%2Bdraft/time.clock.steady
    title: C++ steady_clock contract
  - resource: https://eel.is/c%2B%2Bdraft/list.ops
    title: C++ list splice contract
  - resource: https://www.redblobgames.com/pathfinding/a-star/implementation.html
    title: Red Blob Games A* implementation notes
---
# A* 与路径缓存：先证明算对，再测实际返回的工作

> **撤回旧结论**：旧版“缓存命中 0.001ms、约 33 倍提升”不是实测。旧源码在命中分支直接写入常数，没有计时、取回/复制路径或更新访问顺序；旧实现也不能称为 LRU。两份 A* 代价相同只能证明相互一致，不能独立证明最优。本页不再把这些数字用于预算或真实项目效果证明。
> 旧 [Windows 原始输出](results/astar_benchmark_win_x64_msvc.txt) 保留原字节，包含 `2091573859x` 与 `-2108667674x` 的异常打印。其 SHA-256 为 `297fc4cecd624d83b7582e5ab6aab61f30013fb02b40890404994b71195e1b1f`。旧 `%d` 接 `double` 属类型不匹配，不应把垃圾输出悄悄摘要成 `1.0x`。
> 本次是原创、单线程、静态网格的合同实验及本机观测。**不是工作日志中真实项目的 before/after，不是 UE、线上 Bot 或容量验收。** 原始工作日志、旧结果和已通过的 `astar-contract` 内容均未改写。

## 1. 问题与反例

缓存是否值得用，不能只问一次哈希查询有多快。调用者真正要的是一条可用、可持有的路径：查找、更新 LRU、复制、消费结果都有成本；未命中时还多出存储和淘汰成本。

两个最小反例决定本次实验合同：

- 容量 2，依次 `put(A), put(B), get(A), put(C)`。真正 LRU 应淘汰 B；若命中不 touch，便会错误淘汰 A
- 两种 A* 共享错误的邻接规则或启发，仍可能返回相同的错误代价。需要不使用 A* 队列和启发的独立最短路 oracle

旧实验把缓存查询限制在特意安排的重复分支，`hits / cacheable` 是重复子集命中率。即使它为 100%，也不等于所有请求都命中。本次对**每个请求**查缓存，同时报告总体与安排重复子集两个分母。

## 2. 搜索合同

本次 [C++ 实现](src/astar_benchmark.cpp) 的固定边界如下：

| 项 | 合同 |
| --- | --- |
| 图 | 有限矩形网格；一次查询期间障碍和代价固定 |
| 邻接 | 8 方向，正交 1、对角 √2，使用 double |
| 穿角 | 性能实验禁止穿角：对角两侧的正交格均须可走；功能穷举分别检查允许/禁止两种政策 |
| 启发 | Octile，与本实验边权匹配；一般图证明和不一致启发反例见原有合同实验 |
| OPEN | 堆与线性扫描都存不可变 `(f, queuedG, node, serial)`；同键先较大 g，再先入序号 |
| 松弛/终止 | 严格更优才重插；过滤旧 g 快照；有效目标出队终止；没有永久 CLOSED 禁止改进 |
| 端点 | 越界或阻塞为 Invalid；合法起终点相同为 Found，单节点路径、代价 0 |
| 不可达 | Unreachable、空路径、无穷代价；不同于 Invalid 和缓存未命中 |
| 返回 | 独立 vector，调用者可修改而不影响缓存；校验首尾、首格、所有边、障碍、穿角、重复节点和累计代价 |

旧 Windows 实验允许对角穿角，本次性能实验禁止。实现、邻接、工具链和测量范围均有变化，**不能把两份报告拼成优化前后 A/B**。

`pops` 包含旧条目和目标；`stale_pops` 是旧快照；`expanded` 只计实际展开邻居的有效出队，目标不计入。相同启发不保证不同 tie-break、邻居顺序或终止政策的扩展数相同。本次固定这些策略，是为了减少比较中的混杂变量，而非证明两种容器一般都展开一样多。

## 3. LRU 与返回所有权

`PathCache` 使用 `unordered_map<Key, Item>` 加 `list<Key>`：map 指向唯一的 recency 节点，表头为最近访问。

1. 命中时把同一节点 splice 到表头，不额外追加重复节点
2. 覆盖同 key 时更新值并移动原节点，不扩大条目数
3. 满容量时删除表尾 key，再从 map 删除同一对象
4. 容量 0 禁用缓存；未命中不改变访问顺序
5. 不可达结果可以缓存，命中由单独 bool 表达，不能用空路径判断 miss
6. 缓存绑定一个导航数据集，key 含起点、终点、profile、epoch；切换 epoch 清空旧条目，旧 epoch 写入被拒绝
7. 对外 `get` 复制结果；底层 `lookup` 的借用指针仅在下次缓存修改前有效，不跨线程/异步保存
8. 缓存对象本身不可复制或移动：map中的迭代器绑定本对象的list；默认复制会把旧容器迭代器带入新对象。显式删除copy/move构造与赋值，并用static_assert守住接口合同

单元素 [list::splice](https://eel.is/c%2B%2Bdraft/list.ops) 保持节点迭代器有效、操作为常数时间；哈希查找只有平均常数复杂度。长度 L 的路径复制与完整消费是 **O(L)**，不能称为 O(1) 路径拷贝。

这是单线程教学缓存，没有提供异常强保证或完整异常恢复合同；例如分配失败后不能捕获并假设结构仍可继续复用，实验应失败退出。它也不是可跨线程共享的缓存库。

本实验 profile 只是缓存隔离字段，未实现不同体型、导航过滤器或动态避障。生产缓存还要把真实能力/代价策略映射到 key，并验证当前位置接入段、动态障碍、字节上限和并发所有权；这部分不能由静态网格测试代替。

## 4. 功能测试与故障对照

独立 Bellman-Ford oracle 通过枚举顶点对建立合法边，不复用 A* 的邻居循环、OPEN、启发或父节点数组。

- 3×3 全部 512 个障碍掩码，所有合法有序端点对，分别允许/禁止穿角：共 **23,040 个查询合同**，每个都检查堆与线性实现，共 46,080 份搜索结果
- 另有 5×5 启发回归、阻塞/越界端点、错误起点/首格/边/代价的验证器负例
- LRU 覆盖容量 0/1/多槽、hit touch、同 key 覆盖、miss 不改顺序、淘汰对象、空不可达结果、独立返回、profile 隔离、地图修改后 epoch 失效与新负结果复用
- 正常运行：13 组通过、0 失败
- 故意关闭 hit touch：12 组通过、1 组失败，失败名称必须是 `lru_hit_touches_recency`，进程退出 1。不是任意崩溃或任意非零都算反例成功
- Python runner 的 10 项合同测试覆盖已有目录保全、缺编译器、超时/非零、空数据、非有限时长、批次分母、重复样本和命中数越界
- 同次执行还运行未修改的 [astar-contract](../astar-contract/README.md) 两个入口；其中正文双语言测试仍使用其原有合同，不冒充本次性能采样

全部原始命令、stdout、stderr、退出码与超时标记见 [commands.json](results/astar-measurement-2026-10-04/commands.json)。测试覆盖这些反例，不替代 A* 的一般证明，也不证明生产缓存、线程安全或完整 Bot 行为正确。

## 5. 测量设计

计时器在搜索/缓存外部包围真实操作，使用 [steady_clock](https://eel.is/c%2B%2Bdraft/time.clock.steady)。返回路径的每个节点都参与导出的 checksum，避免“算完没人用”；路径正确性断言和 CSV 写入不放入主计时段；lookup-only 仍包含命中非空检查和元数据 checksum。缓存与无缓存都返回独立路径并完整消费；miss 额外存储成本保留在测量中。

| 场景 | 输入与状态 | 测量样本单位 |
| --- | --- | --- |
| search | 同地图、同查询；堆与线性；每轮/每查询交换先测顺序 | 单查询，含结果消费 |
| cold-fill | 每轮空缓存，64 个不同 key，容量 128 | 单查询，全部 miss |
| warm-hit | 先装填64个key，再循环请求；装填在计时外 | 每32次的批均值，末批可不足32 |
| mixed-pressure | 每轮空缓存；256-key池，另安排每4次中的一次重复5次前请求 | 每32次的批均值，含miss/装填/淘汰 |
| lookup-only | 已装填，借用结果元数据，不复制或完整消费路径 | 每256次的批均值；不能代替完整缓存调用成本 |

每个密度阈值使用固定种子地图；25%/40% 是生成器阈值，不保证实际障碍格恰好占该比例。端点可走**不意味着可达**；报告保留 found/unreachable 数，负结果命中也计入总命中。所有方法使用同一已生成请求序列，源码中固定 PRNG 算法，`samples.csv` 的 input 行保存每个请求坐标及安排重复标记。

5 轮重复；搜索预热每实现最多32次，不计入样本。性能地图100×100，seed=20261004，map seed 分别加25/40。单位是纳秒，summary另报告每轮总量；分位数采用最近秩 `ceil(q*N)`。**批均值的 P99 不是单请求 P99**，五轮相同请求也不是五套独立用户流量。

时钟 type 的 period 为1ns并不意味着1ns实际精度。本次1000个空时钟对最小正观测20ns、中位20ns、P99 31ns；不从每个样本机械减去这个数。原始零值、低于空测量中位的样本数仍被报告，不把精度不足伪装成零开销。

## 6. 本次观测，不设必须提速的门槛

实际：Linux x86_64，GCC **14.2.0**，语言标准 **C++17**，`-O2 -Wall -Wextra -Wformat=2 -Werror -pedantic`；CPU报告AMD EPYC 9V74。完整系统、时钟、编译器原文、源文件/二进制hash见 [provenance.json](results/astar-measurement-2026-10-04/provenance.json)。本次运行在共享宿主，未做 CPU 隔离、亲和绑核或频率锁定；容器资源配额读取结果缺失，不能推定独占物理机或没有其他负载。

完整生成区间为 UTC 2026-10-04 12:08:10.587981 至 12:08:26.888517；其中 benchmark 子进程从 12:08:14.836587 开始，记录经过 10.423583246 秒（据此推算结束约 12:08:25.260170）。编译与功能检查在该采样命令之前；没有测量同时期宿主的外部 CPU 竞争，不能称为无噪声基准。

下表为完整“返回路径+消费”调用的加权均值，单位 μs/请求，不是单请求P99：

| 场景 | 25%阈值：无缓存 / 缓存 | 40%阈值：无缓存 / 缓存 | 本次含义 |
| --- | ---: | ---: | --- |
| cold-fill | 113.368 / 114.953 | 222.926 / 229.244 | miss装填稍慢，缓存不是免费 |
| warm-hit | 125.059 / 0.169 | 241.587 / 0.266 | 人工预热、全部命中，只代表该热点集合 |
| mixed-pressure | 121.075 / 51.662 | 206.470 / 89.045 | 总命中率57.3%/56.4%；安排重复子集都是100% |

warm/mixed行来自批均值，不能把169ns当任意单次命中的保证；40%地图包含更多不可达请求，缓存负结果也是收益的一部分。完整状态数量、路径长度、扩展数、每轮结果与分位数见 [summary.json](results/astar-measurement-2026-10-04/summary.json)，全部 [原始样本](results/astar-measurement-2026-10-04/samples.csv.gz) 和 [覆盖校验](results/astar-measurement-2026-10-04/validation.json) 可重算。

独立search组单查询P99：25%阈值堆688.816μs、线性3263.221μs；40%阈值堆755.073μs、线性1424.380μs。这是本次两个固定地图的观测，不证明所有地图上堆一定快，也不提供Tick容量保证。若下次缓存更慢、差异被噪声淹没或时钟无法分辨，应保留结果，而不是放宽测试去追求加速。

## 7. 重跑与失败语义

依赖Python3标准库及g++。下列相对命令从仓库根运行；若在其他工作目录，传入脚本绝对路径。输出路径必须尚不存在。全套会运行已有 `astar-contract`，该套也要求g++，缺失就失败，不跳过。

```bash
python3 -B evidence/algorithms/astar/scripts/run_benchmark.py \
  --output-dir /tmp/astar-my-new-run --queries 1000 --repeats 5 --seed 20261004
```

源码路径由脚本定位。临时二进制/对象文件在系统临时目录自动清理；持久目录仅有provenance、commands、samples、summary、validation五文件。已有目录、缺编译器、编译失败、超时、功能失败、预期负例没有正确失败、样本缺失或历史输出hash变化，均返回非零。已有目录、缺编译器或旧 hash 不符在预检阶段直接拒绝，不创建结果目录；创建目录后发生的执行失败保留已完成命令与FAIL状态，不补写成成功。

Windows保留 [build_run.ps1](scripts/build_run.ps1) 入口，要求显式新目录，默认g++；可传 `-Compiler cl`，但需要已初始化的VS环境，且未修改的合同套仍需g++。**本次没有运行Windows入口或MSVC，没有重造Windows证据。**

```powershell
.\evidence\algorithms\astar\scripts\build_run.ps1 -OutputDirectory 'D:\Results\astar-new-run'
```

旧结果文件不会被此入口重写。不要把本目录新数据回填成两篇原始工作日志当时已经获得的项目测量。

### 7.1 仓库归档与本地原始输出

runner在**新的本地输出目录**仍产生原始`samples.csv`和四份JSON，共五文件；源码、runner、计时流程与数值不因归档改变。仓库仅把本次CSV无损归档为[samples.csv.gz](results/astar-measurement-2026-10-04/samples.csv.gz)，其余四份原始JSON保持整字节，`provenance.samples_sha256`仍是**解压后CSV**的hash，不能拿gzip文件hash代替。

- 压缩：gzip level=9，mtime=0，不写入源文件名；固定这些参数消除时间戳/文件名差异
- 原始CSV SHA-256：`d3db5d3b3e67d33415e6b38639ec6a4380e9f72eeb8eaea0e687f3e965c51a1b`
- gzip SHA-256：`a995624d3476a2994123e4f7d3274e19249c3706078a634ac7a008f0dc9e7019`
- 原始3,902,044 bytes，归档538,644 bytes；原CSV另作整字节保全，不删除任何样本或改写数值

从仓库根解压到**尚不存在**的外部路径，避免覆盖已有文件：

```bash
python3 -B - <<'PY'
import gzip, hashlib
from pathlib import Path
archive = Path("evidence/algorithms/astar/results/astar-measurement-2026-10-04/samples.csv.gz")
raw = gzip.decompress(archive.read_bytes())
assert hashlib.sha256(raw).hexdigest() == "d3db5d3b3e67d33415e6b38639ec6a4380e9f72eeb8eaea0e687f3e965c51a1b"
with Path("/tmp/astar-restored-samples.csv").open("xb") as output:
    output.write(raw)
PY
```

也可以不落地解压文件，直接用同一runner的统计函数重算全部摘要与schema校验；这只重算已有观测，不重新计时：

```bash
python3 -B - <<'PY'
import csv, gzip, importlib.util, json
from pathlib import Path
base = Path("evidence/algorithms/astar")
result = base / "results/astar-measurement-2026-10-04"
spec = importlib.util.spec_from_file_location("astar_runner", base / "scripts/run_benchmark.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
with gzip.open(result / "samples.csv.gz", "rt", encoding="utf-8", newline="") as source:
    summary, validation = runner.summarize(list(csv.DictReader(source)), 1000, 5)
assert summary == json.loads((result / "summary.json").read_text(encoding="utf-8"))
saved = json.loads((result / "validation.json").read_text(encoding="utf-8"))
assert all(saved[key] == value for key, value in validation.items())
print("PASS: archived observations reproduce summary and sample validation")
PY
```

## 8. 关联与未验证边界

- [A* 主文](../../../知识/02-数学与游戏算法/路径搜索与导航/02-A星算法与优化.md)维护算法概念；[原有合同实验](../astar-contract/README.md)维护正文双语言与队列反例。本次不改它们
- [假人AI完整链路](../../../知识/06-游戏AI/战斗战术与机器人/07-假人AI完整链路.md)只消费本地搜索/缓存证据，完整调度/移动/AOI/网络仍待端到端验证
- [AI与寻路时间预算](../../../知识/07-网络与游戏服务端/运行调度与过载保护/11-AI与寻路时间预算.md)说明协作式分片、过期结果、排队与端到端延迟；不能以单查询或批均值代替世界Tick
- [2026-08-03日志](../../../工作日志/2026-08-03-假人AI-A星优化与耗时测试.md)、[2026-08-05日志](../../../工作日志/2026-08-05-假人寻路路径缓存方案.md)是历史过程材料，本地代理实验不替代真实项目证据

未验证：生产地图/导航代价、不同agent几何、NavMesh/UE接入、动态障碍同步、多线程、安全实时截止、Bot行为分布、AOI与网络容量、Windows/MSVC和Linux perf。本机观测不建立跨平台固定倍率。
