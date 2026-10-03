---
type: Evidence
title: "Evidence · epoll-reactor：LT/ET、部分写与背压契约"
status: stable
verified: []
maturity: L0
updated: 2026-10-03
sources:
  - resource: https://man7.org/linux/man-pages/man7/epoll.7.html
  - resource: https://man7.org/linux/man-pages/man2/epoll_ctl.2.html
  - resource: https://man7.org/linux/man-pages/man2/send.2.html
---
# Evidence · epoll-reactor：LT/ET、部分写与背压契约

> 状态：**已执行**（2026-10-03，Linux x86_64）；7 个 C++ 机制测试 + 20 个 Python/TCP 集成测试通过。不是吞吐 Benchmark，也不是 UE/Windows 验收。

## 问题与假设

1. ET 收到一次事件却只读一部分，是否会丢掉内核缓冲里的数据？假设：应用可能停滞，字节仍能直接读取；LT 对残留可读数据继续通知。
2. 注册前已有数据是否一定漏事件？假设：LT/ET 的 ADD 均可看到已有就绪状态。
3. 部分写、EAGAIN、慢读与半关闭能否保留完整字节流？假设：固定容量队列、发送偏移、EPOLLOUT 续写和 EOF 后排空能完成本实验输入。
4. 高水位暂停读取后，ET 如何恢复？假设：可写回调清空到低水位后主动继续读取，避免等不到新边沿。

## 环境

原始记录见 [results/validation.log](results/validation.log)：Linux 6.18.44 x86_64、g++ 14.2.0、Python 3.12.14、Bash 5.2.37、GNU timeout 9.7；编译参数为 `-O2 -g -std=c++17 -Wall -Wextra -Wpedantic -Werror`。

API 使用 Linux 专有 epoll、accept4、非阻塞 socketpair；`epoll_create1` 和 `accept4` 的 API 下限分别是 Linux 2.6.27/2.6.28，未实测最低内核版本。需要 g++（C++17）、Python 3、Bash 与 GNU coreutils，无第三方 Python 包。

## 文件与运行方式

- [src/echo_server_epoll.cpp](src/echo_server_epoll.cpp)：真实 loopback TCP Reactor；原入口保留
- [src/reactor_io.hpp](src/reactor_io.hpp)：RAII fd、固定容量环形输出队列、带偏移续写
- [tests/epoll_contract_test.cpp](tests/epoll_contract_test.cpp)：单进程 socketpair 机制断言
- [tests/tcp_integration_test.py](tests/tcp_integration_test.py)：启动服务，等待 READY/PAUSE，再做 TCP 断言
- [scripts/build_run.sh](scripts/build_run.sh)：临时目录编译、测试、记录环境与原始输出，退出后清理二进制

从仓库根执行，或在任意工作目录传入脚本绝对路径：

```bash
bash evidence/labs/epoll-reactor/scripts/build_run.sh
# 可选固定端口，兼容旧调用；默认0由内核选空闲端口
bash evidence/labs/epoll-reactor/scripts/build_run.sh 9000
# 复核时不要覆盖提交的原始记录
LOG=/tmp/epoll-review.log bash evidence/labs/epoll-reactor/scripts/build_run.sh
```

可用 `CXX`、`PYTHON` 指定本地工具。脚本对编译与两组测试分别设超时；`set -euo pipefail` 保留失败状态，不把 timeout 当成功。Python 的 socket/条件变量/线程退出也有时限，服务在 finally 中清理，不依赖固定 sleep 等待启动。原始输出中的端口、临时路径、部分写计数受运行环境影响，不要求逐字一致。

手动探索（另开终端运行客户端，最后 Ctrl+C 停止服务）：

```bash
g++ -O2 -std=c++17 -Wall -Wextra -Wpedantic -Werror \
  evidence/labs/epoll-reactor/src/echo_server_epoll.cpp -o /tmp/echo_server_epoll
/tmp/echo_server_epoll 9000 lt
# 或 /tmp/echo_server_epoll 9000 et
```

服务只监听 `127.0.0.1`；不要把实验改为公网监听后直接部署。

## 输入、断言与指标

输入在测试文件中程序化构造，无外部 data 文件或随机种子。

| 测试组 | 输入/设置 | 断言 |
| --- | --- | --- |
| LT 残留 / ET 停滞 | socketpair 写 `abcdef`，只读 `a`，没有新写入 | LT 再次可读；本受控 ET 序列没有第二次事件，FIONREAD=5，直接读仍得 `bcdef` |
| ADD 前就绪 ×2 | 先写 `already-ready`，再注册 LT/ET | 两种模式都报告并读回原字节 |
| 写阻塞/续写 | 524605 字节，SO_SNDBUF=4096，128KiB 环形队列 | 实际出现部分写和 EAGAIN；对端 drain 后 EPOLLOUT 续写，零丢失/重复/乱序，覆盖环绕 |
| 队列上限 | 容量4，追加第5字节 | 拒绝追加且原队列不变 |
| 对端关闭 | socketpair 读端已关 | MSG_NOSIGNAL 防止进程被 SIGPIPE 杀死，失败发送不消费待发字节 |
| 非法 CLI ×6 | 缺参数、非数值、负数、越界、错模式、多参数 | 退出码2且有诊断 |
| TCP 二进制 ×2 | 196745 字节，1/17/4095/4096/8193/313 等块交替写 | LT/ET 返回完全一致的含零字节流 |
| TCP 半关闭 ×4 | 空输入；327680 字节后 SHUT_WR、先不读并等 PAUSE | 空流 EOF；待发尾部完整收到后才 EOF |
| TCP 慢读 ×2 | 2097223 字节；收到 PAUSE 才读；期间另连一个客户端 | 队列达到上限、其他客户端可服务，恢复后字节完整 |
| TCP reset / 并发 ×4 | 连续8次 RST 后再探测；4并发连接 | 服务仍存活；每连接数据一致 |
| 退出与统计 ×2 | SIGTERM 停服 | 正常退出；观察到读暂停、写 EAGAIN，max_pending=262144 |

总计 7 个机制用例 + 20 个集成用例 = **27**。指标是正确性断言、队列占用和实际经过的错误/续写分支，不是时延、吞吐或公平性的上界。

## 原始结果与结论

[完整日志](results/validation.log) 包含编译命令、27 个 PASS、LT/ET 服务输出与退出状态。该次两种模式均观察到 `read_pauses>0`、`max_pending=262144`；部分写与 EAGAIN 次数不属于稳定接口。

- 受控反例支持“ET 未读尽可能停滞，但残留字节没有因 epoll 消失”，推翻旧文“永久性丢数据”的表述。
- LT/ET 注册前已有数据仍可读；就绪通知不是只记录注册之后发生的消息。
- 本实现两种模式均读尽（或在队列满时暂停并主动恢复），不能把模式切换当成性能提升证据。
- 真正的数据丢失风险来自旧示例忽略部分 write 返回值，或 EOF 时直接丢弃输出队列。本批以共享输出队列与 TCP 回显断言覆盖这两条路径。

## 局限与工程边界

- 每连接输出缓冲固定256KiB，最多64条活动连接；超过连接数上限会拒绝额外连接。输出内存的名义上界16MiB不包括 socket 内核缓冲、容器与日志开销；未压测连接上限拒绝路径。
- 为可重复触发背压，服务主动将 SO_SNDBUF 请求设为16384；Linux 可调整/加倍实际值，不能把这个值当实际缓存或调优建议。
- 无每轮 I/O 公平预算、空闲/慢连接超时、TLS、鉴权、长度头解析、工作线程池。慢读用例的另一连接成功仅是一个案例，不证明任意持续洪泛下的公平性。
- SIGTERM/SIGINT 退出会关闭尚存连接，不是全服优雅排空协议；单连接 EOF 排空与服务器退出是两套语义。
- 测试没有注入信号风暴、EMFILE/ENOMEM、真实弱网或 1K/10K 连接，没有生产 P99、Windows、UE、GPU 或最低内核版本数据。错误检查代码存在不等于所有错误路径都已运行。
- socketpair 机制与真实 TCP 分层验证；ET 无第二次事件断言仅针对“单生产者预先写完、消费一部分”的受控序列，不推广为任意连接只通知一次。
- 本 README 保留 L0、正文保留 L3 和空 verified；新增有限运行证据不自动抬高成熟度。

## 关联知识文档

- [01-Socket-Epoll与Reactor](../../../知识/01-编程与计算机基础/操作系统与系统I-O/01-Socket-Epoll与Reactor.md)：唯一主责正文
- [02-Linux DS部署与容器实战](<../../../知识/08-工程实践与质量/部署运维与可观测性/02-Linux%20DS部署与容器实战.md>)：部署边界，未在本实验运行 UE DS
