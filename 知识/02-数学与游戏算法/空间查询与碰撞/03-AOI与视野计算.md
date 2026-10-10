---
type: Mechanism
title: "03-AOI 与视野计算"
description: "从明确的兴趣谓词、候选过滤到进出事件与视野状态，区分离散格政策和精确圆形兴趣。"
status: stable
verified: []
maturity: L2
updated: 2026-10-10
sources:
  - title: "Mirror Spatial Hashing — How It Works"
    resource: https://mirror-networking.gitbook.io/docs/manual/interest-management/spatial-hashing
  - title: "Mirror Interest Management"
    resource: https://mirror-networking.gitbook.io/docs/manual/interest-management
  - title: "Python 3.12 Built-in Types — Numeric Types and Set Types"
    resource: https://docs.python.org/3.12/library/stdtypes.html
  - title: "C++17 working draft N4659 — expr.shift"
    resource: https://timsong-cpp.github.io/cppwp/n4659/expr.shift
---
# 03-AOI 与视野计算

> 知识成熟度：L2。2026-10-10核对官方规则并完成静态推导；第2节原样Python例及第7节固定有限用例已运行，属于局部算法证据，不将本文全部视野、网络或性能内容升级为已测试。已有AOI和Tick实验仍是各自模型的真实历史证据。
> 知识基线：Python3.12整数与集合语义，本轮实际解释器3.12.14；旧有符号左移问题按C++17 N4659解释。算法例采用二维整数点实体、单观察者、精确圆边界包含、有序恰好一次本地事件。
> 最后更新：2026-10-10（区分离散格政策与精确圆合同，补完整事件例和固定有限验证；未运行UE、真实LOS、网络、并发、编译或压力测试）。
> 本文负责算法层：谁属于某观察者的兴趣集、如何从集合变化得到事件、怎样继续做视野判断。索引实现见[空间分区与索引](04-空间分区与索引.md)；连接、复制、限流和交付见[运行时AOI](../../07-网络与游戏服务端/状态复制与兴趣管理/05-AOI与InterestManagement.md)。这里不声称实现了真实网络、引擎射线或完整反作弊系统。

## 1. 先决定“关心谁”，再选择索引

世界快照里有实体的位置和属性，观察者需要的通常只是其中一部分。令 `I_t(o)` 是时刻t、观察者o需要接收的实体ID集合。它由游戏的**兴趣谓词**决定，空间索引只帮助更快找到可能满足谓词的候选。

例如，二维点实体、半径R的精确圆形兴趣为：排除观察者自身，并满足 `dx²+dy²≤R²`。边界接触算在内。阵营、场景、隐身、距离层级可以加入谓词；规则变化时即使没有实体移动也可能改变兴趣集。

另一种合法政策是“观察者所在格及八邻格都属于兴趣集”。[Mirror Spatial Hashing的官方说明](https://mirror-networking.gitbook.io/docs/manual/interest-management/spatial-hashing)就是这种按邻格发送的入口。它与精确圆的结果不同，不能用圆形答案判它失败，也不能把它的结果标成精确圆。

本文正常例选择精确圆；九宫格仅产生候选。格边长s=R>0，格坐标为 `(floor(x/s), floor(y/s))`。圆内点每个坐标分量与观察者相差不超过R，因此格坐标最多差1，九格不会漏掉圆内点；角落格仍可包含圆外点，必须再做距离过滤。

取R=s=10，观察者A=(0,0)。B从(9,0)移到(9,9)，格坐标一直是(0,0)，但距离平方从81变为162。圆形兴趣应产生Leave；离散九宫格政策则仍保留B。**是否跨格只决定索引是否迁桶，不能替代精确兴趣的重新判断。**

兴趣关系一般有向。若A的半径10、B的半径3、双方相距5，则B属于I(A)，A不属于I(B)。发送B的变化时找的是“哪些观察者的集合包含B”，不是直接取I(B)；视锥也天然可能不对称。

## 2. 集合怎样变成接收端状态

对同一观察者，先完成本次权威快照与规则的计算，得到新集合N，再与上次已产生事件的集合O比较：

- `Enter = N - O`：这个ID由不需要变为需要，事件带创建所需状态
- `Leave = O - N`：不再需要这个ID，接收端移除它
- `Stay = N & O`：一直需要这个ID；其中状态发生变化的才产生本文的Move

Enter中的对象不再重复发同帧Move；Leave也不应携带对象离开后的新位置。单机例把事件按Leave、Enter、Move和ID排序，只是为了输出可复核，不代表网络协议必须采用这个顺序。Python的set没有稳定遍历顺序，显式排序用于展示。

Leave只表示这个观察者不再保留该副本，不等于权威世界销毁了实体。下面第2帧B仍在世界里，只是离开A的圆；第4帧C确实从权威快照删除，同样会让A收到Leave。这两种原因共用本例的副本移除操作，但世界生命周期不同。

### 完整最小例：从七帧输入走到接收端副本

保存下面**整个代码块**为 `aoi_interest_example.py`，在Python3.12执行 `python3 -B aoi_interest_example.py`。仅标准库，无额外文件或包。输入是字符串ID到二维整数坐标元组的字典；bool、float、零/负半径、缺失观察者被拒绝。Python整数避免固定宽度溢出，但大整数运算不是任意规模的常数时间保证。

为了看清语义，`interest`逐实体判断它是否在九格候选区，仍是全表扫描；这不是新写一套网格索引，也不声称降低查询复杂度。需要性能时使用[已有PointGrid主文](04-空间分区与索引.md)提供候选，保留后面的圆过滤与事件合同。

```python
# aoi_interest_example.py -- complete Python 3.12 teaching example
def interest(world, observer, radius):
    if type(radius) is not int or radius <= 0:
        raise ValueError("radius must be a positive int")
    if type(world) is not dict or observer not in world:
        raise ValueError("world must contain observer")
    for entity, pos in world.items():
        if type(entity) is not str or not entity:
            raise ValueError("IDs must be nonempty strings")
        if (type(pos) is not tuple or len(pos) != 2
                or any(type(v) is not int for v in pos)):
            raise ValueError("positions must be pairs of ints")
    ox, oy = world[observer]
    cx, cy = ox // radius, oy // radius
    candidates = {
        entity for entity, (x, y) in world.items()
        if entity != observer
        and abs(x // radius - cx) <= 1
        and abs(y // radius - cy) <= 1
    }
    current = {
        entity: world[entity] for entity in candidates
        if ((world[entity][0] - ox) ** 2
            + (world[entity][1] - oy) ** 2 <= radius ** 2)
    }
    return candidates, current


def make_events(previous, current):
    old_ids, new_ids = set(previous), set(current)
    events = [("leave", e, None) for e in sorted(old_ids - new_ids)]
    events += [("enter", e, current[e]) for e in sorted(new_ids - old_ids)]
    events += [("move", e, current[e]) for e in sorted(old_ids & new_ids)
               if previous[e] != current[e]]
    return events


def receive(replica, events):
    result = dict(replica)
    for kind, entity, pos in events:
        if kind == "leave":
            del result[entity]
        elif kind == "enter":
            if entity in result:
                raise ValueError("duplicate enter")
            result[entity] = pos
        elif kind == "move":
            if entity not in result:
                raise ValueError("move before enter")
            result[entity] = pos
        else:
            raise ValueError("unknown event")
    return result


def main():
    frames = [
        {"A": (0, 0), "B": (9, 0), "C": (20, 0)},
        {"A": (0, 0), "B": (9, 1), "C": (20, 0)},
        {"A": (0, 0), "B": (9, 9), "C": (20, 0)},
        {"A": (0, 0), "B": (9, 9), "C": (6, 8)},
        {"A": (0, 0), "B": (9, 0)},
        {"A": (20, 0), "B": (9, 0)},
        {"A": (20, 0), "B": (9, 0)},
    ]
    previous, replica = {}, {}
    for frame, world in enumerate(frames):
        candidates, current = interest(world, "A", 10)
        events = make_events(previous, current)
        replica = receive(replica, events)
        if replica != current:
            raise AssertionError("replica differs from current interest")
        print(frame, "candidates=", sorted(candidates),
              "events=", events, "replica=", sorted(replica.items()))
        previous = current


if __name__ == "__main__":
    main()
```

实际输出（2026-10-10，原样运行一次；与执行前固定的7行预期逐字一致）：

```text
0 candidates= ['B'] events= [('enter', 'B', (9, 0))] replica= [('B', (9, 0))]
1 candidates= ['B'] events= [('move', 'B', (9, 1))] replica= [('B', (9, 1))]
2 candidates= ['B'] events= [('leave', 'B', None)] replica= []
3 candidates= ['B', 'C'] events= [('enter', 'C', (6, 8))] replica= [('C', (6, 8))]
4 candidates= ['B'] events= [('leave', 'C', None), ('enter', 'B', (9, 0))] replica= [('B', (9, 0))]
5 candidates= [] events= [('leave', 'B', None)] replica= []
6 candidates= [] events= [] replica= []
```

第1帧B仍在圆内且坐标变了，所以只发Move。第2帧B仍在候选格中，却离开圆，说明过滤不能省略。第3帧C满足6²+8²=100，边界被接纳。第4帧C已从权威快照删除，它仍存在于旧集合，因此正常发Leave；B则重新Enter。第5帧只移动观察者也足以改变结果。第6帧不变，不应重复创建或删除。

这里的`previous`和接收端都从空字典开始，由函数自身的前一帧结果维护；模型中实体状态只有位置元组，所以Enter携带的就是完整状态。事件有序、恰好交付一次，且没有同帧ID复用。真实网络丢包、重复、乱序、对象代际、完整状态基线与重连需要额外协议，不能靠这三个函数解决。ID被复用时，应把代际纳入身份；否则同一ID新对象会被误当作旧对象的Stay。

## 3. 换空间索引时，哪些合同必须保留

九宫格的充分覆盖依赖s≥R且坐标取floor。一般s>0、R≥0时，可遍历 `floor((x-R)/s)` 到 `floor((x+R)/s)` 及相应y范围，再做圆过滤。半径变大却固定只查八邻格可能漏对象。负坐标必须正确分桶，例如s=10时-1应在-1格；截断到0会破坏区间定义。

旧C++17骨架把负格坐标作 `int64_t(cx) << 32`，这在该标准的[N4659 expr.shift/2](https://timsong-cpp.github.io/cppwp/n4659/expr.shift#2)下不成立；此处不泛化其他标准版本。若确需把两个`int32_t`坐标无碰撞打包为键，应先分别转`uint32_t`，再将高半转`uint64_t`后左移32，与低半作或；容器键也用`uint64_t`。坐标必须先证明可表示为int32，邻格加减也不能溢出。元组/结构体键可避免手工位拼接，但仍需正确相等比较。

灯塔法把实体登记到覆盖它的固定灯塔，再合并共享灯塔内实体。共享灯塔仍只是候选：两个实体分别位于同塔两边，可能彼此相距2R。对无限正方形灯塔网格，间距s、实体登记半径R，任意距离≤R的两点P、Q：其中点M距最近塔≤s/√2，塔距P/Q都≤s/√2+R/2。因此s≤R/√2是两者至少共享一塔的充分条件；它不是必要条件，也不保证共享者都在精确圆内。有限地图边缘还必须实际保留所需塔。

十字链表分别按x/y排序。x范围命中集合与y范围命中集合相交得到矩形候选，再按所选谓词过滤。它的扫描长度依赖分布；一条狭长带或密集热点可能让扫描接近全量，不能把O(√n+k)写成任意输入保证。空间分桶、动态树、静态树或混合结构的选择要依据查询形状、对象运动、候选数量与维护成本；动态场景并不自动排除树。

无论选择哪一种，实体移动需要先更新真实坐标；跨桶才迁索引；位置、朝向、半径或可见规则改变后，受影响观察者需要得到新的兴趣判断。仅更新移动实体自己的集合不足以维护全部有向观察关系。

## 4. 从圆形候选继续算视野

### 4.1 先算一个能手推的视野扇形

观察者位于(0,0)、朝+x、完整视野角90°、半径10，边界接触算可见。圆内点还需满足 `dx≥0` 且 `dy²≤dx²`：这是半角45°的闭扇形，可以用整数精确判断，不需要normalize或三角函数。

- B=(9,0)：81≤100且0≤81，圆与扇形都通过
- C=(6,8)：100≤100但64>36，在圆内、扇形外
- D=(6,6)：72≤100且36=36，恰好在扇形边界，通过
- E=(-1,0)：在圆内但朝向相反，未通过；不能只比较平方而忘记dx符号
- F=(0,0)：零距离在本文规则下直接算可见，不执行归一化

一般朝向的非零向量d与目标偏移v，可用 `dot(v,d) ≥ |v||d|cos(halfAngle)`。须明确角度单位、halfAngle范围、有限值和零方向拒绝策略；若先把d归一化也要核长度。对任意半角都盲目平方会丢符号信息，尤其在超过90°的半角范围不能沿用上面的特例。

### 4.2 遮挡是下一条独立事实

这一步只查询B、C、D三个目标。假定地图的视线过程返回D被墙挡住，而B可通视，则圆→扇形→LOS三个阶段依次得到 `{B,C,D}`、`{B,D}`、`{B}`。这演示的是集合组合；“D被挡住”是本例明确给定的输入，不是假装调用了引擎射线。

真实LOS需定义眼睛与目标位置、障碍层、终点接触、自身碰撞忽略及移动障碍版本。读目标所在的一位阻挡数据不能证明整条线不穿墙；网格通常要遍历穿过的格，几何场景要做线段/射线查询。所需工作随穿过的格、候选障碍及后端而变，不能统一写成O(1)。客户端遮挡剔除只决定画面，已经收到的隐藏对象数据仍在客户端内存；[Mirror官方兴趣管理说明](https://mirror-networking.gitbook.io/docs/manual/interest-management)也区分这两件事。

### 4.3 战争迷雾：覆盖计数与探索历史

同一阵营多个单位可能照亮同一瓦片。对每个单位保存其上一版完整可见瓦片集V_old，重新算出V_new后，只对 `V_old-V_new` 做count减1，对 `V_new-V_old` 加1。保留交集不再累加；单位销毁相当于V_new为空。`visible = (count>0)` 每次从计数派生，`explored |= visible`，二者不是同一个布尔值。

一块瓦片的正常轨迹为：没有单位覆盖0/不可见/未探索 → U进入1/可见/已探索 → V也进入2/可见/已探索 → U离开1/仍可见/已探索 → V离开0/不可见/仍已探索。重复提交同一V_new时差集为空，count不变；减少后不得变负。若实现改为每帧全量重算，必须先清零count再累加所有单位，不能与增量方案混用。

服务端计算兴趣或迷雾时，使用已验证的位置、阵营和规则半径；客户端申报值不能未经验证直接成为权威输入。像素纹理、模糊和边缘过渡只表现这份状态，不修改它。历史探索允许显示哪些静态信息、已离开单位保留多久的最后已知状态，是玩法规则，必须与当前可见信息区分。

## 5. 必须先定清的更新与失败边界

圆形例按每次输入快照更新，不承诺连续时间内从未漏看快速穿越。若两个采样点都在圆外，途中进入又离开不会出现在离散结果；需要连续触发时应做扫掠检测或增加规则要求的采样密度。视锥仅转向、门关闭、阵营切换、半径改变也会影响结果，不能只订阅跨格事件。

当边界抖动需要迟滞时，可规定“未订阅者在R_in内进入，已订阅者超过R_out才离开”，且R_out≥R_in。这已变成带状态的兴趣政策，不能仍与单一R的无状态圆逐集合对拍。延迟Leave还可能延长信息暴露；隐藏规则不允许继续发送时不能为了平滑默认续发位置。

出生实体应先成为权威快照里的有效对象，再为相关观察者计算Enter；销毁保留旧兴趣快照才能生成Leave。传送、新场景或重连需要明确快照代际和旧消息处置，不能把“先发全量再发旧Leave”当通用配方。这些交付问题接运行时主文；本文完整例只验证一条本地有序链。

同一批快照内的位置应一致。计算到一半修改世界，会使候选过滤与事件各自看到不同状态。可采用稳定快照、固定更新阶段或适合项目的同步方案；本文没有并发读写。

## 6. 复杂度、带宽与量化怎样判断

若n个实体每个以f次/秒向所有其他实体发送，一对一消息数约为 `n(n-1)f`，不能省掉频率直接称“每秒”。AOI将它改成各观察者兴趣量与更新频率的总和，但密集场景仍可能让大部分观察者关心大部分实体，最坏情况不会因使用网格自动变成线性。

本文候选函数扫描n个实体；事件按ID排序还花费与新旧集合大小相关的排序成本。实际索引查询应分别记录维护成本、访问格数、候选数、最终兴趣数及事件数。灯塔要计注册复制和去重，链表要计扫描长度。性能比较必须在同一兴趣政策下对拍，不能拿方格和圆形两种不同输出的耗时直接当等价算法优劣。

例如500个观察者、平均每人60个持续更新对象、10Hz、每对象20B，纯对象负载为 `500×60×10×20=6,000,000 B/s`，即十进制6MB/s；还没计Enter/Leave、包头、重传、压缩和批包。这个算术例不是本库实测容量，频率与人数不能作为普遍预算。

位置量化可以降低单次负载，但不会被插值自动消除偏差。若把有限坐标x∈[-L/2,L/2]映射到16位无符号整数，可先验证L>0与输入范围，再按最近整数计算 `q=round((x/L+1/2)×65535)`；反解为 `(q/65535-1/2)×L`。在理想实数运算及最近舍入下最大误差为L/(2×65535)，L=10000m时约7.63cm；实际浮点端点与舍入需单独核查。“误差小于一像素”还取决于相机和显示尺度。超域、NaN与无穷应按明确策略拒绝或处理，不能直接cast后当有效值；详见[数据压缩与序列化](../数据结构与编码/06-数据压缩与序列化.md)。

## 7. 现有证据与验证建议

[旧AOI模拟器](../../../evidence/algorithms/aoi/README.md)有2026-08-12 Windows x64/MSVC的[原始输出](../../../evidence/algorithms/aoi/results/aoi_simulator_win_x64_msvc.txt)：200×200格、视野半径3格，扫描49格；以切比雪夫格距为ground truth，100/1000/10000实体的Enter/Leave不一致数都是0。原记录另报欧氏圆与离散方形的差异2/249/25386对/帧。这支持原模型的离散格合同，不是上面新Python圆形例的运行记录，也不能用新圆例否定原0不一致。

[AOI与大规模场景证据](../../../evidence/tests/aoi-scale/README.md)保留原aoi_scale的9断言与dynamic_shard的19断言、热点分布及估算包体结果；[Tick策略证据](../../../evidence/server/tick-scheduler/README.md)有独立的时间单位、上限和相位守恒合同，并保全旧MSVC记录。这些原件继续有效于各自注明的输入、工具链和模型边界；本文不重跑覆盖，也不把其中局部结果扩大成真实网络或本篇全文测试。

2026-10-10在Python3.12.14（解释器构建标识Clang22.1.3）上完成以下四个目标进程，各只运行一次，均未超过10秒。正常源码直接从第2节唯一完整代码块抽取，执行后未改代码；其2,875 bytes的SHA-256为 `9798629b2699f5ed715c0d69e8db1a2f6f0e5885cd3c08ae1ecd6486739614cb`。主例stdout为上面的完整501 bytes，SHA-256为 `41e3b43063d291bf95e497c1a13a69f093105986873a684c5dc494b0764bc814`，和执行前固定预期逐字一致。

| 固定运行 | 输入与判定 | 退出码 | stdout / stderr |
| --- | --- | --- | --- |
| T1原样主例 | 7帧；完整7行输出与每帧副本相等 | 0 | 501 / 0 bytes |
| T2固定边界 | 13命名case：16正向快照、5非法输入；具体集合、事件、副本和拒绝类型/消息均通过 | 0 | 384 / 0 bytes |
| M1删除圆过滤 | 2快照；由 `M1_EXPECT_LEAVE_AND_EMPTY` 指定断言检出错误保留B | 1（预期失败） | 43 / 0 bytes |
| M2仅跨格提交兴趣 | 2快照；由 `M2_EXPECT_LEAVE_AND_EMPTY` 指定断言检出漏Leave | 1（预期失败） | 43 / 0 bytes |

T2正向用例是：P1 B=(9,0)→(9,9)→(9,0)；P2 B=(6,8)；P3 A=(-1,0)、B=(-11,0)；P4固定B=(9,0)，A=(0,0)→(20,0)→(0,0)；P5含B后删除B；P6仅A；P7同一快照两次；P8同一A/B、R=10→8→10。除另注外A=(0,0)、R=10，每个场景从空副本开始。N1/N2分别拒绝R=0/-1，N3/N4拒绝B=(True,0)/(9.0,0)，N5拒绝缺A；五项均核已有previous/replica未变。没有另写正常AOI实现。

两个故障都只在仓外独立副本/适配层使用B=(9,0)→(9,9)：先核正常Enter，第二步再要求Leave且副本空。M1实际错误保留B并生成Move；M2每次仍计算fresh_current，却仅在格坐标改变时提交，因而错误保留旧B状态。只有上述指定断言抓到这些具体错误才记为预期失败，任意崩溃或非零退出不算通过。

合计32次固定输入，未扩测、未自动重跑。每个stdout/stderr均低于32KiB事后接受界；这个界不代表实时硬内存限制。源码、真实argv、解释器信息、每项正反例、未截断原流及hash已在本次整改的仓外执行记录中保全，正文保留主例全部源码和完整实际输出。后续复现仍先用第2节命令与上述固定输入，不把一次局部结果扩大为任意输入证明。

视野扇形与迷雾仍是第4节坐标和计数轨迹的手算核对，没有为了运行额外造实现；真实LOS、并发快照、丢包/乱序、跨场景生命周期、热点性能仍另行验收。本篇无需先做大规模压测才理解兴趣集合，也不能由这些有限用例推断工程容量。

## 8. 关联阅读

- [空间分区与索引](04-空间分区与索引.md)：正确候选生成、更新与查询实现
- [运行时AOI与Interest Management](../../07-网络与游戏服务端/状态复制与兴趣管理/05-AOI与InterestManagement.md)：事件进入服务器Tick、连接和发送流程；注意其自身证据范围
- [ReplicationGraph兴趣管理](../../07-网络与游戏服务端/状态复制与兴趣管理/05-ReplicationGraph兴趣管理.md)：UE特定复制路由与生命周期，不能由本例推断引擎已实测
- [随机数与洗牌算法](../随机采样与程序化生成/01-随机数与洗牌算法.md)：刷怪或掉落的随机选择，不决定出生对象何时进入权威世界
- [路径平滑与转向行为](../路径搜索与导航/04-路径平滑与转向行为.md)：运动采样与路径结果，和兴趣谓词分工不同
- [位运算与性能优化技巧](../数据结构与编码/05-位运算与性能优化技巧.md)：可用于迷雾位图，不能替代正确覆盖计数

旧稿提及的《Game Programming Gems 4》、云风《游戏之旅》及Bernier网络延迟论文属于后续阅读线索，本轮未核具体章页，不作为上述算法或性能断言的来源；书籍、原始笔记与附件均保留。
