---
type: Architecture
title: "01-Socket-Epoll与Reactor"
status: stable
verified: []
maturity: L3
---
# 01-Socket-Epoll与Reactor
> 验证与基准：按文中命令执行最小实验，记录结果与边界。

> 知识基线：POSIX socket、非阻塞 IO、`EAGAIN/EWOULDBLOCK`、Linux epoll（`epoll_create1/epoll_ctl/epoll_wait`）、LT/ET 语义；React 模式（事件循环 + 回调）。
> 版本基准：epoll 自 Linux 2.6；`epoll_create1`/`accept4` 需内核 ≥ 2.6.27/2.6.28；本机无 Linux 环境，实验代码已就绪但**未执行**（见 [evidence/labs/epoll-reactor](../../evidence/labs/epoll-reactor/README.md)）。
> 适用范围：Linux 游戏网关、登录服、DS 平台代理、聊天/推送长连接服务；UE DS 的 Linux 部署见 [05-UE Dedicated Server平台化/02-Linux DS部署与容器实战](<../../游戏服务端/05-UE Dedicated Server平台化/02-Linux DS部署与容器实战.md>)。
> 官方参考：[epoll(7) man page](https://man7.org/linux/man-pages/man7/epoll.7.html)、[epoll_ctl(2)](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html)、[accept4(2)](https://man7.org/linux/man-pages/man2/accept4.2.html)。
> 最后更新：2026-08-12（首版；实验待 Linux 执行后回填结果）。
> 知识成熟度：L3（可运行 Demo + 运行命令 + 预期输出，链接 Evidence；本机未执行，执行后按证据升级 L4）。

## 1. 概述

游戏服务器的长连接（网关、登录、聊天、DS 平台代理）几乎都在 Linux 上用 **epoll** 驱动：成千上万条连接由**一个线程**的 `epoll_wait` 分发事件，配合 Reactor 模式处理读写。本文回答：

1. 为什么不用阻塞 IO + 每连接一线程？
2. select/poll/epoll 的演进与 epoll 的内核机制；
3. LT 与 ET 的语义差异，为什么 ET 必须"读尽到 EAGAIN"？
4. partial read/write 与背压（backpressure）怎么处理？
5. Reactor 事件循环的正确骨架长什么样？

目标读者：自研网关/代理/匹配服的服务器工程师；前置：socket 基础与 [01-C++对象生命周期与RAII](../01-C++核心/01-C++对象生命周期与RAII.md)（连接对象管理）。

阅读顺序：先看 3.1~3.4 建立模型，再对照 4.1 的代码结构，最后用第 8 节的命令在 Linux 上复现 LT/ET 差异。

## 2. 核心概念

| 术语 | 中文 | 一句话定义 |
| --- | --- | --- |
| Blocking IO | 阻塞 IO | read/write 未就绪时线程挂起等待 |
| Non-blocking IO | 非阻塞 IO | 未就绪立即返回 `EAGAIN/EWOULDBLOCK` |
| `EAGAIN` | 重试指示 | 非阻塞读写"当前无数据/写不下"，稍后重试 |
| select/poll | 多路复用（旧） | 每次调用把 fd 集合在内核/用户态间拷贝扫描，O(n) |
| epoll | 事件就绪通知 | 内核维护就绪链表，只返回就绪 fd，O(就绪数) |
| LT | 水平触发 | 数据未读完就持续通知（默认） |
| ET | 边沿触发 | 只在状态变化（无→有）时通知一次 |
| Reactor | 反应器 | 事件循环 + 注册回调：就绪事件分发到处理函数 |
| Partial read/write | 部分读写 | 一次 read/write 只处理了部分数据 |
| Backpressure | 背压 | 对端慢/写不下时的流控：缓冲 + 高水位 + 丢弃/断连 |
| `EPOLLOUT` | 可写事件 | 发送缓冲可写时通知（配合写缓冲管理） |
| `EINTR` | 被信号中断 | `epoll_wait` 被信号打断，需继续循环 |
| `SO_REUSEPORT` | 端口复用 | 多进程/线程各自监听同一端口（内核负载均衡） |
| `EPOLLEXCLUSIVE` | 互斥唤醒 | 多线程共享 epoll fd 时避免惊群（内核 4.5+） |

## 3. 原理详解

### 3.1 系统调用为什么贵，多路复用解决什么

系统调用涉及用户态/内核态切换与上下文保存；每连接一线程的阻塞模型在几千连接时会耗尽线程资源（每线程栈 8MB 虚拟内存、调度开销、上下文切换）。多路复用的核心：

```text
阻塞每连接一线程：N 连接 → N 线程 → N 次切换
epoll 单线程：     N 连接 → 1 个 epoll_wait → 只处理就绪的 M 个 fd
```

select/poll 的缺陷：每次调用都全量扫描 fd 集合（O(n)），且 fd 集合在用户态/内核态间拷贝；epoll 用**内核就绪链表**只返回就绪 fd（O(就绪数)），并支持对 fd 的增删改（`epoll_ctl`），连接数上万时优势明显。

### 3.2 LT 与 ET 的语义

```text
LT（水平触发）：只要读缓冲还有数据，epoll_wait 就持续返回该 fd —— 处理"读一次"即可，漏读会再次通知。
ET（边沿触发）：只在"无数据 → 有数据"的状态跳变时通知一次 —— 必须把数据读到 EAGAIN，否则剩下的数据不再触发。
```

关键结论：

- LT 实现简单、不易丢事件，代价是**惊群/重复唤醒**（大流量下每次 epoll_wait 都返回同一 fd）；
- ET 减少无效唤醒、吞吐更好，代价是**必须循环读尽**，且任何一次漏读都是永久性丢数据；
- 实际工程：连接数大、消息突发（游戏网关）常用 ET + 读尽循环；低频小流量用 LT 更省心。

### 3.3 Reactor 骨架

```text
epoll_wait(events)
  → for each ready fd:
      listen fd  → accept 所有连接（ET 下循环 accept 到 EAGAIN），注册 EPOLLIN
      client fd  → 循环 read 到 EAGAIN：
                    解析消息 → 业务处理 → 写回（写缓冲 + EPOLLOUT 管理）
                  断开（read==0）→ 注销 fd、清理连接对象
```

（完整可运行代码见 [evidence/labs/epoll-reactor/src/echo_server_epoll.cpp](../../evidence/labs/epoll-reactor/src/echo_server_epoll.cpp)。）

### 3.4 Partial read/write 与背压

- **partial read**：一次 `read` 只读到部分消息（TCP 是字节流，无消息边界）——需要**应用层粘包/半包处理**（长度头 + 缓冲重组）；
- **partial write**：`write` 返回小于请求长度（发送缓冲满）——必须记录未写完的字节，注册 `EPOLLOUT`，可写事件到达后继续写；
- **背压**：对端读得慢 → 发送缓冲堆积 → 写缓冲超过高水位 → 策略：暂停该连接发送（反压到业务）、丢弃低优先级消息、或超时断连。游戏里"AOI 更新限流/降频"就是应用层背压的一种。

### 3.5 与游戏服务端/DS 平台的关系

- 登录/网关/聊天服务：epoll 单线程事件循环 + 业务线程池，是标准形态；
- UE DS 平台代理（分配、心跳、转发）：连接数大但消息小，LT/ET 均可，关键是超时与重连语义（见 [03-DS会话注册与重连实现](<../../游戏服务端/05-UE Dedicated Server平台化/03-DS会话注册与重连实现.md>)）；
- UE DS 自身的网络层是引擎实现（`UNetDriver`），Linux 部署只关心进程/端口/系统参数（见 [02-Linux DS部署与容器实战](<../../游戏服务端/05-UE Dedicated Server平台化/02-Linux DS部署与容器实战.md>)）——epoll 知识服务于**自研网关/代理/匹配服**等周边服务。

### 3.6 epoll 内核机制简述

epoll 在内核维护一张事件表（`epoll_ctl` 增删改），并只向用户态返回**就绪列表**：

```text
epoll_create1 → 创建 eventpoll 对象（红黑树 + 就绪链表）
epoll_ctl(ADD) → 把 fd 挂到红黑树，注册回调（就绪时进入就绪链表）
epoll_wait → 只拷贝就绪链表到用户态（O(就绪数)），非就绪 fd 不参与
```

LT 与 ET 在内核的差别：LT 在"数据未读完"时每次 `epoll_wait` 都返回该 fd（就绪状态持续）；ET 在返回一次后，只有**新的**数据到达（状态跳变）才再次加入就绪链表——所以 ET 用户态必须读尽。

工程含义：epoll 的复杂度与**就绪数**相关而非连接数，这使它成为十万连接场景的标准选择；但"注册/注销"（`epoll_ctl`）本身是系统调用，高频增删连接时也要控制频率（连接复用、池化）。

补充：`epoll_wait` 的 `maxevents` 数组一次性拷贝就绪事件；事件数超过数组大小时剩余就绪事件留在内核，下次 `epoll_wait` 继续返回——数组大小按峰值就绪数配置（常见 64~256）。

### 3.7 io_uring 与替代方案

Linux 5.1+ 的 io_uring 提供异步 IO（提交队列 + 完成队列，减少系统调用与拷贝）；对游戏服务端：

- 适合**高 IOPS 的读写密集型**（日志、存储、代理转发）；
- 对"连接管理 + 小消息"的网关场景，epoll 仍是主流（成熟、工具链完善、心智负担低）；
- 结论：先 epoll 跑通，瓶颈在系统调用开销时再评估 io_uring；不要为了新技术引入复杂度。

### 3.8 与并发/内存模型的边界

epoll 解决"哪些 fd 就绪"，线程模型解决"谁处理就绪事件"：

- 单线程 Reactor：无共享状态、无锁，但单核吞吐受限；
- Reactor + 业务线程池：事件循环无锁，线程池内部按任务隔离（`std::atomic` 计数 + 无锁队列，见 [04-并发与内存模型](../04-C++并发与内存模型/README.md)）；
- 多 Reactor（每线程一个 epoll fd + `SO_REUSEPORT`）：连接数分配均衡，但连接对象跨线程迁移时要明确所有权（见 [01-C++对象生命周期与RAII](../01-C++核心/01-C++对象生命周期与RAII.md)）。

选型原则：先单线程 Reactor + 线程池跑通，确需多核线性扩展再上多 Reactor；不要一开始就引入跨线程连接迁移。

### 3.9 常见错误模式速查

| 错误 | 后果 | 正确做法 |
| --- | --- | --- |
| 阻塞 fd 挂进 epoll | 一个慢连接卡死事件循环 | 所有 fd 设 `O_NONBLOCK` |
| ET 不读尽 | 永久漏读（数据丢失） | 循环 read 到 `EAGAIN` |
| 忽略 `EINTR` | 偶发空转/退出 | 循环重试 `epoll_wait` |
| 常驻 `EPOLLOUT` | 可写事件风暴（CPU 空转） | 只在写缓冲非空时注册 |
| 事件循环里做阻塞操作 | 全体连接延迟尖峰 | 投递线程池，结果回投 |
| 忽略 partial write | 数据丢失/错序 | 写缓冲 + `EPOLLOUT` 续写 |
| 不处理半包 | 消息错乱 | 应用层长度头 + 缓冲重组 |
| 连接对象不随 fd 清理 | 泄漏/悬垂回调 | RAII 连接对象 + 事件循环注销 |

## 4. 示例：最小 Echo Server（LT/ET 对照）

代码：`evidence/labs/epoll-reactor/src/echo_server_epoll.cpp`（`./echo_server_epoll <port> <lt|et>`）。

核心片段（节选）：

```cpp
// ET 关键：可读事件后必须循环读到 EAGAIN
for (;;) {
    ssize_t r = read(fd, buf.data(), buf.size());
    if (r > 0) { /* 处理并回显 */ if (edge) continue; break; }
    if (r == 0) { close(fd); break; }                      // 对端关闭
    if (errno == EAGAIN || errno == EWOULDBLOCK) break;    // 读尽（ET 必须）
}
```

### 4.1 代码逐段说明（配合证据库源码阅读）

`evidence/labs/epoll-reactor/src/echo_server_epoll.cpp` 的结构：

1. **listen fd 注册**：`epoll_create1(0)` + `EPOLLIN`（LT 默认）；ET 模式只需在注册事件上加 `EPOLLET`；
2. **accept 循环**：事件循环里 `accept4(..., SOCK_NONBLOCK)` 到 `EAGAIN`——ET 下漏 accept 会永久漏连接；
3. **读处理**：`read` 循环到 `EAGAIN`——ET 的"读尽"要求；LT 可读一次即返回（剩余数据会再触发）；
4. **回显**：`write` 全量写回（示意）；生产要处理 partial write + `EPOLLOUT`；
5. **断开**：`read == 0` → `epoll_ctl(DEL)` + `close`，并清理连接对象（RAII，关联 01 篇）。

复现时观察点：LT 下客户端一次发大块数据，服务端 `echo` 日志可能多次触发；ET 下每状态变化只触发一次（用 `strace` 或日志行数对比）。

补充：把 `SOCK_NONBLOCK` 去掉重跑同一用例，观察阻塞模式下 `epoll_wait` 挂起与单连接卡死全体的现象——这是"为什么必须非阻塞"的最直观演示。

## 5. 实验结果与结论

> 本机（Windows）无 Linux 环境，**实验待执行**；运行与断言见 [evidence/labs/epoll-reactor/README.md](../../evidence/labs/epoll-reactor/README.md)。

预期结论（执行后核对）：

1. LT 与 ET 都能正确回显（正确性一致）；
2. 客户端一次写入大块数据时，LT 产生多次 `epoll_wait` 唤醒，ET 只在状态变化时唤醒一次（吞吐差异）；
3. ET 若漏掉 EAGAIN 处理，出现"连接有数据却不再触发"的经典故障——这正是 ET 的工程代价。

（执行后把原始输出保存到 `evidence/labs/epoll-reactor/results/`，并按 Evidence 格式回填结论。）

### 5.1 如何用实验区分 LT/ET

用一个客户端连接连续发送两个包（间隔 10ms），观察服务端日志：

- LT：两个包都可能触发两次事件（或一次处理两个，取决于时序）——"还有数据"就继续通知；
- ET：连接从无到有只触发一次，处理循环必须把两个包都读完（日志显示一次事件处理多包）。

这是验收"ET 读尽"是否正确的最直观手段。

如果两个包被合并成一次 read（TCP 粘包），LT/ET 都能一次读完；区别只在"读完后是否还有数据"时的触发行为——ET 的差异来自"状态跳变"而非"数据量"。

## 6. 最佳实践

1. **默认 LT 起步**，确需高吞吐再切 ET；切 ET 必须配"读尽循环 + 严格 EAGAIN + 代码评审"。
2. **非阻塞 + 应用层缓冲**：所有 fd 设 `O_NONBLOCK`；读写都走缓冲，`EPOLLOUT` 管理写缓冲。
3. **消息边界**：TCP 字节流必须自定协议（长度头 + 半包重组），不能依赖 read 次数。
4. **背压三连**：写缓冲高水位 → 暂停/降频发送 → 超时断连；游戏 AOI 更新用"合并 + 限流"降低背压。
5. **accept 循环**：ET 下 listen fd 也要循环 accept 到 EAGAIN，否则慢连接下漏接受。
6. **单线程事件循环 + 业务线程池**：事件循环不做阻塞操作；耗时业务投递线程池，回调回到循环时注意线程安全。
7. **超时管理**：每连接记录最后活跃时间，定时扫描空闲连接（与 epoll 定时器/`timerfd` 结合）。
8. **监控**：连接数、就绪事件率、写缓冲水位、EAGAIN 频率——水位与 EAGAIN 是背压的先行指标。
9. **连接池/复用**：高频建连场景复用连接（HTTP/1.1 keep-alive、网关长连接），减少 `epoll_ctl` 与三次握手成本。
10. **惊群规避**：多线程共享 epoll fd 用 `EPOLLEXCLUSIVE`（内核 4.5+）或 `SO_REUSEPORT` 分 fd；单线程 Reactor 无此问题。
11. **信号与中断**：`epoll_wait` 被信号中断返回 `EINTR` 时继续循环；用 `signalfd` 把信号也纳入事件循环。
12. **协议先行**：消息长度头 + 版本号在联调前定死；半包/粘包处理写进框架层，业务不感知。
13. **压测带真实消息模式**：小消息高频 + 突发大包混合，别只用 echo 单包测吞吐。

## 7. FAQ

**Q1：epoll 一定比 select 快吗？**
连接数小（<100）时差距不大；连接数大或"就绪比例低"时 epoll 优势明显（O(就绪数) vs O(n)）。性能结论应在本机 Benchmark。

**Q2：ET 漏读真的会永久丢数据吗？**
会。ET 只在状态跳变时通知；如果处理函数没读尽，剩余数据不会再次触发，直到下一次新数据到达（且读尽逻辑若仍不处理旧数据，就持续滞后）。LT 不存在此问题。

**Q3：为什么游戏网关常用 ET？**
游戏消息突发性强（战斗广播、AOI 更新），LT 会让大量连接的读事件反复唤醒循环；ET + 读尽把"每连接每事件"压缩为"每状态变化一次"。

**Q4：`write` 返回部分字节怎么办？**
记录写偏移，把剩余数据挂到该连接的写缓冲，注册 `EPOLLOUT`；可写事件到达后继续写，写完注销 `EPOLLOUT`。

**Q5：Reactor 和 Proactor 什么区别？**
Reactor 通知"可读/可写"由应用自己读写；Proactor 由内核/框架完成读写再回调（如 IOCP）。游戏服务器两者都有，Reactor 在 Linux 更常见。

**Q6：Windows 上用什么替代 epoll？**
IOCP（完成端口，Proactor 形态）；UE 的网络层跨平台封装了这些差异，自研服务端按平台选型。

**Q7：epoll 的惊群怎么处理？**
多个线程同时 `epoll_wait` 同一 epoll fd 时，内核 4.5+ 默认 `EPOLLEXCLUSIVE`（互斥唤醒）或按需 `SO_REUSEPORT` 分 fd；单线程 Reactor 无惊群问题。

**Q8：`EPOLLOUT` 什么时候注册？**
只在写缓冲非空时注册（写完注销），避免一直触发；"常驻 EPOLLOUT"是经典浪费 CPU 的错误。

**Q9：连接数上万时每连接一个 `epoll_ctl` 太贵吗？**
注册是一次性成本；高频建连（每秒上千）才需要池化。真正的热点是每事件的数据拷贝与业务处理，不是 `epoll_ctl` 本身。

**Q10：游戏网关为什么还要业务线程池？**
单线程事件循环做"分发"，耗时的业务（登录鉴权、DB 查询、匹配计算）放线程池，结果通过队列回投事件循环——避免阻塞循环导致所有连接卡顿。

**Q11：`accept4` 和 `accept + fcntl` 有区别吗？**
`accept4` 在 accept 时直接设置 `SOCK_NONBLOCK`（一个系统调用）；老写法需要额外 `fcntl`，多一次调用且存在竞态窗口。内核 ≥ 2.6.28 优先 `accept4`。

**Q12：为什么"先注册 EPOLLIN 再处理业务"的顺序重要？**
如果先处理业务再注册事件，业务期间的到达数据不会触发通知（漏事件）；正确顺序：注册 → 事件循环 → 回调处理。这也是 Reactor 框架统一封装的原因。

**Q13：连接对象的内存归属谁负责？**
事件循环拥有连接对象生命周期（RAII 随 fd 注销销毁）；业务线程池只持有短期引用，通过"任务完成回投 + 弱引用/代际"避免悬垂——与 [01-C++对象生命周期与RAII](../01-C++核心/01-C++对象生命周期与RAII.md) 的 3.7 节一致。

**Q14：为什么推荐"单线程事件循环 + 每连接一个状态对象"而不是每连接一个协程？**
协程模型（如 `co_await` 读写）写起来直观，但调度、栈、超时与背压的可控性不如显式状态机；网关类服务用"事件循环 + 连接状态对象"更易审计与压测。协程在客户端 UI 层更有优势。

**Q15：生产环境为什么通常"多个 Reactor + 每个 Reactor 一个 epoll fd"？**
单 Reactor 受单核限制；多 Reactor 配合 `SO_REUSEPORT` 让内核把连接分发到各线程，每个线程独立 `epoll_wait`，互不阻塞。代价是连接与线程绑定，跨线程迁移（如负载再平衡）需要明确所有权转移协议。

**Q16：背压的最终手段是断连吗？**
是。缓冲有上限（内存保护），高水位 → 降频 → 丢弃低优先级 → 超时断连是标准阶梯；断连前必须让对端可恢复（协议层重连语义），否则断连即事故。

## 8. 验证与基准

- Linux 执行：`bash evidence/labs/epoll-reactor/scripts/build_run.sh 9000`（编译 + LT/ET 双模式回显验证）；
- 断言：回显字节一致、断开日志出现、ET 无漏读；
- 升级 L4 计划：并发连接压测（1K/10K 连接 × 消息吞吐 × P99）+ LT/ET 唤醒次数对比，原始数据入 `results/`；
- 标准依据：[epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html)、[epoll_ctl(2)](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html)。

升级 L4 的压测计划（Linux）：

```text
1. 连接规模：1K / 10K 连接（epoll 单线程）
2. 消息模式：小消息高频（游戏心跳）、突发大包（AOI 广播）
3. 指标：吞吐（msg/s）、P50/P95/P99 处理延迟、CPU
4. 对照：LT vs ET 的 epoll_wait 返回次数（strace -c）
5. 原始数据入 results/，按 Evidence 格式回填
```

验收清单（代码评审用）：

- [ ] 所有 fd 非阻塞；ET 模式有"读尽循环"；
- [ ] 写缓冲 + `EPOLLOUT` 管理，无 partial write 丢失；
- [ ] 半包/粘包由框架层处理（长度头）；
- [ ] 连接对象 RAII 管理，事件循环注销与清理成对；
- [ ] 事件循环内无阻塞 IO / 锁等待；
- [ ] 背压策略（高水位/降频/断连）明确并有监控。

## 9. 术语速查

| 术语 | 含义 |
| --- | --- |
| LT | Level Triggered：就绪态持续触发，事件循环需自行确认 |
| ET | Edge Triggered：仅状态变化沿触发，须循环读写到 EAGAIN |
| EAGAIN | 非阻塞 fd 无数据/缓冲满的返回码（ET 读取终止条件） |
| EPOLLOUT | 可写事件（只在写缓冲非空时注册，避免事件风暴） |
| SO_REUSEPORT | 多进程/线程各自监听同一端口的内核负载均衡 |

## 10. 关联阅读

- [Evidence · epoll-reactor](../../evidence/labs/epoll-reactor/README.md)：可运行代码与预期输出。
- [07-Linux系统编程 README](README.md)：本分类导航。
- [游戏服务端/01-架构与网络](../../游戏服务端/01-架构与网络/README.md)：游戏服务器网络层对 IO 模型的工程使用。
- [05-UE Dedicated Server平台化/02-Linux DS部署与容器实战](<../../游戏服务端/05-UE Dedicated Server平台化/02-Linux DS部署与容器实战.md>)：DS 的 Linux 部署边界。
- [02-Atomic与C++内存模型](../04-C++并发与内存模型/02-Atomic与C++内存模型.md)：事件循环与业务线程池之间的同步语义。
- [01-C++对象生命周期与RAII](../01-C++核心/01-C++对象生命周期与RAII.md)：连接对象的 RAII 管理。
- `07-Linux系统编程/02-Linux信号与进程管理`（规划）：信号与守护进程。
- [04-C++并发与内存模型](../04-C++并发与内存模型/README.md)：线程池与无锁队列的底层。
- 升级 L4 说明：实验在 Linux 执行并回填原始数据后，将本文成熟度从 L3 升为 L4。
