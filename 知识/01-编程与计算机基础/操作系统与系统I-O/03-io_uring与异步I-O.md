---
type: Mechanism
title: "io_uring 与异步 I/O"
status: stable
verified: []
maturity: L2
description: "以固定版本源码解释 io_uring 的发布、完成、取消与资源寿命，并给出未经编译运行的一次普通文件读取示例。"
updated: 2026-10-06
sources:
  - id: liburing-2.3
    title: "liburing 2.3 headers, implementation and manuals"
    resource: https://github.com/axboe/liburing/tree/4915f2af869876d892a1f591ee2c21be21c6fc5c
  - id: linux-6.1
    title: "Linux 6.1 io_uring implementation"
    resource: https://github.com/torvalds/linux/tree/830b3c68c1fb1e9176028d02ef86f3cf76aa2476/io_uring
  - id: fio-3.32
    title: "fio 3.32 HOWTO"
    resource: https://github.com/axboe/fio/blob/db7fc8d864dc4fb607a0379333a0db60431bd649/HOWTO.rst
  - id: man-pages-5.13
    title: "Linux man-pages 5.13 historical maintainer source"
    resource: https://github.com/mkerrisk/man-pages/tree/091fbf1fef4808f0ccfe0ff8c333aedf833b8782/man2
---

# io_uring 与异步 I/O

> 知识成熟度：L2

本文解决一个具体问题：应用把读写交给内核以后，凭什么判断它已结束，又到哪一步才能复用内存？主线限定 Linux、普通 single-shot 请求、无 CQE skip；完整例子是独立单线程进程，对调用者提供的受控普通文件只读一次，显式 offset=0，最多 4095 字节。链、取消、注册资源及网络部分解释扩展时必须增加的协议，不构成完整服务器实现。

知识基线是上述固定 liburing 2.3、Linux 6.1、fio 3.32 和 man-pages 5.13 的实际源文件，具体符号见版本与来源节。2026-10-06 的工作是原始资料和代码控制流静态核对，主要承诺保持 L2，`verified: []` 不变。未编译 C，未运行任何 fixture、io_uring setup/probe/请求、取消竞态、fixed/multishot/SQPOLL、性能工具或 Windows 实验。已有依赖观察仅表明 `pkg-config liburing` 未找到包、默认 `/usr/include` 与 `/usr/local/include` 未找到头文件，不能推出所有自定义目录都无库；本轮没有安装依赖。文中命令是后续验证入口，表中 `PAPER_EXPECTED` 是纸面预期。

## 模型

同步 `pread` 把“发出请求并等待结果”放在一次调用里。io_uring 允许先准备多个操作，再按完成事件推进应用；它改变提交和等待的组织方式，不取消错误、短 I/O 和对象寿命的责任。

共享 SQ（Submission Queue）和 CQ（Completion Queue）传递的是请求、完成的**描述信息**。普通 READ/WRITE/RECV/SEND 不因此自动获得 payload 零拷贝。应用通过 liburing 填写 SQE，发布共享 SQ tail；内核消费 SQE 后执行操作，最终发布 CQE；应用读取结果并推进 CQ head。批量可以摊销提交/等待成本，但收益和锁竞争要测量。

五个边界应分别记录：准备了 SQE → 发布到共享 SQ → 内核消费 → 目标操作终结 → 应用消费 CQE。发布不证明消费，消费不证明完成，CQ 槽归还也不证明应用连接对象已无引用。特别是 SQPOLL 下，`io_uring_submit` 的数量不能当作内核已经消费或完成的数量。[liburing 发布与提交实现](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/src/queue.c#L200-L231)

## 初始化

`io_uring_queue_init(entries, &ring, flags)` 成功返回 0，建立内核实例并映射环；失败返回负 errno。仅初始化成功的 ring 才交给 `io_uring_queue_exit`。需要能力位或指定 CQ 大小时，用零初始化的 `io_uring_params` 配合 `io_uring_queue_init_params`，读取实际返回的 SQ/CQ 容量与 `features`，不凭请求的 entries 推定最终容量。

SQ 容量约束描述项的排队空间，CQ 容量承接尚未收割的完成；两者不等于 payload 数或可支持的连接数。控制请求、multishot 和通知可能产生额外 CQE，所以 CQ 需要按峰值完成速率及最大消费停顿估算余量。扩大 CQ 不能代替背压。

- `IORING_SETUP_SQPOLL` 创建提交轮询线程。空闲达到配置条件时它可休眠，并设置 NEED_WAKEUP，由后续提交路径唤醒；不能概括成永远占满一个核，也不能保证无需 syscall。它与面向完成的 IOPOLL 不同
- `IORING_SETUP_COOP_TASKRUN` 改变内核 task_work 的协作处理方式，减少强制中断的需要；不是替应用调用业务 callback。应用仍需取得 CQE、解释结果并调度业务。多线程提交/等待和只 peek 的模式要同时核对相关 taskrun 约束
- flags、权限、内核配置和操作对象不匹配时初始化或请求可能失败；版本号和源码中有符号都不能代替实际验证

上述语义来自固定版 [setup 手册的 SQPOLL、COOP 与 feature 说明](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/man/io_uring_setup.2)。本文基础例 flags=0，不使用这些优化模式。

## 常用操作

| 目的 | 常用 opcode | 选择时需明确的条件 |
| --- | --- | --- |
| 文件读写 | READ/WRITE；分散聚集用 READV/WRITEV | 显式 offset、长度、短 I/O；普通操作不会自动使用 fixed buffer |
| socket 数据 | RECV/SEND | stream/datagram、正长度/零长度、协议消息边界、部分收发 |
| 建连和接入 | CONNECT/ACCEPT | 成功结果含义不同；ACCEPT 的成功结果是新 fd，需要自己的所有者 |
| 定时和截止时间 | TIMEOUT/LINK_TIMEOUT | 独立计时或完成计数条件，与专门关联某个目标的取消尝试不同 |
| 文件生命周期 | OPENAT/CLOSE/FSYNC | 打开/关闭对象的所有权；写完成不等于持久化，FSYNC 也有独立结果 |
| 控制 | ASYNC_CANCEL | 匹配条件、取消自身 token 与目标 token、目标是否已终结 |

opcode 只是请求种类。是否支持某种 flag、fixed file、文件系统或 socket 模式仍需分层验证。非向量 READ/WRITE 不能被误写成 Linux 5.4 的通用基础能力；版本事实见后文。[固定版 enter 操作说明](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/man/io_uring_enter.2)

## 生命周期

对普通 single-shot、没有 skip-success 的一个请求，应用可以按以下步骤推理：

1. `io_uring_get_sqe` 可能返回 NULL；拿到槽后填操作字段，并且**每次明确设置 user_data**，get/prep 不负责清空这个字段
2. 通过唯一 SQ 写者或外部同步保护准备与发布。submit 返回值描述提交阶段；一般批量可能部分提交，必须保留已消费与未消费尾部的实际状态，不能重新构造整个业务批次造成重复 I/O
3. `io_uring_wait_cqe` 返回 0 只表示取得 CQE；等待错误与操作错误分开。通过 token 找到请求；`cqe->res` 才是本次操作结果，负值为负 errno，不是线程当前 `errno`
4. 在 `io_uring_cqe_seen` **之前复制**后续要用的 `res`、`user_data`、`flags`。seen 推进 CQ head 后槽可被复用；queue_exit 还会解除映射。这是两种不同的失效原因，此后只能使用副本
5. 识别目标 terminal，结束应用处理及其他引用，再回收关联资源。主目标、timeout、cancel 各自有账，不能把一个控制 CQE 当作目标完成

| 对象 | 寿命与回收依据 |
| --- | --- |
| SQE 槽 | 发布后不能任意改写；内核消费并归还 SQ 空间后，按 liburing 分配协议复用，不必等该 I/O 完成 |
| iovec、timespec 等结构参数 | 依具体 opcode 和 SUBMIT_STABLE 合同；存在该 feature 时相关状态在内核消费 SQE 后稳定，不是“调用过 submit 即可释放” |
| read/write payload | 保持有效、地址稳定且没有冲突访问，直到目标操作的实际寿命结束；SUBMIT_STABLE 不把 payload 寿命缩短到消费 SQE |
| 请求/连接上下文 | token 不自动延长对象寿命；直到目标终结及所有应用/控制处理引用结束，generation 防止迟到结果命中复用对象 |
| CQE 槽及指针 | 只在应用仍持有该槽且映射有效时读；字段复制后 seen，后续不用该指针 |
| 注册资源引用 | 按注册、更新、注销及在途使用的规则管理；应用对象的释放仍需自己的协议 |

计数例外必须显式建模：multishot 每个 CQE 都看 `IORING_CQE_F_MORE`，没有 MORE 才表示该请求结束，不能按“收到一个 CQE”减一个 inflight。SEND_ZC 首 CQE 即便 `res<0` 也要看 MORE；若 MORE 指示后续通知，相关 buffer 必须留到 NOTIF 通知，通知只控制 buffer 寿命，不证明对端已收到数据。`IOSQE_CQE_SKIP_SUCCESS` 可能省略完成通知，必须另有可证明的完成协议，本文所有示例均排除它。CQE 数、目标终结数、控制请求数分别统计。[get_sqe](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/man/io_uring_get_sqe.3)、[CQ head 更新](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/src/include/liburing.h#L286-L308)、[MORE/NOTIF 合同](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/man/io_uring_enter.2#L1165-L1202)

## 完整的一次 read 示例（未编译、未运行）

下面是完整 C 源码，只有一个 main；本轮只做静态核对。调用者必须提供获准、受控、内容在验证期间不变的普通文件。`O_NONBLOCK` 与 `fstat` 不能保证任意设备路径安全，也不是文件内容快照。程序只读一次，不循环读完整文件；任意合法正短结果都接受。stdout 按实际长度输出原始字节，包含 NUL，stderr 单独诊断。

异常控制流刻意很窄：进程寿命的静态 ring/payload 避免返回时丢掉在途内存；一旦提交或等待状态无法按本例证明，记录诊断后 `_Exit` 终止**整个独立进程**，不 free、不返回继续运行的宿主、不换同步接口重发。不确定出口不能复制成库或服务器的 shutdown。

```c
#define _GNU_SOURCE
#include <liburing.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Static-review candidate only: NOT COMPILED, NOT RUN.
 * Standalone, single-threaded process; one regular-file read at offset zero.
 * The caller must supply an authorized, controlled ordinary-file path.
 * O_NONBLOCK/fstat do not make arbitrary paths or device opens safe, and do
 * not provide a snapshot against concurrent file modification.
 * Any positive short result is valid; no min(file_size, READ_LIMIT) promise.
 * No SQPOLL, registered resources, links, cancellation, multishot or CQE skip.
 * stdout contains exactly the bytes returned by this one read.
 * stderr contains diagnostics and the successful completion's byte count.
 */
enum { READ_LIMIT = 4095 };
static unsigned char payload[READ_LIMIT];
static struct io_uring ring;
static const __u64 read_token = 1;

/* No caller resumes after this function. Keep process-lifetime payload intact.
 * _Exit does not prove that pending I/O completed/canceled or was durable.
 * Kernel/process teardown owns the remaining descriptors and ring here.
 * This is deliberately NOT a reusable library/server shutdown protocol.
 */
static _Noreturn void uncertain_stop(const char *stage, int result)
{
    fprintf(stderr, "%s: result=%d; terminating without reusing payload\n",
            stage, result);
    _Exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s regular-file\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* O_NONBLOCK prevents a mistakenly supplied FIFO from blocking at open.
     * The input contract is still a user-controlled ordinary regular file.
     */
    int fd = open(argv[1], O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;               /* no ring and no request yet */
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);                          /* keep fstat as first error */
        return EXIT_FAILURE;
    }
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "input must be an ordinary regular file\n");
        close(fd);
        return EXIT_FAILURE;
    }

    int ret = io_uring_queue_init(8, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "queue_init: %s\n", strerror(-ret));
        close(fd);
        return EXIT_FAILURE;               /* no initialized ring to exit */
    }
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    if (sqe == NULL) {
        fprintf(stderr, "no SQE in fresh ring\n");
        io_uring_queue_exit(&ring);          /* nothing has been submitted */
        close(fd);
        return EXIT_FAILURE;
    }
    io_uring_prep_read(sqe, fd, payload, sizeof(payload), 0);
    io_uring_sqe_set_data64(sqe, read_token);

    ret = io_uring_submit(&ring);
    if (ret != 1)
        uncertain_stop("submit (expected one)", ret);

    struct io_uring_cqe *cqe = NULL;
    ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret != 0 || cqe == NULL)
        uncertain_stop("wait_cqe", ret);

    /* Copy every CQE field we will use before releasing its slot. */
    const __u64 token = cqe->user_data;
    const int res = cqe->res;
    const unsigned flags = cqe->flags;
    if (token != read_token || flags != 0 || res > READ_LIMIT)
        uncertain_stop("unexpected CQE", res);

    /* Under the stated single-shot contract this target is now terminal. */
    io_uring_cqe_seen(&ring, cqe);
    cqe = NULL;
    int failed = 0;
    if (res < 0) {
        fprintf(stderr, "read: %s\n", strerror(-res));
        failed = 1;
    } else {
        if (fprintf(stderr, "bytes=%d\n", res) < 0)
            failed = 1;
        if (fwrite(payload, 1, (size_t)res, stdout) != (size_t)res)
            failed = 1;
        if (fflush(stdout) == EOF)
            failed = 1;
    }

    if (close(fd) < 0) {
        perror("close");
        failed = 1;                         /* never retry close blindly */
    }
    io_uring_queue_exit(&ring);              /* target already terminal */
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
```

逐出口理解比“最后调用 queue_exit”更重要：

| 出口 | 已知状态 | 本例如何结束 |
| --- | --- | --- |
| 参数/open/fstat/类型检查失败 | 未建 ring，没有 io_uring 请求 | 返回失败；已有 fd 关闭一次；首失败保留，close 的次错不覆盖首因 |
| queue_init 失败 | 没有成功初始化的 ring | 关闭输入 fd，返回失败，不调用 queue_exit |
| 新 ring 的 get_sqe 失败 | 从未发布请求 | queue_exit、关闭 fd、返回失败 |
| submit!=1；wait 错误/缺失 CQE；身份或形态异常 | 不能按本例确认唯一目标终结 | uncertain_stop，不复用 payload，不盲等/盲重发 |
| 识别唯一目标终结，res<0 或输出/close 失败 | 目标已终结，后续仅用 CQE 副本 | seen 后处理/清理，最终失败 |
| 识别终结且输出/close 成功 | 本次读出的实际字节已经交给 stdout 流 | 正常清理，成功；不代表读完整文件或输出已持久化 |

`_Exit` 不调用 atexit/on_exit，也不保证刷新 stdio；日志函数调用不保证日志一定送达。关闭 fd 可能有未知延迟，哪些 pending I/O 被取消依实现而定。这里的安全边界是“应用不再恢复执行并复用 payload”，不是及时取消、收齐全部 CQE、完成业务清理或持久化的证明。Linux 6.1 进程退出包含 io_uring 取消及 mm/files 的内核回收，但它不把普通 `close(fd)` 变成同步 I/O 完成屏障。[固定版 _exit/_Exit 合同](https://github.com/mkerrisk/man-pages/blob/091fbf1fef4808f0ccfe0ff8c333aedf833b8782/man2/_exit.2)、[Linux do_exit](https://github.com/torvalds/linux/blob/830b3c68c1fb1e9176028d02ef86f3cf76aa2476/kernel/exit.c#L775-L815)

## 最小实验

### 环境记录与操作边界

后续验证先记录真实机器、编译器和已有 liburing 的版本，不从 uname 或 kallsyms 判定权限。下列只读入口也是文档内容，本轮没有重新执行：

```bash
uname -r
cc --version
pkg-config --modversion liburing
pkg-config --cflags --libs liburing
```

pkg-config 成功只说明构建发现信息；头文件、链接库、加载时库可能不同。当前没有可确认的既有 liburing，本轮到静态源核为止，没有编译、安装或 setup/probe。将来若使用已有依赖，应保存精确构建/加载版本，编译通过也不能当作内核支持证据。

### 单次 read 的后续验证入口

以下命令仅供在已准备工具链、已有授权且独占的实验目录中执行。把上节完整围栏保存为 `uring-read.c`；所有输入与输出名必须是该目录中新文件，不能覆盖用户文件。没有安装命令，不以此暗示本次已运行：

```bash
cc -std=c11 -O2 -Wall -Wextra $(pkg-config --cflags liburing) \
  uring-read.c $(pkg-config --libs liburing) -o uring-read \
  > build.stdout 2> build.stderr
build_result=$?
printf 'build_exit=%s\n' "$build_result"
if [ "$build_result" -eq 0 ]; then
  printf '%s\n' 'hello io_uring' > input.txt
  ./uring-read input.txt > actual.bin 2> diagnostic.txt
  result=$?
  printf 'exit=%s\n' "$result"
  wc -c input.txt actual.bin
fi
```

`hello io_uring` 加一个换行共 15 bytes。正常完整取得该小 fixture 的纸面预期是 exit=0、stderr 有 `bytes=15`、stdout 与输入相同；这不是“read 必定填满”的 API 保证。更一般的成功验收是：实际 `res=n` 在有效范围内，stdout 长度为 n，字节等于受控输入从 offset=0 起的 n 字节前缀；诊断不混入 stdout。固定小文件全读预期落空时记录实际结果并调查，不能把合法正短读改判成 API 错误。

| 设计输入/轨迹 | PAPER_EXPECTED 与判定 | 本轮状态 |
| --- | --- | --- |
| 15-byte 文本 | 常见正常路径 n=15；通用合同按实际 n/输出前缀核对 | NOT_RUN |
| 空普通文件 | 正长度 read 返回 0 表示 EOF；stdout 0 bytes | NOT_RUN |
| 大于 4095 bytes 的固定文件 | 只验本次 0≤n≤4095 与前缀，不要求输出文件全长，也不强制 n=min(file_size,4095) | NOT_RUN |
| 含 NUL 的受控文件 | 仍按长度和字节比较；不得用 `%s` 或文本显示截断来验收 | NOT_RUN |
| 不存在的受控路径 | open 失败、非零退出、没有建立 ring；stderr 诊断 | NOT_RUN |
| 纸面 submit=0/负数、wait 失败、错误 token | 进入 uncertain_stop；这些分支未做故障注入，也未观察到实际发生 | NOT_RUN |

请求参数、fixture、出口三者必须对应。不要固定要求“一次 setup 与一次 enter”：等待、唤醒和其他条件可能改变调用数，strace 也改变运行负担。若后续受限环境拒绝 setup，保存实际退出码和错误，不改 seccomp/sysctl 绕过。外部监督时限不承诺内核中卡住的操作一定及时终止。

## 工程陷阱

### 短 I/O 是一次操作的结果，续作才是应用协议

对要求完成给定字节区间的普通文件读取，设待处理区间起点 offset=100、remaining=10：纸面第一次 `res=4` 后，下次使用 payload+4、offset=104、remaining=6；若随后 read=0，按 EOF 结束并由上层判断是否满足业务长度。正结果先检查不越界，再推进；负结果先分类，再考虑有界重试，不能先转成 `size_t`。每次续作是新的请求，需要自己的 token 与资源跟踪。

普通文件写入也按正进度推进 offset/buffer/remaining；正长度写返回 0 视为无进展而停止或交上层处置，不无限重试。`-EAGAIN`、`-EINTR` 是否重试取决于操作、已知进度、deadline 和次数/资源预算；不能把所有负值都当“再发一次”。socket stream 没有文件 offset，按协议维护收发位置；数据报需要保留消息边界，不能照搬流式拼接。本文完整例有意只做一次 read，以上是续作合同，未扩成通用框架。[read 返回值](https://github.com/mkerrisk/man-pages/blob/091fbf1fef4808f0ccfe0ff8c333aedf833b8782/man2/read.2)、[write 的 partial 说明](https://github.com/mkerrisk/man-pages/blob/091fbf1fef4808f0ccfe0ff8c333aedf833b8782/man2/write.2)

### 注册资源是前置成本

注册 files/buffers 用长期内核引用或映射，换取较低的每次 I/O 成本；不是“减少注册开销”。fixed file 的 SQE fd 填**注册表索引**并设置 `IOSQE_FIXED_FILE`，不是原进程 fd。fixed buffer 使用相应 READ_FIXED/WRITE_FIXED 等操作及有效 `buf_index`，addr/len 必须落在该注册区间；普通 prep_read 不会因地址恰好被注册就自动转成 fixed I/O。

应用最易审查的协议是：停止新请求使用该资源 → 跟踪并收割旧引用对应的目标终结 → 调用注销并核返回值 → 再回收应用内存。注册/注销不是自动成功；Linux 6.1 的注销路径可能等待引用并返回错误，失败时不能当作已注销。更新接口返回也不必已释放旧资源，旧引用可延至在途请求完成；资源 tag 通知与 I/O token 还需区分。

ring 真正销毁时内核会自动注销普通注册 files/buffers，所以不是每条退出路径都必须先显式 unregister。但 `queue_exit` 返回不证明该异步销毁已完成，更不能据此 free 仍在使用的用户 payload。注册不使连接对象自动延寿、不解决 data race；pin 的内存大小和数量仍需容量限制。[注册手册](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/man/io_uring_register.2)、[read_fixed](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/man/io_uring_prep_read_fixed.3)、[Linux 资源注销路径](https://github.com/torvalds/linux/blob/830b3c68c1fb1e9176028d02ef86f3cf76aa2476/io_uring/rsrc.c#L304-L350)

## 超时、取消与资源生命周期

### 取消匹配的是 token 值

默认 flags=0 时，`io_uring_prep_cancel64(cancel_sqe, target_token, 0)` 将目标 user_data 的**值**写入 addr；不是传“保存 token 的变量地址”。随后给取消 SQE 设置另一个 `cancel_token`。不要复用尚未终结的 target token，否则取消或迟到 CQE 可能匹配错代请求。[prep_cancel64 实现](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/src/include/liburing.h#L614-L625)、[内核匹配](https://github.com/torvalds/linux/blob/830b3c68c1fb1e9176028d02ef86f3cf76aa2476/io_uring/cancel.c#L30-L51)

| cancel_token 的 res（默认 flags=0） | 表示什么 | 对 target 的动作 |
| --- | --- | --- |
| 0 | 找到并成功取消目标 | 仍解释目标自己的终结 CQE；取消结果不是目标字节数 |
| -ENOENT | 未找到；可能早已完成，也可能 token 错误 | 先查本地终结账；已收割的 target 不再等第二次，尚未确认的继续追踪并排查 |
| -EALREADY | 已进入不能按此方式取消的执行阶段 | 等目标自己的真实终结结果；不能提前释放 payload |
| 其他负值 | 取消参数/操作本身失败 | 保留目标原有状态，按错误分类处理 |

ALL/ANY 等匹配多个请求的 flags 有计数返回规则，不能把上表的成功 0 套到所有取消模式；正数计数也不是目标业务字节结果。对于该表涉及的单次目标，若取消前已处理 terminal，只处理剩余控制 CQE；否则直到目标 terminal 及应用引用结束才回收。不要依赖取消/目标两个 CQE 被应用处理的相对次序。成功取消不回滚已发生的写入、发送或其他副作用。[取消结果合同](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/man/io_uring_prep_cancel.3)

### linked timeout：先防半链，再处理部分提交

独立 TIMEOUT 可以按时间/完成计数驱动事件循环。LINK_TIMEOUT 是与主操作协同计时的专门语义，不能简单套用普通 LINK 的“前项完成后后项才执行”。read SQE 带 LINK，后接 timeout SQE，两者分别带 token；超时 `-ETIME` 表示计时到期且尝试取消目标，`-ECANCELED` 表示主请求先结束使计时器被取消。两种情况都不能替代 read 的结果。

以下 **C 准备片段，未编译、未运行** 展示替换基础例“取一个 SQE 并准备”部分时的最窄条件：仍是独立单线程进程，ring 刚初始化且空、没有在途请求、flags=0，fd 已按基础例打开，payload/ring 及 uncertain_stop 采用上节定义。它不是第二个完整程序；要真正提交这一变体，必须另写下述双结果处理逻辑，不能沿用基础 main 的 submit==1 和唯一 token 判断。

```c
/* Preconditions: fresh empty ring; one SQ writer; no in-flight request.
 * Preparation only. Both parameter and payload storage outlive the pair.
 */
static struct __kernel_timespec deadline = { .tv_sec = 1, .tv_nsec = 0 };
const __u64 timeout_token = 2;
if (io_uring_sq_space_left(&ring) < 2)
    uncertain_stop("need two SQ slots", 0);
struct io_uring_sqe *read_sqe = io_uring_get_sqe(&ring);
struct io_uring_sqe *timeout_sqe = io_uring_get_sqe(&ring);
if (read_sqe == NULL || timeout_sqe == NULL)
    uncertain_stop("pair allocation failed", 0);
io_uring_prep_read(read_sqe, fd, payload, sizeof(payload), 0);
io_uring_sqe_set_data64(read_sqe, read_token);
read_sqe->flags |= IOSQE_IO_LINK;
io_uring_prep_link_timeout(timeout_sqe, &deadline, 0);
io_uring_sqe_set_data64(timeout_sqe, timeout_token);
/* No submit here: the caller must implement the pair protocol below. */
```

先拿齐两槽再填充、发布，且预检到实际取得期间只有一个 SQ 写者。意外分配失败直接终止本独立进程，不能普通返回后让其他 submit 顺手发布已经取得的一半；一般服务应停用该生产路径并按已定义的队列所有权协议处理，而不是盲目“flush 解围”。

即使拿齐两槽并同次 submit，也不保证内核 all-or-none 接受。固定 Linux 的 `io_submit_sqes` 可在消费部分 SQE 后停止，`io_submit_state_end` 会处理当前链头；链不能跨提交边界。若 submit!=2，不得稍后单独补发 timeout 并声称原 read 的一秒 deadline 被补全。已经接受的 read 仍可能在运行，任何尾部状态都须保留，不重建目标；这个独立进程教学变体可采用相同 uncertain_stop 终止边界，不能普通清理 payload 后继续运行。[部分提交源码](https://github.com/torvalds/linux/blob/830b3c68c1fb1e9176028d02ef86f3cf76aa2476/io_uring/io_uring.c#L2181-L2303)

双结果处理必须分别标记 read_terminal、timeout_terminal，按 token 复制结果后 seen；read 的 res 表示数据结果，timeout 的 res 表示计时器结果。只看到 timeout 到期不能释放 read payload；先看到 read 完成也还有控制 CQE 要处理。本片段保守把 timespec 控制参数与 payload 都留到两者处理结束；这不表示两类对象的 API 最短寿命相同，也不依赖 SUBMIT_STABLE 优化。

普通 LINK 约束链内依赖，不提供全局 CQE 排序；错误或 unexpected 短 read 可断链，使未启动尾部以 `-ECANCELED` 结束。HARDLINK 只改变已成功提交请求的完成结果断链行为，提交失败仍能断链；它不是事务，也不撤回前项副作用。[LINK_TIMEOUT 与 LINK/HARDLINK](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/man/io_uring_enter.2#L630-L650)

## 网络服务器模式

一条基础 stream 连接可用 `ACCEPT → RECV → 解析 → SEND → RECV/关闭` 描述，但连接状态与每个 I/O 请求状态必须分开。ACCEPT 成功创建新 fd/连接所有者；每次 recv/send 各有 token，payload 在各自目标 terminal 前保留；解析器可能跨多次 recv 拼成一条消息，send 的正短结果只推进已发送前缀。

对正长度 stream recv，res=0 表示对端有序关闭；零长度 datagram 或请求长度为0也可能返回0，不能统一按断连处理。取消 recv 的 CQE 不允许直接销毁仍被 send、计时器或业务任务引用的连接。deadline 到来先关闭新业务入口，再对在途请求发控制操作并收割；连接 generation 与引用账防止迟到结果命中复用连接。[recv 的零结果范围](https://github.com/mkerrisk/man-pages/blob/091fbf1fef4808f0ccfe0ff8c333aedf833b8782/man2/recv.2#L438-L446)

CQ 消费线程只做有预算的解析/派发，长业务交给明确持有引用的任务；这会增加另一个释放条件，不能只统计 CQE。换成 multishot recv 时必须重新设计提供 buffer 池、MORE、每个 buffer 归还和请求 terminal，基础 single-shot 状态机不能原样套用。本文没有网络实现或网络实测。

## 背压与并发控制

SQ entries 不是吞吐目标。应用设置每连接、每租户及全局的请求数和字节预算：先预留资源，再准许准备/发布；尚未被内核消费的已发布请求也占用预算，不能因为 submit 计数未增长就忘记它。普通 single-shot 的 inflight 按逻辑请求是否 terminal 计，控制请求另有额度，multishot/SEND_ZC 还分别保留请求和 buffer 的未完成账。

高水位暂停生产者，低水位恢复，留出迟到完成、取消和控制事件的空间。例如 70% 暂停、50% 恢复只是设计起点，应由 payload 大小、处理停顿、deadline 和负载验证决定。到达 deadline 先停止新用途、尝试取消，不因发出 cancel 就立刻返还全部预算。`-EAGAIN`/`-ENOBUFS` 按原因、剩余 deadline 和重试次数处理，避免无进展循环。

监控 SQ 空间、CQ 占用/overflow、准备未发布量、发布未消费量、未终结目标数、控制请求数、保留 payload 字节、取消率与最老请求等待时间。共享 ring 需要明确 SQ/CQ 各自所有者、跨线程同步、唤醒以及关闭协议；liburing 的内核共享内存顺序不等于应用任意多写者自动安全。每线程 ring 是另一种所有权选择，也有资源成本，收益应在等价负载下比较。

## 性能

性能判断应从可检验的假设出发：批量是否减少每个已完成请求的提交/等待 syscall；注册是否在足够长的使用期里摊平前置成本；是否只是增加并发或改变了缓存命中。CQ 批量消费可以摊销处理成本，但“降低锁竞争”不是任何架构必然成立的结果。

SQPOLL 将一部分提交工作移给内核线程，可能节省应用线程周期，也增加系统 CPU 消耗；空闲可睡眠/需唤醒，延迟、功耗都与负载有关。只报应用 CPU 会漏掉系统成本。记录有效吞吐、错误/重试、p50/p95/p99、总 CPU cycles/完成操作、RSS/pinned 内存与实际深度，再讨论取舍。普通共享环和 SEND_ZC 的零拷贝协议不是同一个性能机制。

strace 能观察 setup、enter、register 等 syscall，不能看到每次纯用户态 SQE 准备/CQ 消费；它不是“只看初始化”的工具，也不能单独证明某次 payload 已安全。perf/采样与追踪有各自开销和权限条件，探查运行与正式计时分开；不从小文件单次运行推导生产 p99。

## 同步、epoll 与 io_uring 对照 Benchmark

比较必须先选同一种工作。若比较文件读取，则三组都使用同一受控文件、请求 offset 序列、块大小、有效请求数、并发预算和验证规则；epoll+worker 的 epoll 负责派发/通知，文件读仍由 worker 完成，不能拿“64条网络连接”与另两组文件读直接比较。若比较网络服务，则三组共享相同协议、连接/请求轨迹和端到端计时边界，另做实验。

原有三个自定义程序没有提供实现。以下是待实现的**接口设计说明，不是可复制执行的命令**；完成源码、构建入口与正确性合同前，没有复现结论：

```text
sync-read:        pread 工作线程按统一 offset 序列执行普通文件读取
epoll-worker:     同一读取工作由 worker 执行，epoll 处理派发/完成通知
uring-read-bench: 同一 offset 序列由 io_uring 提交并按 token 收割
三者共有输入：受控 file、block_size、offset_trace、request_count、并发预算
三者共有输出：每请求身份/结果/字节校验、计时起止、错误、原始延迟样本
状态：接口尚未实现；不是本页 uring-read 的别名或运行结果
```

先以总 outstanding 相同的配置比较，再单独扫描并发，不把同步 engine 的 iodepth 参数当作真的异步深度。缓存读取与 direct I/O 分开；direct I/O 的对齐、文件系统、设备前提也必须满足，它不证明冷缓存或持久化。CPU 亲和、NUMA、worker 数与总 CPU 预算固定并记录，不通过“io_uring 多用一个核”掩盖成本。

每次输出实现/版本、请求轨迹标识、实际深度分布、完成 ops/s 与 bytes/s、p50/p95/p99 的具体口径、应用与系统 CPU、syscall/完成操作、错误和重试率。不要把 fio 的 clat、总 lat 和业务端到端延迟混列成同一个 p99；也不能平均多个 p99 得到合并样本的 p99。低并发缓存命中时同步可能更便宜、批量时 io_uring 可能受益，都是待检验假设，本轮没有结果。

## 实验与 Benchmark

### 先验收正确性，再选择测量预算

先固定问题与成功条件，再做小输入/失败路径验证；按一个变量一组组织对照，保存每次原始结果，包括失败、超时和中断。计时覆盖所声称的工作直到目标完成，不能在 submit 后立即停表。预热、测量时长、重复次数根据噪声和预算事先决定；“30次”“5次”及“p99退化不超过10%”都不是通用标准。

记录加载/分配热身、CPU 频率/SMT/NUMA、后台负载、缓存状态及工具扰动。微基准必须防止编译器消去有效工作；端到端性能还要确认正确性、安全边界、错误率和恢复行为没有被优化改变。性能回归套件应绑定可比硬件与基线、预先约定阈值，超阈值保存诊断再定位/bisect；本文没有新增或运行 runner、CI、回归套件。

### fio 的有限深度设计

以下是**未执行的命令设计**。前提：已经获准对该机器测量，fio 版本已记录，独占工作目录下已有一个至少 1 GiB 的受控普通文件 `controlled-existing.bin`，它不是设备或生产数据，已有目录 `results` 内相应输出名未占用。只读检查和禁止新建不能代替调用者确认对象；本轮没有创建文件或执行 fio。每个 iodepth 是一个标量，默认资源/SQPOLL 关闭：

```bash
for depth in 1 8 32; do
  fio --readonly --allow_file_create=0 \
    --name="uring-d${depth}" --filename=./controlled-existing.bin \
    --ioengine=io_uring --rw=randread --bs=4k --size=1G \
    --direct=1 --iodepth="$depth" --numjobs=1 \
    --fixedbufs=0 --registerfiles=0 --sqthread_poll=0 \
    --randrepeat=1 --randseed=23 \
    --ramp_time=10 --runtime=30 --time_based \
    --lat_percentiles=1 --percentile_list=50:95:99 \
    --output-format=json+ --output="results/direct-base-d${depth}-r1.json"
  result=$?
  printf 'depth=%s exit=%s\n' "$depth" "$result"
  if [ "$result" -ne 0 ]; then break; fi
done
```

10 秒预热与 30 秒采样只是本命令设计的预算，ramp_time 额外增加运行时间；不是本轮已发生的测量。需要更多重复时每次使用新的 r编号和完整 stdout/stderr/退出码，不能覆盖失败样本。实际 depth 分布以 fio 输出为准，设为32不证明一直达到32；同步 engine 提高 iodepth 通常不增加在途量。[fio 深度与批量](https://github.com/axboe/fio/blob/db7fc8d864dc4fb607a0379333a0db60431bd649/HOWTO.rst#L2841-L2857)

| 单独设计的变体 | 相对于同深度基础组的参数变化 | 需要观察 |
| --- | --- | --- |
| fixed buffers | `--fixedbufs=1` | 前置注册成本、总 CPU、内存与有效完成数 |
| fixed files | `--registerfiles=1` | 文件引用成本是否变化 |
| 提交轮询 | 固定同一 registerfiles 组再设 `--sqthread_poll=1` | fio 3.32 此模式要求 registerfiles；系统 CPU 与尾延迟 |
| buffered | 独立实验设 `--direct=0` | 记录真实 cache 策略，不与 direct 组合合并归因 |

这些参数来自 [fio 3.32 引擎选项](https://github.com/axboe/fio/blob/db7fc8d864dc4fb607a0379333a0db60431bd649/HOWTO.rst#L2262-L2300)。选项存在不证明宿主支持。clat 是提交到完成收割，lat 含 fio 自己的提交阶段，既不是设备内部时延，也不覆盖业务入队前等待；明确使用哪个分布。用于性能分析的 syscall 计数要指定进程/系统范围、工具与完成数分母；perf 事件不可用时记录缺失，不填0。回滚配置先停止新请求并处理在途，下一次新建 ring 才改 setup flags；不能在尚有目标时关闭资源并盲目备用重发。

## 深入实验清单

所有行都是后续实验设计，状态 **NOT_RUN**；不能把纸面轨迹填成运行 PASS。

| 主题 | 输入/对照 | 必须核对的结果与失败边界 |
| --- | --- | --- |
| fixed buffers/files | 普通与 fixed 的同一读取轨迹 | index/flag/range、注册返回、在途引用、注销错误及旧引用释放，不只看 CPU |
| LINK/HARDLINK | 正常、前项负结果、unexpected 短 read | 链内依赖和未启动尾部结果；链外无全局排序，提交失败不被 HARDLINK 修复 |
| LINK_TIMEOUT | 主目标先完成、计时先到、部分提交 | 两种 token 的独立终结；-ETIME不证明 payload 可回收；不可跨提交补链 |
| ASYNC_CANCEL | 目标尚在途、已完成未消费、已收割 | 取消与目标 res 分开，已收割目标不重复等待，副作用不回滚 |
| multishot / SEND_ZC | 模式前提与 buffer 池/通知设计 | 每 CQE 的 MORE、终结及 NOTIF 寿命；非本页完整实现 |
| 每线程 ring / 共享 ring | 相同工作与资源预算 | SQ/CQ 所有权、唤醒、锁争用、跨线程关闭与引用回收 |

保存每个请求 token、代际、操作、发布/消费观察依据、CQE res/flags、应用 terminal 状态和相关资源引用。源码推导、用户态计数和真实内核观察分列；仅计数相等不证明所有身份对应正确。出现不支持或权限失败时记录并停止该模式，不改系统安全配置；高级 flags 的回滚须保留原始失败与正确性检查。

## 诊断

先问错误发生在哪一层，再结合队列和对象状态：

| 层 | 观察值 | 不能据此推出 |
| --- | --- | --- |
| open/fstat 等同步 API | 返回 -1，读取 errno | io_uring 的 CQE 错误也是读取当前 errno |
| queue_init / 注册 | 返回0或负 errno、实际 features | 初始化成功就支持所有 opcode/flags/对象 |
| get_sqe / 准备 | NULL 或有效槽、明确 token | 槽已准备就已经发布 |
| submit | 返回数量或负 errno，结合共享 SQ 状态 | 目标已经完成；异常时整个批次必然没有副作用 |
| wait/peek | 获取 CQE 的状态 | wait=0 就代表 I/O 成功 |
| 目标 CQE | 身份、res、flags | cancel/timeout res 是目标读写字节数 |
| 应用处理与释放 | terminal账、引用、payload账、CQ head | 全部seen就不会泄漏连接对象或注册内存 |

诊断日志不要只留 submit 总数。记录 requested/prepared/published、可确认的消费、target terminal、控制 CQE、CQ overflow、应用资源保留原因；对 CQE 先复制字段再记录/seen。关联 perf/strace 时写明扰动与可见范围。文中没有执行任何追踪工具。

## 排障清单

- `-EINVAL`：先区分 setup 参数、SQE 字段、具体 flag/对象模式与控制请求参数；不是见到 EINVAL 就断言 buffer 已释放
- `-EBADF`：核对普通 fd 是否仍有效、fixed file 是否错误填成进程 fd、操作是否支持 fixed；generation/token 错误也需另查
- CQE 长时间未见：核对是否真的发布/消费、是否等错 token、目标是否已经收割、是否开启省略 CQE 的模式、完成处理/task_work 是否得到运行机会；有界诊断，不能无限重发
- CPU 高：分别看业务 busy loop、CQ peek、SQPOLL、worker、重试和系统 CPU，按同负载对照，不能单靠 flags 猜原因
- 内存增长：同时核 CQ 槽、payload、连接/请求引用、注册/pinned内存与待回收资源；seen 只是槽归还

`io_uring_queue_exit` 在 liburing 2.3 解除 SQE/SQ/CQ 映射并关闭 ring fd，没有等待目标 CQE 的返回协议。Linux 6.1 的 `io_uring_release → io_ring_ctx_wait_and_kill` 将 `io_ring_exit_work` 排到 system_unbound_wq；尝试取消、等待引用与资源清理在 worker 中进行。因此 queue_exit 不是在途 payload 释放屏障或同步取消屏障，不能由函数名中的 wait 推导它同步完成。[liburing queue_exit](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/src/setup.c#L191-L208)、[Linux 异步销毁](https://github.com/torvalds/linux/blob/830b3c68c1fb1e9176028d02ef86f3cf76aa2476/io_uring/io_uring.c#L2725-L2837)

生产关闭应停止接收新业务与发布，明确处置准备未发布项，跟踪已发布目标和控制请求，必要时尝试取消，继续处理到真实终结及引用结束，再注销/释放应用资源、关闭 fd、退出 ring。关闭 fd 不代替这套协议。若不能建立终结条件，应进入预先设计的隔离/故障退出策略，不能宣称“调用 queue_exit 后 free 即可”。上节整个进程 `_Exit` 只服务独立教学例，不提供服务器优雅关闭模板。

## 故障排查与安全检查

`-ENOMEM` 要按分配失败阶段查看队列、注册规模、进程/系统内存限制；注册 buffer 的锁页预算与普通 ring 初始化不是一条万能 RLIMIT_MEMLOCK 解释。`-EBUSY` 应优先结合 CQ 未消费、overflow、NODROP 与 enter flags 检查；固定版 enter 手册列明 CQ overflow 无法刷出等条件，单个 errno 不能证明 SQPOLL 被 CPU 配额限制。[enter 的 EBUSY 条件](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/man/io_uring_enter.2#L1526-L1542)

CQ 满时优先提高有界消费能力、降低生产速率并调查引用/业务停顿；不能只扩容。结果偶发错乱先查已发布 payload 是否提前复用、SQ/CQ 是否多写者竞争、token 是否重复或指向已失效上下文。`-EINTR` 按当前阶段和请求 deadline 处理，不能无界重试等待而丢失关闭信号。

权限失败只记录实际内核、容器策略、setup/enter/register 的具体错误与允许读取的配置；若某个宿主有 io_uring_disabled 等策略入口，应以该宿主实际存在的接口为准，不把新内核选项强加到固定 Linux 6.1。不要更改 seccomp、权限或 sysctl 以制造通过。对不可信长度限制单次/总 payload 和注册内存，并按客户端断开、deadline、shutdown 区分取消原因。

降级的安全点是业务操作尚未开始，或已依据结果/幂等协议确定下一步。初始化失败且没有请求时可另选同步/epoll路径；已发布或可能产生副作用后，不能因 submit/wait 报错就把同一写入/发送直接重发到备用接口。READ 虽无写副作用，重复读取也可能看到并发修改后的不同数据；回滚配置和重放业务是两件事。

## 内核与 liburing 版本矩阵

本页用固定版本研究合同，不把内核版本与 liburing 版本机械一一配对；也不把研究基线当部署最低版本。发行版 backport、配置、权限与对象条件都可能改变实际能力。

| 具体能力 | 本次已读资料中的版本边界 | 仍需核对 |
| --- | --- | --- |
| 非向量 READ/WRITE | enter 手册：Linux 5.6 起 | 不与更早的 READV/WRITEV 混称；对象、offset、长度 |
| LINK_TIMEOUT | enter 手册：Linux 5.5 起 | 正确链及提交边界、双结果处理 |
| COOP_TASKRUN | setup 手册：Linux 5.19 起 | task_work 与提交/等待线程模型 |
| recv multishot | prep_recv 手册：Linux 6.0 起 | 该版本要求 len=0、BUFFER_SELECT、不能 MSG_WAITALL；不能推广到所有 multishot opcode |
| CQE_SKIP_SUCCESS | enter 手册：Linux 5.17 起 | 省略完成的独立协议；本页示例排除 |

能力检查依次是：构建头文件/符号与链接/加载库 → setup 是否成功及返回 features → opcode probe → 该 flag/模式 → 实际文件系统/socket 对象条件。若后续执行 probe，`io_uring_get_probe_ring` 需要有效 ring 并可能返回 NULL；只对有效 probe 调用 `io_uring_opcode_supported`，最后 `io_uring_free_probe`。probe 中存在 opcode 仍不证明所需模式可用。本基线 header 与 symbol map 没有所用 `io_uring_major_version()`，不要把该函数加入 liburing 2.3 示例；构建版本先记录 pkg-config 及实际库身份。[probe 声明与检查](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/src/include/liburing.h#L140-L159)、[固定 symbol map](https://github.com/axboe/liburing/blob/4915f2af869876d892a1f591ee2c21be21c6fc5c/src/liburing.map)

### 实际核对的固定来源

| 版本与不可变 revision | 实际阅读位置与支撑范围 |
| --- | --- |
| liburing 2.3：4915f2af869876d892a1f591ee2c21be21c6fc5c | `src/include/liburing.h` 的 get_sqe/CQ advance/cancel/link_timeout/probe；`src/queue.c` 的 flush/submit；`src/setup.c` 的映射与退出；对应 get_sqe/submit/wait_cqe/cqe_seen/prep_read/prep_cancel/register/read_fixed/setup/enter/recv 手册。各技术段已给固定链接 |
| Linux 6.1：830b3c68c1fb1e9176028d02ef86f3cf76aa2476 | `io_uring/io_uring.c` 的 io_submit_state_end/io_submit_sqes、io_ring_ctx_free、io_ring_exit_work/release；`cancel.c` 的匹配及结果；`rsrc.c` 的 quiesce/files/buffers注销；`kernel/exit.c` 的退出调用序列 |
| fio 3.32：db7fc8d864dc4fb607a0379333a0db60431bd649 | HOWTO 的 readonly、allow_file_create、direct、runtime/ramp_time、随机序列、iodepth/actual depth、fixedbufs/registerfiles/sqthread_poll、输出和延迟口径 |
| man-pages 5.13：091fbf1fef4808f0ccfe0ff8c333aedf833b8782 | 历史维护者仓库 `man2/read.2`、`write.2`、`recv.2` 的结果范围，以及 `_exit.2` 的 `_Exit`/stdio/延迟与 pending I/O 边界 |

阅读范围是这些实际函数/手册段，不是整套 Linux/liburing 的行为证明。手册 `.TH` 中遗留的较早版本标题不改变本次文件的 checkout revision。独立 `io_uring_prep_link_timeout.3` 页面未取得，本页依据已读 header 和 enter 手册，不伪装有另一份来源。没有把动态 man7 页面或仓库主页当作固定版本证据。

## 实验记录模板

后续每次实际实验独立保存：目标/假设/成功条件、日期、源码及散列、编译器/构建/加载库、内核/容器约束、CPU/内存/磁盘、文件系统/挂载、输入及内容指纹、offset/随机轨迹、cache/direct策略、并发/实际深度、CPU亲和/NUMA/governor、后台负载、原命令、退出码、完整 stdout/stderr、原始延迟样本/fio JSON、工具事件和计时口径。未运行字段明确 NOT_RUN；资料读取失败也保留真实错误。

结论回答：收益来自 syscall、并发还是缓存差异；什么负载下消失；错误与资源上限下如何停止；什么状态允许降级而不重复副作用。没有足够证据时只写探索性观察，不写生产容量或通用性能承诺。本文本轮未产生运行/Benchmark 原始结果，纸面状态追踪不冒充内核实验。

### 原文历史记录（非本次验证结论）

以下两块及紧随的“参考”整节逐字保留原始来源与日期语境。原文“当前标准”“按步骤执行实验”等话语是历史文本，不是本轮已运行或已验证的声明。旧 PDF 读取记录为超时，未确认内容、作者或出版日期；旧 kernel 文档 URL 返回 Internal Error，不能据此写成404。它们保留历史来源身份，现行教学合同由上节固定原始源承担。

> 知识基线：以当前标准、协议或工具链版本为准，平台差异需实测。
> 最后更新：2026-08-20。
> 官方参考：https://man7.org/。
> 验证与基准：按文中命令执行最小实验并记录指标。

> 知识基线：当前标准与工具链版本，平台差异需验证。
> 最后更新：2026-08-20。
> 官方参考：https://kernel.dk/io_uring.pdf
> 验证与基准：按文中步骤执行实验并记录指标。
来源：https://kernel.dk/io_uring.pdf｜验证与基准：liburing 示例、epoll 对照压测。

## 参考

- https://kernel.dk/io_uring.pdf
- https://man7.org/linux/man-pages/man7/io_uring.7.html
- https://github.com/axboe/liburing
- https://docs.kernel.org/userspace-api/io_uring.html

## 关联
- [Socket、Epoll 与 Reactor](01-Socket-Epoll与Reactor.md)
- [文件系统与存储栈](03-文件系统与存储栈.md)

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
  - [07-Linux系统编程 README](../../../00_Index/学习路线/编程与计算机基础.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/学习路线/编程与计算机基础.md)
