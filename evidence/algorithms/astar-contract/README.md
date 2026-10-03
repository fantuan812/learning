---
type: Evidence
title: "A* 队列、重开与终止合同反例"
description: "用最小图复现可变键、旧条目、不一致启发、过早终止和同键策略；直接编译/执行正文示例。"
status: stable
verified: []
maturity: L2
updated: 2026-10-03
---

# A* 队列、重开与终止合同反例

本目录服务于 [A* 算法与优化](../../../知识/02-数学与游戏算法/路径搜索与导航/02-A星算法与优化.md) 的第 3、5、6 节。先修是 [图论、松弛与 Dijkstra](../../../知识/02-数学与游戏算法/路径搜索与导航/01-图论基础与搜索算法.md)。这是**正确性合同实验**，不是速度评测；既有 [堆/线性扫描性能实验](../astar/README.md) 仍保留自己的地图与测量口径。

## 1. 修复对象与可复现结果

基线 `ea63344a974ff41efaa7a0368dd56cac43a68fef` 的正文 C++ 代码存在两个可分离问题：

- 8 方向、斜向 √2 代价使用曼哈顿启发，会高估一格对角线的剩余代价
- 堆保存 `Node*`，更优路径只修改节点 f，却不执行 decrease-key 或重插不可变快照；流程图和伪代码也漏了这一队列动作

本次正文采用固定 Octile、不可变入堆值、严格 `<` 松弛、旧快照过滤、有效目标出队终止；两个语言例子都显式禁止穿角。以下小图、变体与测试为本目录编写，没有移植第三方测试集。

| 证据 | 故意错误的策略 | 正确策略 / 本次观察 |
| --- | --- | --- |
| 8 方向启发高估 | 把单语言 Octile 临时替换成曼哈顿，5×5 用例返回 6.414213562373095 | 正文两语言均返回独立 Bellman-Ford 最优值 5.82842712474619 |
| 可变堆键 | 只改 Node 字段；C++ 复现返回 5 | 重插独立快照返回 3，已知最优路线 S→B→A→G |
| 一致 h 的重复入堆 | 旧条目继续扩展 A，A 扩展 2 次 | 5 次 pop、1 个 stale；A 有效扩展 1 次，最优代价 22 |
| 可采纳但不一致 h | 永久关闭 A，代价 5 | 改进后 reopen，A 扩展 2 次，最优代价 4 |
| 发现目标立即结束 | 首次扫描到目标返回 10 | 有效目标出队返回 2 |
| 同键与等价路径 | `(priority,node)` 比较不可排序对象会抛 TypeError | 唯一序号隔离节点比较；FIFO 与较大 g 优先返回相同最优代价 2，但有效扩展数为 3 与 2 |
| 零代价环 | 等价改进若也重插会有循环风险 | 严格 `<`，S→A→B→G，4 pops，代价 1 |

队列/重开等小图与 h 列在正文第 3.6 节和 [astar_contract.py](astar_contract.py)。`pops` 包含旧条目和目标；`expanded` 只计通过过滤且实际遍历邻居的次数，目标不计入。`stale` 以入堆 g 快照与当前最佳 g 不同判定。故意关闭过滤/重开或提前返回的开关仅用于教学反例。

## 2. 运行方式

依赖：Python 3 标准库和 `g++`（支持 C++11）；无 pip 依赖、无外部数据下载、不改 Git index/history/remote。

在仓库根执行：

```bash
python3 -B evidence/algorithms/astar-contract/astar_contract.py
python3 -B evidence/algorithms/astar-contract/test_astar_contract.py
```

测试从任意工作目录也可执行，路径由脚本自身位置解析；`-B` 避免生成 `__pycache__`。临时 C++ 源码与可执行文件放在系统临时目录并自动清理。`g++` 缺失或编译失败会使测试失败，**不会跳过后声称全通过**。

实际工具链：Python 3.12.14；g++ 14.2.0（Debian 14.2.0-19）。编译标志：`-std=c++11 -O2 -Wall -Wextra -Werror`。

[本次真实合并输出](results.txt) 保存运行环境、正常测试与两个负对照命令的 stdout/stderr、退出码和待测文件 SHA-256。测试摘要为：

```text
mutable_queue: old_cost=5 optimal=3 fixed_cost=3
PASS: 14 tests; 200 graph cases; 11520 small-grid + 1 heuristic-regression queries per language; C++11 -Werror
```

日志中的 unittest 总运行秒数只是工具原始输出，不是 A* 延迟或可比较的性能样本。

## 3. 测什么，如何避免“另一份示例通过”

[test_astar_contract.py](test_astar_contract.py) 会**直接从正文第 3.4 / 3.5 节抽取代码块**：

- Python：编译并执行该代码块，然后调用其中 `a_star`
- C++：把该代码块与输入输出驱动组合，C++11 编译后运行
- 穷举 3×3 网格全部 512 个障碍掩码，以及每张图中全部有序可走端点对，共 **11,520 查询 / 语言**；比较独立 Bellman-Ford 代价，同时验证路径首尾、合法边、不穿角、无 parent 环
- 每种语言再增加 1 个下面的 5×5 启发回归，总计 **11,521 次与 oracle 对照的网格查询 / 语言**
- 两种语言另测空图、空行、非矩形输入、阻塞端点、起点/终点越界；穷举集已含起终点相同、不可达、多条等价路径和合法对角线
- 一般图实验另跑 200 个固定种子 `20261003` 的 6 节点有向图，边权为整数 0…4；先计算真实目标距离，再生成不高估它的 h，与独立 Bellman-Ford 比较结果和重建路径

### 3.1 启发缺陷必须有能失败的负对照

初版 12 项测试的 3×3 穷举不足以检测曼哈顿误用于 8 方向的缺陷：把 h 改错仍可通过。因此补入独立审查提供的确定性地图；不能用用例数量代替“确实能抓住该错误”的证据。

```python
grid = [[1,1,1,1,1],
        [1,1,0,1,1],
        [1,1,1,0,1],
        [1,1,1,1,1],
        [1,1,1,1,1]]
start, goal = (1, 0), (4, 4)
```

邻接仍是正交 1、斜向 √2、禁止穿角。独立枚举合法边后用 Bellman-Ford 求 oracle，得到 `3 + 2√2 = 5.82842712474619`；测试不把 A* 另一语言的结果当作基线。

- 正常运行：正文 C++ 和 Python 都得到 oracle 代价；14 项全部通过，退出码 0
- Python 负对照：只将抽取的 Python 代码字符串的 h 替换为曼哈顿，代价变为 `6.414213562373095`；新 `test_13` 失败，其余 13 项通过，退出码 1
- C++ 负对照：只将抽取的 C++ 代码字符串的 h 替换为曼哈顿，代价同样错误；新 `test_14` 失败，其余 13 项通过，退出码 1

可重复执行如下**预期失败**命令；它们的非零退出码证明测试抓住回归，不代表正常版本失败：

```bash
python3 -B evidence/algorithms/astar-contract/test_astar_contract.py --mutant python-manhattan
python3 -B evidence/algorithms/astar-contract/test_astar_contract.py --mutant cpp-manhattan
```

替换仅发生在进程内字符串；临时 C++ 文件位于自动清理的系统临时目录。正文、实现及测试源文件不被 mutation 命令写回。输出记录同时比较运行前后 SHA-256，确认正常/负对照运行没有污染源文件。负对照不是基线全文的执行：它隔离验证“把正确示例的 h 改回曼哈顿”这一处退化。

### 3.2 堆键缺陷的独立提取

[mutable_queue_regression.cpp](mutable_queue_regression.cpp) 是原有队列策略的最小提取：保留 `Node*` 比较器、opened/closed 与“只改字段，不恢复堆”的动作，令 h=0 以隔离启发误差。它故意验证旧策略得到错误代价 5，修正版得到 3。违反堆不变量后的行为不能作为跨标准库保证；这里记录的是本次编译运行的可复现观察。正文网格实现与该最小图分别受测，没有把最小图结果冒充原始整段网格代码的执行结果。

## 4. 正确性前提与未验证边界

- 一次搜索期间图、边权与 h 固定；图有限，边权有限非负；h 可采纳且 h(goal)=0。通用实现允许严格改进后重开，`closed` 不作永久黑名单
- 图反例使用整数，断言是精确值。网格使用 double / Python float，路径代价对比用 `assertAlmostEqual`；这不证明无限精度下所有地图的比较行为，也不承诺跨平台位级一致
- 小图与种子测试能防止这些具体回归，不能替代一般最优性证明；没有性能结论、UE/Unity/NavMesh 集成验证、JPS/HPA* 实现验证、多线程安全验证、内存压力或大型地图测试
- 未运行 sanitizer 或其他标准库/CPU 组合；两个语言例子不是生产导航 SDK
- 本实验脚本不执行仓库 PowerShell 门禁；整合阶段另行实跑并报告。文章 maturity 保持 L2、verified 保持空列表，测试通过不伪造审核事件

## 5. 原理与实现来源

来源核对日期：2026-10-03。以下只支撑规则说明，不是本目录实验结果来源。

- [Python 官方 heapq 文档：Priority Queue Implementation Notes](https://docs.python.org/3/library/heapq.html#priority-queue-implementation-notes)：同键对象比较、唯一序号、优先级修改与堆不变量
- [Amit Patel：Implementation notes](https://theory.stanford.edu/~amitp/GameProgramming/ImplementationNotes.html)：改进已关闭节点时的 reopen，以及一致启发下该动作何时无需发生
- [Red Blob Games：Implementation of A*](https://www.redblobgames.com/pathfinding/a-star/implementation.html)：作者展示的重复入队、严格代价改进和目标出队实现
