---
type: Evidence
title: "Evidence · epoll-reactor：epoll LT/ET + Reactor 最小实现"
status: stable
verified: []
maturity: L0
---
# Evidence · epoll-reactor：epoll LT/ET + Reactor 最小实现

> 状态：**待执行**（代码已就绪；epoll 为 Linux 专有 API，本机 Windows 环境未运行）

## 问题

1. LT（水平触发）与 ET（边沿触发）在可读事件处理上的差异（ET 必须循环读到 EAGAIN）；
2. 非阻塞 IO + partial read 处理；
3. 单线程 Reactor 事件循环结构（listen → accept → 读 → 回显）。

## 假设

- LT 模式下一次 `epoll_wait` 事件处理一次 read 即可（剩余数据会再次触发）；ET 模式必须读到 `EAGAIN`，否则漏读。
- 非阻塞 fd + `EAGAIN` 是"本轮读完"的唯一正确判断。
- 生产服务器需要写缓冲 + `EPOLLOUT` 处理部分写与背压（本实验仅回显示意，代码注释已标注）。

## 环境（目标）

- Linux（内核 ≥ 2.6.27，`epoll_create1`/`accept4`）；g++ ≥ 9，`-O2 -std=c++17`

## 运行方式（Linux）

```bash
bash scripts/build_run.sh 9000
# 或手动：
g++ -O2 -std=c++17 src/echo_server_epoll.cpp -o build/echo_server_epoll
./build/echo_server_epoll 9000 lt      # LT 模式
./build/echo_server_epoll 9000 et      # ET 模式
printf 'hello\n' | nc 127.0.0.1 9000
```

## 预期输出（断言）

```text
echo server: port=9000 mode=LT pid=<pid>
+ conn fd=5
echo fd=5 8 bytes (wrote 8)
```

- 客户端发送任意文本，服务端原样回显；`echo` 行字节数 = 发送字节数。
- ET 模式差异观察：客户端一次写入大块数据（> 接收缓冲）时，LT 会产生多次 `epoll_wait` 唤醒；ET 只在状态变化时唤醒一次，由循环读处理全部数据。

## 指标

- 正确性：回显字节一致；断开时 `- close fd=` 出现；无 EAGAIN 死循环（CPU 占用正常）。
- 扩展建议：并发连接数 × 回显吞吐（与 W1-11 正文的 backpressure/限流结合）。

## 结论（待执行后回填）

- 预期结论：ET 在高吞吐下减少无效唤醒，但要求"读尽"循环与严格的 EAGAIN 处理；LT 更简单、不易漏事件，适合低连接数/低频场景。游戏网关连接数高、消息突发时常用 ET + 写缓冲。

## 局限

- 本实验未包含 EPOLLOUT/写缓冲、定时器、线程池；仅演示 LT/ET 与 Reactor 骨架。
- 未执行的实验不构成 L4 证据；正文按 L3（可运行 Demo + 运行命令 + 预期输出）标注。

## 关联知识文档

- [01-Socket-Epoll与Reactor](../../../00-计算机与工程基础/07-Linux系统编程/01-Socket-Epoll与Reactor.md)
- [游戏服务端/05-UE Dedicated Server平台化/02-Linux DS部署与容器实战.md](<../../../游戏服务端/05-UE Dedicated Server平台化/02-Linux DS部署与容器实战.md>)
