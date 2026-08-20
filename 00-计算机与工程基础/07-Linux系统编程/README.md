---
type: Index
title: "07-Linux系统编程 · 分类"
status: stable
verified: []
maturity: L0
---
# 07-Linux系统编程 · 分类

> 定位：Linux 下的网络与系统编程基础（socket、epoll、IO 多路复用、信号、性能工具），服务端自研网关/代理/匹配服的底层支撑。

## 文件列表

| 文件 | 简介 | 成熟度 |
| --- | --- | --- |
| [01-Socket-Epoll与Reactor](01-Socket-Epoll与Reactor.md) | 阻塞/非阻塞、select/poll/epoll、LT/ET、Reactor、partial read/write、背压；实验代码就绪待 Linux 执行 | L3 |
| [03-性能工具：插桩与perf采样](<03-性能工具：插桩与perf采样.md>) | 耗时点测试方法：C++ 插桩（steady_clock/RAII/编译器插桩）与 Linux perf 采样（record/report/火焰图/关键指标）；服务端场景统计模式与容量衔接 | L2 |
| [03-io_uring与异步I-O](03-io_uring与异步I-O.md) | 共享队列异步 I/O、网络状态机、批量提交与诊断 | L2 |

## 规划

- `02-Linux信号与进程管理`（规划）：信号、daemon、systemd 集成；
- `04-Linux诊断工具`（规划）：strace/ss/netstat 等（与 08 体系结构篇互补；perf 已落地为 03 篇）。

## 与 UE/服务端的对接

- 自研网关/登录服/匹配服/DS 平台代理：本分类是底层基础；
- UE DS 的 Linux 部署参数（端口、连接、SIGTERM）见 [游戏服务端/05-UE Dedicated Server平台化](<../../游戏服务端/05-UE Dedicated Server平台化/README.md>)。
- [03-io_uring与异步I-O](03-io_uring与异步I-O.md)：提交队列、完成队列、零拷贝与工程模式。
