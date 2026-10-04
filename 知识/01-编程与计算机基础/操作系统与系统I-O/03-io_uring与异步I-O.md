---
type: Mechanism
title: "io_uring 与异步 I/O"
status: stable
verified: []
maturity: L2
---
> 知识基线：以当前标准、协议或工具链版本为准，平台差异需实测。
> 最后更新：2026-08-20。
> 官方参考：https://man7.org/。
> 验证与基准：按文中命令执行最小实验并记录指标。
# io_uring 与异步 I/O

> 知识成熟度：L2
> 知识基线：当前标准与工具链版本，平台差异需验证。
> 最后更新：2026-08-20。
> 官方参考：https://kernel.dk/io_uring.pdf
> 验证与基准：按文中步骤执行实验并记录指标。
来源：https://kernel.dk/io_uring.pdf｜验证与基准：liburing 示例、epoll 对照压测。

## 模型
io_uring 通过共享提交队列（SQ）与完成队列（CQ）减少 syscall 和拷贝。应用准备 SQE，内核执行后写入 CQE；用户消费 CQE 并回收资源。

## 初始化
- `io_uring_queue_init` 创建环与 mmap 区域。
- SQ/CQ 可独立大小，CQ 通常需要额外余量。
- `IORING_SETUP_SQPOLL` 由内核线程轮询，适合高频提交但消耗 CPU。
- `IORING_SETUP_COOP_TASKRUN` 调整完成回调的执行时机。

## 常用操作
- `IORING_OP_READ/WRITE` 文件 I/O。
- `IORING_OP_RECV/SEND` socket 收发。
- `IORING_OP_ACCEPT` 异步接入。
- `IORING_OP_CONNECT` 异步连接。
- `IORING_OP_TIMEOUT` 超时控制。
- `IORING_OP_LINK_TIMEOUT` 将超时与主操作绑定。
- `IORING_OP_OPENAT`、`CLOSE`、`FSYNC` 管理生命周期。

## 生命周期
1. 从 SQ ring 取空闲 SQE。
2. 填入 opcode、fd、buffer、len、user_data。
3. `io_uring_submit` 发布请求。
4. 通过 peek/wait 获取 CQE。
5. 检查 `cqe->res`，负值是 `-errno`。
6. `io_uring_cqe_seen` 释放 CQ 槽位。

## 工程陷阱
- buffer 在完成前必须保持有效且不可移动。
- fixed buffer/fd 减少注册开销，但资源需要显式注销。
- partial read/write 仍需循环处理。
- cancel 只表示取消请求，不保证业务副作用回滚。
- 多线程共享 ring 要明确所有权与唤醒策略。
- 过大的队列隐藏背压并增加内存占用。

## 性能
- 批量提交降低 syscall 次数。
- batch completion 降低锁竞争。
- SQPOLL 适合稳定高负载，低负载可能不划算。
- 注册文件描述符和缓冲区减少 fdget 与 pin 成本。
- 以 p50/p99、CPU cycles/op、队列深度评估收益。

## 网络服务器模式
- accept 请求完成后立即准备 recv。
- 每个连接维护 user_data 状态机。
- recv 完成触发解析，再提交 send。
- 发送完成后决定继续发送或关闭。
- 所有路径设置超时、取消和异常关闭。

## 诊断
- 检查内核版本、liburing 版本与 opcode 支持。
- 统计 SQE 提交、CQE 完成和 inflight 数。
- 对比 epoll + blocking worker 的端到端 p99。
- strace 只看初始化，业务路径应观察 ring 指标。

## 关联
- [Socket、Epoll 与 Reactor](01-Socket-Epoll与Reactor.md)
- [文件系统与存储栈](03-文件系统与存储栈.md)


## 实验与 Benchmark

建立基线、控制变量、预热并重复至少 30 次，记录中位数、p95/p99、错误率和资源占用。保存源码、编译器、内核、硬件与原始日志。

### 实验流程

1. 定义问题与假设。
2. 构造最小样例并验证正确性。
3. 使用 perf、strace、bpftrace 或对应分析器采集证据。
4. 每次只改变一个变量，测试对照组。
5. 检查异常路径、资源上限和恢复行为。
6. 输出表格、分布和结论边界。

### 指标解释

吞吐是单位时间完成量；IOPS 是请求数；CPU/请求体现软件开销；p99 体现长尾；错误率和重试率决定真实可用性。

| 项目 | 基线 | 优化 | 证据 |
|---|---:|---:|---|
| 端到端延迟 |  |  | p50/p99 |
| 吞吐 |  |  | ops/s |
| CPU |  |  | cycles/op |
| 内存 |  |  | RSS/显存 |
| 错误 |  |  | errno/重试 |

### 常见陷阱

- 首次运行包含加载、分配和 JIT 成本。
- 异步程序未等待同步点，计时提前结束。
- 编译器删除无可观察副作用的基准代码。
- CPU 频率、SMT、NUMA 和后台任务造成噪声。
- 只测峰值吞吐，不测尾延迟、功耗和错误恢复。
- 优化改变数值精度、ABI 或安全边界，却未回归。

### CI 验收

将 Benchmark 纳入性能回归套件，设定相对基线阈值（例如 p99 退化不超过 10%）。超阈值时保留 perf 数据和环境信息并执行 bisect；不同硬件只比较相对变化。
## 最小实验

### 环境

Linux 5.19+，liburing 2.3+，`gcc`、`fio`、`perf`。先确认内核支持：

```bash
uname -r
grep io_uring_setup /proc/kallsyms | head
pkg-config --modversion liburing
```

### 读文件程序流程

1. `io_uring_queue_init` 创建 ring。
2. `io_uring_get_sqe` 获取 SQE。
3. `io_uring_prep_read` 填充 fd、buffer、offset。
4. `io_uring_submit` 提交请求。
5. `io_uring_wait_cqe` 等待 CQE 并检查 `cqe->res`。
6. `io_uring_cqe_seen` 释放 CQE。

负数 `res` 是 `-errno`，不能直接当作字节数。取消和超时必须消费对应 CQE。

### COOP_TASKRUN 语义

`IORING_SETUP_COOP_TASKRUN` 不表示“执行完成回调”。它允许内核在协作时机运行 task_work，通常由提交线程在进入/离开内核或显式任务运行路径触发，以减少异步 IPI。CQE 仍需由用户线程收割；不能假设回调在提交线程之外自动执行。

### Benchmark

```bash
fio --name=uring --ioengine=io_uring --iodepth=1,8,32 \
 --rw=randread --bs=4k --size=1G --direct=1 --runtime=30 --time_based
perf stat -e cycles,instructions,context-switches \
 ./uring-read /tmp/testfile
```

|配置|队列深度|固定 buffer|SQPOLL|指标|
|---|---:|---|---|---|
|基线|1|否|否|延迟 p99|
|并发|8|否|否|IOPS、CPU|
|固定资源|32|是|否|系统调用数|
|轮询|32|是|是|p99、功耗|

每组预热 10 秒、采样 30 秒、重复 5 次。失败信号：EOPNOTSUPP、CQE 丢失、p99 飙升。回滚：关闭 SQPOLL、降低队列深度、释放注册资源。

## 排障清单

- `-EINVAL`：检查 SQE 字段、offset、buffer 生命周期。
- `-EBADF`：确认 fd 未被提前关闭。
- CQE 不返回：确认已 submit，检查 ring fd 和信号屏蔽。
- CPU 过高：比较 SQPOLL 与阻塞等待，检查 busy poll。
- 内存泄漏：确保每个 CQE 都 `cqe_seen`，并在退出前 queue_exit。

## 参考

- https://kernel.dk/io_uring.pdf
- https://man7.org/linux/man-pages/man7/io_uring.7.html
- https://github.com/axboe/liburing
- https://docs.kernel.org/userspace-api/io_uring.html

## 深入实验清单

|实验|参数|观测|失败信号|
|---|---|---|---|
|固定 buffer|REGISTER_BUFFERS|CPU、IOPS|EINVAL|
|固定文件|REGISTER_FILES|提交开销|EBADF|
|链式请求|IOSQE_IO_LINK|CQE 顺序|链断|
|超时取消|TIMEOUT、ASYNC_CANCEL|完成码|残留请求|
|多 ring|每线程 ring|扩展性|锁争用|

每项实验保存 `strace`、`perf stat`、fio JSON 和内核版本。CQE 的 `res`、`user_data` 必须逐项核对，不能只统计 submit 数。

回滚：关闭高级 flags，改用基础 poll/等待；检测到内核 bug 时降级到 blocking I/O，并保留正确性测试。

门禁：所有异步路径有退出清理；取消后不访问已释放 buffer；信号处理不在 CQE 回调中执行阻塞操作。

## 可运行的 liburing 最小程序

下面程序只完成一次异步读取，适合先验证工具链和 CQE 错误语义。它不依赖固定缓冲区或 SQPOLL，失败时可以直接退回同步读取。

```c
#include <liburing.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/etc/hostname";
    struct io_uring ring;
    char buf[4096];
    int ret = io_uring_queue_init(8, &ring, 0);
    if (ret < 0) { fprintf(stderr, "queue_init: %s\n", strerror(-ret)); return 1; }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror("open"); io_uring_queue_exit(&ring); return 1; }
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (!sqe) { fprintf(stderr, "no SQE\n"); close(fd); io_uring_queue_exit(&ring); return 1; }
    io_uring_prep_read(sqe, fd, buf, sizeof(buf) - 1, 0);
    io_uring_sqe_set_data(sqe, buf);
    ret = io_uring_submit(&ring);
    if (ret < 0) { fprintf(stderr, "submit: %s\n", strerror(-ret)); close(fd); io_uring_queue_exit(&ring); return 1; }
    struct io_uring_cqe *cqe = NULL;
    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0) { fprintf(stderr, "wait_cqe: %s\n", strerror(-ret)); close(fd); io_uring_queue_exit(&ring); return 1; }
    if (cqe->res < 0) fprintf(stderr, "read: %s\n", strerror(-cqe->res));
    else { buf[cqe->res] = '\0'; printf("bytes=%d data=%s", cqe->res, buf); }
    io_uring_cqe_seen(&ring, cqe);
    close(fd);
    io_uring_queue_exit(&ring);
    return cqe->res < 0;
}
```

编译与执行：

```bash
sudo apt-get install -y liburing-dev build-essential
cc -O2 -Wall -Wextra uring-read.c -luring -o uring-read
printf 'hello io_uring\\n' >/tmp/uring-input
./uring-read /tmp/uring-input
# 预期：bytes=16 data=hello io_uring
```

验收条件是程序退出码为 0、输出字节数等于文件实际长度，并且 `strace -e io_uring_setup,io_uring_enter ./uring-read` 能看到一次 setup 与一次 enter。不要把 `cqe->res` 直接转成 `size_t`，负 errno 会造成巨大的无符号数。

## 超时、取消与资源生命周期

超时有两种常用模型。独立 `IORING_OP_TIMEOUT` 用于驱动事件循环心跳；`IORING_OP_LINK_TIMEOUT` 与读写请求链接，主请求完成后超时请求自动失效。链接请求的 CQE 仍要逐个收割，不能只等待第一个。

```c
struct __kernel_timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
struct io_uring_sqe *read_sqe = io_uring_get_sqe(&ring);
io_uring_prep_read(read_sqe, fd, buf, len, off);
read_sqe->flags |= IOSQE_IO_LINK;
struct io_uring_sqe *to_sqe = io_uring_get_sqe(&ring);
io_uring_prep_link_timeout(to_sqe, &ts, 0);
```

收到客户端断开、关闭或 deadline 事件时，使用 `IORING_OP_ASYNC_CANCEL`，`addr` 指向原请求的 `user_data`。取消是竞态操作：如果请求已完成，取消可能返回 `-ENOENT`；如果取消成功，原请求通常以 `-ECANCELED` 完成。无论哪种结果，都必须等待并消费原请求的 CQE，再释放 buffer、fd 和连接对象。

建议为每个请求维护 `SUBMITTED → COMPLETED/CANCELED → RECLAIMED` 状态，并用 generation 或引用计数防止迟到 CQE 访问已复用的 `user_data`。进程退出前按“停止接收新请求、取消 inflight、收割 CQE、关闭 fd、queue_exit”的顺序清理。

## 背压与并发控制

SQ 深度不是吞吐越大越好。应用应设置 `max_inflight`，当 `submitted - completed` 达到上限时停止读取上游或返回显式背压错误。网络服务可按连接、租户和全局三个维度设置配额，避免单个慢客户端占满 ring。

推荐策略：

1. 正常水位达到 70% 时暂停生产者；
2. 降到 50% 以下再恢复提交；
3. 超过 deadline 的请求优先取消；
4. 对 `-EAGAIN`、`-ENOBUFS` 做有界重试并记录原因；
5. 监控 SQ 空闲槽、CQ 占用率、inflight、取消率和最大等待时间。

不要在 CQE 消费线程中执行无限时长业务逻辑，否则 CQ 会堆积。生产者和消费者可以分线程，但每个 ring 的所有权、唤醒机制和 shutdown 协议必须写入设计文档；共享 ring 只有在锁开销可接受且确有收益时采用。

## 同步、epoll 与 io_uring 对照 Benchmark

比较时固定文件大小、块大小、缓存策略、并发数、CPU 亲和性和 NUMA 节点。至少测试三组：同步 `pread`、epoll + worker、io_uring（SQPOLL 开关各一组）。对页缓存读取和 `O_DIRECT` 读取分别报告，不能混为一个结论。

```bash
taskset -c 2 ./sync-read --file test.bin --bs 4096 --requests 100000
taskset -c 2 ./epoll-worker --connections 64 --requests 100000
taskset -c 2 ./uring-read-bench --depth 1,8,32 --requests 100000
perf stat -r 5 -e cycles,instructions,syscalls:sys_enter_io_uring_enter,context-switches \
  ./uring-read-bench --depth 32 --requests 100000
```

记录以下字段，并保留每次运行的 JSON/CSV 原始数据：

|实现|深度|吞吐 ops/s|p50 us|p95 us|p99 us|CPU%|syscall/op|错误率|
|---|---:|---:|---:|---:|---:|---:|---:|---:|
|pread|1||||||||
|epoll+worker|64||||||||
|io_uring|1||||||||
|io_uring|32||||||||

预期趋势不是固定结论：低并发、小请求、页缓存命中时同步调用可能延迟最低；高并发或需要批量提交时 io_uring 通常减少 syscall。SQPOLL 可能降低 p99，但会持续消耗一个 CPU。只有在同一硬件和相同工作集上重复 5 次，且 p99 退化不超过 10% 才接受优化。

## 内核与 liburing 版本矩阵

|内核|liburing|应验证能力|风险/限制|
|---|---|---|---|
|5.4 LTS|2.0|基础 read/write、timeout|特性较少，部分 socket opcode 不可用|
|5.10 LTS|2.1|accept、connect、poll|检查 fixed resource 与取消语义|
|5.15 LTS|2.2|buffer/file registration|发行版 backport 可能改变行为|
|5.19+|2.3+|multishot、linked timeout、更多网络操作|需验证发行版配置与权限|
|6.1 LTS|2.4+|较新 task_work、send/recv 特性|不同 CPU 的 SQPOLL 成本不同|

启动时打印 `uname -r`、`io_uring_major_version()`、`io_uring_opcode_supported()` 结果，并将能力探测作为测试前置条件。遇到 `-EOPNOTSUPP` 时降级到 epoll 或阻塞 I/O，不应通过版本字符串盲目假设能力。

## 故障排查与安全检查

- `io_uring_queue_init` 返回 `-ENOMEM`：检查 `RLIMIT_MEMLOCK`、队列深度和 cgroup memory；降低深度后重试。
- `io_uring_submit` 返回 `-EBUSY`：检查 SQPOLL 内核线程是否被 CPU 配额限制，暂时关闭 SQPOLL。
- `recv` 完成 `0`：表示对端有序关闭，不是“没有数据”，应进入连接回收状态。
- `-EINTR`：不要无界重试；记录信号来源并遵循请求 deadline。
- CQ 占满：优先增加消费频率或降低生产速率，不能只扩大 CQ 而掩盖泄漏。
- 结果偶发错乱：检查 buffer 是否被复用、`user_data` 是否指向栈变量、是否存在 data race。
- 权限问题：检查 seccomp 是否允许 `io_uring_setup`、`io_uring_enter`，以及容器的 capability 与 `/proc/sys/kernel/io_uring_disabled`。

生产环境应限制注册 buffer 的大小和数量，避免 pin 住过多物理内存；对不可信输入设置每请求长度上限。监控 `-ECANCELED`、`-ETIME`、`-EPIPE`、`-ENOBUFS` 的比例，并把取消原因区分为客户端断开、服务端 deadline 和进程 shutdown。

## 实验记录模板

每次实验目录至少包含：`README.md`（目标、假设、环境）、源码、`uname.txt`、`pkg-config.txt`、编译命令、`perf.stat`、fio JSON、原始延迟样本和结论。记录日期、CPU 型号、内存、磁盘型号、挂载选项、后台负载及 CPU governor。

结论必须回答三个问题：收益来自减少 syscall、增加并发还是改变缓存命中？在何种负载下收益消失？失败时是否可以无损降级？若不能回答，实验只能标记为“探索性”，不可作为生产容量承诺。

---

## 关联知识与工程落地

- **前置依赖**：
  - [01-Socket-Epoll与Reactor](01-Socket-Epoll与Reactor.md)：反应器模式与 I/O 多路复用。
  - [06-操作系统/03-文件系统与存储栈](03-文件系统与存储栈.md)：Linux 存储栈与直接 I/O。
- **性能与体系结构**：
  - [08-计算机体系结构与性能/01-处理器存储层次与性能工程](../硬件体系结构与性能/01-处理器存储层次与性能工程.md)：内存屏障与共享环无锁访问。
  - [03-性能工具：插桩与perf采样](../../08-工程实践与质量/调试与性能分析/03-性能工具：插桩与perf采样.md)：高吞吐异步系统调用性能采样。
- **游戏服务端落地**：
  - [游戏服务端/01-架构与网络/05-并发与高性能](../../07-网络与游戏服务端/运行调度与过载保护/05-并发与高性能.md)：服务器高吞吐持久化与网络前沿。
- **分类与领域入口**：
  - [07-Linux系统编程 README](../../../00-计算机与工程基础/07-Linux系统编程/README.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/domains/计算机与工程基础.md)
