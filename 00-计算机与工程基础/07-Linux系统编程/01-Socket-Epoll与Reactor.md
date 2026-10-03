---
type: Architecture
title: "01-Socket-Epoll与Reactor"
status: stable
verified: []
maturity: L3
updated: 2026-10-03
sources:
  - resource: https://man7.org/linux/man-pages/man7/epoll.7.html
  - resource: https://man7.org/linux/man-pages/man2/epoll_ctl.2.html
  - resource: https://man7.org/linux/man-pages/man2/accept.2.html
  - resource: https://man7.org/linux/man-pages/man2/send.2.html
  - resource: https://man7.org/linux/man-pages/man2/recv.2.html
---
# 01-Socket-Epoll与Reactor
> 验证与基准：Linux 机制测试与 loopback TCP 测试见第 5、8 节；测试证明本实验契约，不代表生产容量。

> 知识基线：POSIX socket、非阻塞 IO、`EAGAIN/EWOULDBLOCK`、Linux epoll（`epoll_create1/epoll_ctl/epoll_wait`）、LT/ET 语义；Reactor 模式（事件循环 + 回调）。
> 版本基准：epoll 首见 Linux 2.5.44；本实验使用 `epoll_create1`/`accept4`，API 下限分别为 2.6.27/2.6.28，实际验证版本以 [Evidence](../../evidence/labs/epoll-reactor/README.md) 为准，不代表在最低版本实测。
> 适用范围：Linux 游戏网关、登录服、DS 平台代理、聊天/推送长连接服务；UE DS 的 Linux 部署见 [05-UE Dedicated Server平台化/02-Linux DS部署与容器实战](<../../游戏服务端/05-UE Dedicated Server平台化/02-Linux DS部署与容器实战.md>)。
> 官方参考：[epoll(7) man page](https://man7.org/linux/man-pages/man7/epoll.7.html)、[epoll_ctl(2)](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html)、[accept4(2)](https://man7.org/linux/man-pages/man2/accept.2.html)。
> 最后更新：2026-10-03（核对 Linux man-pages，修正 ET/惊群/注册时序，补有界写缓冲与运行证据）。
> 知识成熟度：L3（保留现级；Linux 小规模契约实验不等同生产压测、UE 验收或跨平台验证）。

## 1. 概述

Linux 长连接服务（网关、登录、聊天、DS 平台代理）可以用 **epoll** 驱动：一个 Reactor 线程用 `epoll_wait` 分发多条连接的就绪事件，也可扩展为多 Reactor。本文回答：

1. 为什么不用阻塞 IO + 每连接一线程？
2. select/poll/epoll 的演进与 epoll 的内核机制；
3. LT 与 ET 的语义差异，为什么 ET 要读尽或保存待续状态？
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
| epoll | 事件就绪通知 | 维护关注集合与就绪列表，避免每轮由用户态全量提交 fd |
| LT | 水平触发 | 数据未读完就持续通知（默认） |
| ET | 边沿触发 | 通知 I/O 状态变化；未读尽不能依赖后续重复通知 |
| Reactor | 反应器 | 事件循环 + 注册回调：就绪事件分发到处理函数 |
| Partial read/write | 部分读写 | 一次 read/write 只处理了部分数据 |
| Backpressure | 背压 | 对端慢/写不下时的流控：缓冲 + 高水位 + 丢弃/断连 |
| `EPOLLOUT` | 可写事件 | 发送缓冲可写时通知（配合写缓冲管理） |
| `EINTR` | 被信号中断 | `epoll_wait` 被信号打断，需继续循环 |
| `SO_REUSEPORT` | 端口复用 | 多进程/线程各自监听同一端口（内核负载均衡） |
| `EPOLLEXCLUSIVE` | 排他唤醒标志 | 多个 epoll 实例监视同一目标 fd 时减少唤醒；需显式 ADD（内核 4.5+） |

## 3. 原理详解

### 3.1 系统调用为什么贵，多路复用解决什么

系统调用涉及用户态/内核态切换与上下文保存；每连接一线程的阻塞模型会增加栈虚拟地址空间、调度与切换成本（默认栈大小受平台、运行时和资源限制影响，并非固定 8MB，也不等于已驻留物理内存）。多路复用的核心：

```text
阻塞每连接一线程：N 连接 → N 线程 → 栈与调度成本随线程数增加
epoll 单线程：     N 连接 → 1 个 epoll_wait → 只处理就绪的 M 个 fd
```

select/poll 的缺陷：每次调用都全量扫描 fd 集合（O(n)），且 fd 集合在用户态/内核态间拷贝；epoll 维护**就绪列表**，并支持对 fd 的增删改（`epoll_ctl`）；大量空闲连接时通常有优势，实际成本还包括内核就绪检查、系统调用和业务处理。

### 3.2 LT 与 ET 的语义

```text
LT：仍然可读时可再次报告；可以分批读，也可以读到 EAGAIN。
ET：收到事件后记住“仍可能就绪”；在读到 EAGAIN/EOF/错误前，不可只等新事件。
```

- ET 没读完通常是**应用停滞**：内核缓冲仍可能保留字节，不是 epoll 把数据删除。即使后来有新事件，也不能依赖它保证进展。
- 不要把 ET 简化为“空→非空只通知一次”。多次到达可能产生多次事件，事件也可能合并；`epoll_wait` 次数、TCP 段数、应用消息数互不等价。
- LT 也可以循环读尽；把“LT 每次读一次”和“ET 每次读尽”的程序直接对比，会混入处理策略差异，不能得出 ET 必然更快。
- 最小实验选择读到 `EAGAIN/EWOULDBLOCK`，遇 `EINTR` 重试，遇 EOF/错误转状态。生产中若用字节/时间预算保证公平，必须在用户态就绪队列保留未完成连接，轮转续处理，不能预算耗尽后盲等新的 ET。

依据：[epoll(7) 的 LT/ET 与 starvation 说明](https://man7.org/linux/man-pages/man7/epoll.7.html)。

### 3.3 Reactor 骨架

```text
epoll_wait(events)
  → for each ready fd:
      listen fd  → accept 所有连接（ET 下循环 accept 到 EAGAIN），注册 EPOLLIN
      client fd  → 循环 read 到 EAGAIN：
                    解析消息 → 业务处理 → 写回（写缓冲 + EPOLLOUT 管理）
                  读 EOF（read==0）→ 停止读；输出排空后再注销 fd、清理连接对象
```

（完整可运行代码见 [evidence/labs/epoll-reactor/src/echo_server_epoll.cpp](../../evidence/labs/epoll-reactor/src/echo_server_epoll.cpp)。）

### 3.4 Partial read/write 与背压

- **partial read**：一次 `read` 只读到部分消息（TCP 是字节流，无消息边界）——需要**应用层粘包/半包处理**（长度头 + 缓冲重组）；
- **partial write**：`write` 返回小于请求长度（发送缓冲满）——必须记录未写完的字节，注册 `EPOLLOUT`，可写事件到达后继续写；
- **背压**：对端读得慢 → 发送缓冲堆积 → 写缓冲超过高水位 → 策略：暂停上游生产/接收（反压到业务）、丢弃低优先级消息、或超时断连。游戏里"AOI 更新限流/降频"就是应用层背压的一种。

### 3.5 与游戏服务端/DS 平台的关系

- 登录/网关/聊天服务：epoll 单线程事件循环 + 业务线程池，是标准形态；
- UE DS 平台代理（分配、心跳、转发）：连接数大但消息小，LT/ET 均可，关键是超时与重连语义（见 [03-DS会话注册与重连实现](<../../游戏服务端/05-UE Dedicated Server平台化/03-DS会话注册与重连实现.md>)）；
- UE DS 自身的网络层是引擎实现（`UNetDriver`），Linux 部署只关心进程/端口/系统参数（见 [02-Linux DS部署与容器实战](<../../游戏服务端/05-UE Dedicated Server平台化/02-Linux DS部署与容器实战.md>)）——epoll 知识服务于**自研网关/代理/匹配服**等周边服务。

### 3.6 epoll 内核机制简述

epoll 在内核维护一张事件表（`epoll_ctl` 增删改），并只向用户态返回**就绪列表**：

```text
epoll_create1 → 创建 eventpoll 对象（红黑树 + 就绪链表）
epoll_ctl(ADD) → 把 fd 挂到红黑树，注册回调（就绪时进入就绪链表）
epoll_wait → 从就绪列表检查并返回事件，受 maxevents 限制
```

这里的树/链表是理解实现的模型，不是应用可依赖的稳定 ABI。LT 会继续报告就绪；ET 不保证对残留数据重复报告。不要从内核数据结构推导“一包一事件”或绝对时间复杂度承诺。

工程含义：事件分发避免每轮扫描全部连接，但吞吐仍受活跃比例、缓冲、CPU 与处理策略影响；但"注册/注销"（`epoll_ctl`）本身是系统调用，高频增删连接时也要控制频率（连接复用、池化）。

补充：`epoll_wait` 的 `maxevents` 数组一次性拷贝就绪事件；事件数超过数组大小时剩余就绪事件留在内核，下次 `epoll_wait` 继续返回——数组大小按峰值就绪数配置（常见 64~256）。

### 3.7 io_uring 与替代方案

Linux 5.1+ 的 io_uring 提供异步 IO（提交队列 + 完成队列，可批量提交；不意味着普通操作自动零拷贝）；对游戏服务端：

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
| ET 不读尽又直接等待 | 缓冲里仍有字节但应用停滞 | 读到 `EAGAIN`，或保存用户态就绪状态续处理 |
| 忽略 `EINTR` | 偶发空转/退出 | 循环重试 `epoll_wait` |
| LT 常驻 `EPOLLOUT` | 无待发数据仍反复唤醒 | 本实验仅输出未清空时注册；ET 常驻是另一种需自行记录可写状态的设计 |
| 事件循环里做阻塞操作 | 全体连接延迟尖峰 | 投递线程池，结果回投 |
| 忽略 partial write | 数据丢失/错序 | 写缓冲 + `EPOLLOUT` 续写 |
| 不处理半包 | 消息错乱 | 应用层长度头 + 缓冲重组 |
| 连接对象不随 fd 清理 | 泄漏/悬垂回调 | RAII 连接对象 + 事件循环注销 |

## 4. 示例：有界 Echo Reactor（LT/ET 对照）

代码入口仍为 [echo_server_epoll.cpp](../../evidence/labs/epoll-reactor/src/echo_server_epoll.cpp)，公共 I/O 契约在 [reactor_io.hpp](../../evidence/labs/epoll-reactor/src/reactor_io.hpp)。CLI 为 `echo_server_epoll <port> <lt|et>`；只绑定 `127.0.0.1`，`port=0` 由内核选空闲端口。

### 4.1 从旧示意到可验证的状态机

旧版只对每次 read 调用一次 write，会在部分写时丢掉尾部；现在每条连接保存有界输出队列、发送偏移和读端 EOF 状态。主链路是：

```text
读就绪 → recv → 追加输出 → 立即尝试 send
                     ↓ EAGAIN / 只发送一部分
                保留未发送字节，监听 EPOLLOUT
                     ↓ 输出到高水位
                暂停读取（不再无限分配）
                     ↓ 输出下降到低水位
                恢复读取，并主动继续 drain（ET 不能只盼新边沿）
读 EOF → 禁止后续读 → 排空输出 → 注销/关闭
```

1. `socket`/`accept4` 显式设置非阻塞和 close-on-exec；监听与连接按选择注册 LT/ET。
2. 对 `EINTR` 重试；对 `EAGAIN/EWOULDBLOCK` 保存状态，回事件循环等待；致命错误明确处理。
3. `send(..., MSG_NOSIGNAL)` 避免对端关闭时 SIGPIPE 终止整个进程，但仍检查 `EPIPE` 等返回值。
4. EOF 只说明对端不再发送。不能因 `EPOLLRDHUP` 或 `recv==0` 就丢弃已经排队的回显；错误复位则可能无法交付尾部。
5. `EPOLLIN`、`EPOLLOUT`、`EPOLLRDHUP` 是可同时出现的位，不是互斥事件种类；HUP 也不证明接收缓冲已空。

来源：[send(2)](https://man7.org/linux/man-pages/man2/send.2.html)、[recv(2)](https://man7.org/linux/man-pages/man2/recv.2.html)、[epoll_ctl(2)](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html)。

### 4.2 背压案例：慢玩家不能让网关内存无限增长

战斗广播生产速度超过客户端消费速度时，仅注册 `EPOLLOUT` 不会限制内存。本实验用固定容量输出队列和高低水位暂停/恢复读，保留已接受字节的顺序；游戏网关还需把压力传回业务生产者，并区分可合并的 AOI 状态与不可随意丢弃的交易消息。

暂停读会逐步影响 TCP 接收窗口，但内核缓冲不等于应用队列，传播也不是立即完成。ET 恢复读取必须显式继续 I/O 或可靠重置关注状态；否则读缓冲早已可读却没有新的边沿，连接仍会停住。

此 Demo 是**字节流回显**，不是长度头协议解码器，只做最多64连接的粗上限，没有空闲超时、TLS、线程池或生产公平调度。慢连接长期不读可能占住连接资源；上线前仍需配合全局预算与超时。

## 5. 实验结果与结论

运行说明、完整断言和 Linux 原始日志见 [Evidence README](../../evidence/labs/epoll-reactor/README.md)。本批分开验证两层：

| 层 | 方法 | 能支持的结论 |
| --- | --- | --- |
| 机制契约 | 单进程 pipe/socketpair，先写入已知数据再 epoll；有限超时 | LT 残留就绪、ET 停滞反例但数据仍在、注册前已有数据、写阻塞后的续写 |
| 集成链路 | 独立 server + Python loopback TCP 客户端，LT/ET 各跑 | 多块/二进制字节一致、半关闭仍收到尾部、慢读续传、断开后服务可继续工作 |

这里没有 1K/10K 连接容量数据、CPU 优化结论或 P99 指标，也不宣称 Windows/UE/GPU 实际验收。保留 L3 和空 `verified`，不因测试通过自动升级整章成熟度。

### 5.1 为什么不用“隔 10ms 发两个包”验收 ET

TCP 可拆分/合并写入，进程调度又改变何时进入 `epoll_wait`，因此不能用一次/两次唤醒预测数据是否完整。机制测试固定“写完后读取一部分、没有后续写入”的序列，检查就绪与残留字节；TCP 测试只断言完整字节流与最终 EOF，不断言收发系统调用次数。

## 6. 最佳实践

1. **默认 LT 起步**，测量表明 ET 有益时再采用；读到 EAGAIN/EOF/错误，或显式保存待续状态并保证继续调度。
2. **非阻塞 + 应用层缓冲**：网络 fd 设 `O_NONBLOCK`；读写都有边界，`EPOLLOUT` 管理未发送字节。
3. **消息边界**：TCP 字节流必须自定协议（长度头 + 半包重组），不能依赖 read 次数。
4. **背压方向**：高水位时限制上游生产/新消息入队，必要时暂停读取；已有输出继续按可写能力发送。游戏 AOI 可按语义合并/限流，超时断连按协议决定。
5. **accept 循环**：ET 下 listen fd 也要循环 accept 到 EAGAIN，否则慢连接下漏接受。
6. **单线程事件循环 + 业务线程池**：事件循环不做阻塞操作；耗时业务投递线程池，回调回到循环时注意线程安全。
7. **超时管理**：每连接记录最后活跃时间，定时扫描空闲连接（与 epoll 定时器/`timerfd` 结合）。
8. **监控**：连接数、就绪事件率、输出水位与排队时长；区分读/写 EAGAIN。正常读尽也会 EAGAIN，写端 EAGAIN 联合持续高水位才支持发送压力判断。
9. **连接池/复用**：高频建连场景复用连接（HTTP/1.1 keep-alive、网关长连接），减少 `epoll_ctl` 与三次握手成本。
10. **惊群规避**：先区分共享同一 epoll 实例与多个实例监视同一监听 fd。`EPOLLEXCLUSIVE` 是后者的显式 ADD 选项，并非默认开关；`SO_REUSEPORT` 则是独立监听 socket 的负载分配。
11. **信号与中断**：`epoll_wait` 被信号中断返回 `EINTR` 时继续循环；用 `signalfd` 把信号也纳入事件循环。
12. **协议先行**：消息长度头 + 版本号在联调前定死；半包/粘包处理写进框架层，业务不感知。
13. **压测带真实消息模式**：小消息高频 + 突发大包混合，别只用 echo 单包测吞吐。

## 7. FAQ

**Q1：epoll 一定比 select 快吗？**
不一定。连接规模、空闲比例、注册/MOD 频率与每事件业务成本共同决定收益。固定连接数阈值不能代替目标负载下的 profile/benchmark。

**Q2：ET 漏读真的会永久丢数据吗？**
不是“epoll 删除数据”。残留字节仍可能留在内核，直接等待可能无限停滞。真正的数据丢失还可能来自忽略部分写、主动关闭或缓冲溢出；本实验分别检查残留读取与完整回显。

**Q3：为什么游戏网关常用 ET？**
ET 可减少某些重复就绪通知，但收益依赖负载和处理策略；LT 也能读尽。应比较相同缓冲、批处理、公平预算和消息分布下的 CPU/P99，不能只凭模式名称决定。

**Q4：`write` 返回部分字节怎么办？**
记录写偏移，把剩余数据挂到该连接的写缓冲，注册 `EPOLLOUT`；可写事件到达后继续写，写完注销 `EPOLLOUT`。

**Q5：Reactor 和 Proactor 什么区别？**
Reactor 通知"可读/可写"由应用自己读写；Proactor 由内核/框架完成读写再回调（如 IOCP）。游戏服务器两者都有，Reactor 在 Linux 更常见。

**Q6：Windows 上用什么替代 epoll？**
IOCP（完成端口，Proactor 形态）；UE 的网络层跨平台封装了这些差异，自研服务端按平台选型。

**Q7：epoll 的惊群怎么处理？**
`EPOLLEXCLUSIVE`（Linux 4.5+）要在 `EPOLL_CTL_ADD` 显式指定，针对多个 epoll 实例附着同一目标 fd，保证唤醒一个或多个排他实例，并不保证恰好一个。它不能通过 MOD 设置，标志组合受限。多个线程等待同一实例的 ET 唤醒行为另见 epoll(7)，不能混成“默认 EXCLUSIVE”。

**Q8：`EPOLLOUT` 什么时候注册？**
本实验在输出未清空时注册，写空后撤销。LT 常驻会反复通知可写；ET 也可采用常驻 IN|OUT 并自行记录可写状态的设计，但每次新输出都必须尝试发送，不能等一个可能不会再来的事件。

**Q9：连接数上万时每连接一个 `epoll_ctl` 太贵吗？**
要实测连接 churn、ADD/DEL/MOD 频率与业务占比。长连接的初始注册可摊销，但频繁切换关注事件仍有成本；不要预先断言热点一定不在 epoll_ctl，也没有通用的池化阈值。

**Q10：游戏网关为什么还要业务线程池？**
单线程事件循环做"分发"，耗时的业务（登录鉴权、DB 查询、匹配计算）放线程池，结果通过队列回投事件循环——避免阻塞循环导致所有连接卡顿。

**Q11：`accept4` 和 `accept + fcntl` 有区别吗？**
`accept4` 在 accept 时直接设置 `SOCK_NONBLOCK`（一个系统调用）；老写法需要额外 `fcntl`，多一次调用且存在竞态窗口。内核 ≥ 2.6.28 优先 `accept4`。

**Q12：为什么"先注册 EPOLLIN 再处理业务"的顺序重要？**
应尽快注册以免人为延迟，但“注册前到达的数据必然漏掉”不成立：ADD 可以发现已有就绪状态，本实验直接覆盖这个反例。真正要管理的是注册失败、并发读者消耗就绪状态与连接生命周期，而非把事件当作历史消息队列。

**Q13：连接对象的内存归属谁负责？**
事件循环拥有连接对象生命周期（RAII 随 fd 注销销毁）；业务线程池只持有短期引用，通过"任务完成回投 + 弱引用/代际"避免悬垂——与 [01-C++对象生命周期与RAII](../01-C++核心/01-C++对象生命周期与RAII.md) 的 3.7 节一致。

**Q14：为什么推荐"单线程事件循环 + 每连接一个状态对象"而不是每连接一个协程？**
本实验用显式状态对象便于看清缓冲与状态转换。协程也能封装同样的非阻塞状态机；是否便于超时、取消、背压和生命周期审计取决于框架，不是语法本身决定。

**Q15：生产环境为什么通常"多个 Reactor + 每个 Reactor 一个 epoll fd"？**
单 Reactor 受单核限制；多 Reactor 配合 `SO_REUSEPORT` 让内核把连接分发到各线程，每个线程独立 `epoll_wait`，互不阻塞。代价是连接与线程绑定，跨线程迁移（如负载再平衡）需要明确所有权转移协议。

**Q16：背压的最终手段是断连吗？**
可以是兜底之一，不是固定的必经阶梯。先限制全局/单连接资源，向上游传播压力，再按消息语义决定合并、拒绝或超时断连；涉及交易的消息不能任意丢弃，恢复需要幂等和重连协议。

## 8. 验证与基准

- Linux 执行：`bash evidence/labs/epoll-reactor/scripts/build_run.sh`（C++ 编译 + 机制断言 + LT/ET TCP 集成）；
- 断言：按机制测试和 TCP 用例逐项检查；有超时、失败退出码和原始输出，不用 `|| true` 吞掉测试失败；
- 后续可选容量验证（本批未执行）：并发连接压测（1K/10K 连接 × 消息吞吐 × P99）+ LT/ET 唤醒次数对比，原始数据入 `results/`；
- 标准依据：[epoll(7)](https://man7.org/linux/man-pages/man7/epoll.7.html)、[epoll_ctl(2)](https://man7.org/linux/man-pages/man2/epoll_ctl.2.html)。

生产容量压测建议（Linux，不能用本批小实验替代）：

```text
1. 连接规模：1K / 10K 连接（epoll 单线程）
2. 消息模式：小消息高频（游戏心跳）、突发大包（AOI 广播）
3. 指标：吞吐（msg/s）、P50/P95/P99 处理延迟、CPU
4. 对照：LT vs ET 的 epoll_wait 返回次数（strace -c）
5. 原始数据入 results/，按 Evidence 格式回填
```

验收清单（代码评审用）：

- [ ] 网络 fd 非阻塞；ET 读到 EAGAIN/EOF/错误，或保存待续状态并保证继续调度；
- [ ] 写缓冲 + `EPOLLOUT` 管理，无 partial write 丢失；
- [ ] 半包/粘包由框架层处理（长度头）；
- [ ] 连接对象 RAII 管理，事件循环注销与清理成对；
- [ ] 事件循环内无阻塞 IO / 锁等待；
- [ ] 背压策略（高水位/降频/断连）明确并有监控。

## 9. 术语速查

| 术语 | 含义 |
| --- | --- |
| LT | Level Triggered：就绪态持续触发，事件循环需自行确认 |
| ET | Edge Triggered：不能依赖残留就绪重复通知；读写到 EAGAIN 或保存待续状态 |
| EAGAIN | 非阻塞 fd 无数据/缓冲满的返回码（ET 读取终止条件） |
| EPOLLOUT | 可写事件（本实验按写缓冲需求注册；LT 空队列常驻会反复唤醒） |
| SO_REUSEPORT | 多进程/线程各自监听同一端口的内核负载均衡 |

## 10. 关联阅读

- [Evidence · epoll-reactor](../../evidence/labs/epoll-reactor/README.md)：可运行代码、契约断言与原始输出。
- [07-Linux系统编程 README](README.md)：本分类导航。
- [游戏服务端/01-架构与网络](../../游戏服务端/01-架构与网络/README.md)：游戏服务器网络层对 IO 模型的工程使用。
- [05-UE Dedicated Server平台化/02-Linux DS部署与容器实战](<../../游戏服务端/05-UE Dedicated Server平台化/02-Linux DS部署与容器实战.md>)：DS 的 Linux 部署边界。
- [02-Atomic与C++内存模型](../04-C++并发与内存模型/02-Atomic与C++内存模型.md)：事件循环与业务线程池之间的同步语义。
- [01-C++对象生命周期与RAII](../01-C++核心/01-C++对象生命周期与RAII.md)：连接对象的 RAII 管理。
- `07-Linux系统编程/02-Linux信号与进程管理`（规划）：信号与守护进程。
- [04-C++并发与内存模型](../04-C++并发与内存模型/README.md)：线程池与无锁队列的底层。
- 成熟度边界：新增 Linux 运行证据只覆盖声明过的契约；生产容量、复杂并发与跨平台能力仍需各自验证。
