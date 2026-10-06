---
type: Mechanism
title: "02-虚拟内存、PageFault 与 mmap"
status: stable
verified: []
maturity: L2
description: "以不可变资源文件为主线，解释地址、驻留、缺页、映射寿命和可验证的 I/O 选型边界。"
updated: 2026-10-06
sources:
  - title: "Linux man-pages 6.19: mmap(2)"
    resource: https://man7.org/linux/man-pages/man2/mmap.2.html
  - title: "Linux 6.12: Page Tables"
    resource: https://docs.kernel.org/6.12/mm/page_tables.html
  - title: "Linux 6.12: Overcommit Accounting"
    resource: https://docs.kernel.org/6.12/mm/overcommit-accounting.html
  - title: "Linux 6.12: The /proc Filesystem"
    resource: https://docs.kernel.org/6.12/filesystems/proc.html
  - title: "Linux man-pages 6.19: getrusage(2)"
    resource: https://man7.org/linux/man-pages/man2/getrusage.2.html
  - title: "Linux man-pages 6.19: close(2)"
    resource: https://man7.org/linux/man-pages/man2/close.2.html
---
# 02-虚拟内存、PageFault 与 mmap

> 知识成熟度：L2。主要机制和接口以 Linux man-pages 6.19、Linux 6.12 官方文档为静态基线；局部 C 程序的有限实跑单列于第 9 节，不代表整篇机制、性能或故障恢复已验证。
> 适用范围：Linux 用户态、普通页、普通文件及私有匿名内存。Windows 只作 reserve/commit 概念对照；不覆盖设备映射、DAX、远程文件系统和内核完整实现。
> 最后核对：2026-10-06。本文的资源文件例要求调用者在整个映射期保证文件对象的内容和长度不变。

假设一个资源索引保存在文件中，查询只用其中少量字节。`mmap` 让程序通过地址读取这些字节，但“得到地址”没有回答三件事：第一次读是否阻塞、文件能否被别人改短、这段地址什么时候失效。先把这三件事连起来，才能判断映射是否适合索引、游戏资源包或大块 arena。它是一种访问和寿命合同，是否更快要用同一工作量的证据判断。

## 1. 一个地址背后有几种不同状态

虚拟地址是进程使用的地址值。Linux 用虚拟内存区域（VMA）记录一段地址的范围、权限及后备对象等属性；页表与 TLB 参与把地址翻译成实际可访问的物理位置。一个指针数值落入 VMA，并不等于此刻无需缺页就能访问，更不等于文件格式允许读取那个字节。

|问题|对应状态|不能由它推出的结论|
|---|---|---|
|这个地址范围归谁、允许什么操作？|进程地址空间及 VMA；`maps` 可观察范围和权限|页表已经就绪、全部字节驻留或对象仍有效|
|这一次读/写能否完成翻译和权限检查？|页表项及其权限；TLB 缓存翻译信息|一个硬件只读页发生写异常就必然是非法访问，COW 也可故意如此设置|
|后备数据此刻是否在 RAM？|物理驻留；普通文件通常经 page cache 支持|本进程已建立对应页表项、下次不会被回收，或读操作在应用边界内|
|系统为可能需要的内存承担多少计账？|Linux commit accounting 等资源承诺|相同数量的物理页已经分配或锁定|
|翻译是否在快速缓存里？|TLB 命中状态|数据的 CPU cache 命中，也不能反推 page fault 次数|

例如，创建一大片私有匿名可写映射后，地址范围可以存在，而许多页尚未因写入而取得独立物理页。随后读可能用零页，写入才需要可写后备页。又如，另一个进程已经读过文件，数据可能在 page cache 中，但新读者仍需建立自己的页表映射。这两种情况说明“地址已分配”“数据驻留”“访问无需 fault”是不同命题。[Page Tables，MMU/TLB 与 Page Faults](https://docs.kernel.org/6.12/mm/page_tables.html)

Linux 的 `Committed_AS`、`CommitLimit` 与 overcommit 策略描述计账；不同映射类型的成本不同，例如只读文件映射和可能产生私有副本的可写文件映射不能按同一规则理解。策略 0、1、2 分别有启发式、允许 overcommit、限制承诺量等差别；即便某次申请获准，也不能推出不受 cgroup/进程限额影响或绝不会 OOM。RSS/PSS 测的是驻留口径，不是 commit 的另一个名字。[Overcommit Accounting，模式与计账表](https://docs.kernel.org/6.12/mm/overcommit-accounting.html)

Windows 的 `VirtualAlloc(MEM_RESERVE)` 保留地址范围，`MEM_COMMIT` 承担相应内存承诺；commit 不等于在调用瞬间为所有页分配物理内存。这只是概念对照，不能把 Linux 的 VMA、`mmap` 标志和计账模式逐个替换成 Win32 API。[VirtualAlloc，MEM_RESERVE/MEM_COMMIT](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc)

页大小、地址有效位和硬件页表层数取决于架构与配置。Linux 的多级页表抽象可以折叠未使用的层；不能把五层页表、4 KiB 普通页或 2 MiB 大页写成所有机器的固定合同。

## 2. 一次访问为什么可能缺页，也可能不缺页

按以下分支追踪一次用户态访问，比“虚拟地址最终到磁盘”更准确。这是教学概观，不是完整内核调用图：

1. CPU 发出虚拟地址和操作类型。TLB 若有合适的翻译且权限允许，可以继续访问；TLB miss 则需页表遍历或架构相关的填充。如果页表已经满足访问条件，翻译完成后即可继续，**不必发生 page fault**。
2. 如果当前翻译/权限状态不能完成访问，可能进入缺页异常。内核结合 VMA 与操作类型判断：是允许的访问暂缺条件，还是根本不允许的访问。VMA 允许写、硬件页表暂只读的 COW 分支，与向真正只读区域写入不同。
3. 对可服务的 fault，内核取得或安排所需后备、更新映射，成功后重试原指令；错误分支可能发送信号或走资源不足处理。不能把“缺页处理成功”写成“每次异常都能恢复”。

|访问时的情形|可能需要的工作|教学分类|
|---|---|---|
|TLB miss，但已有可访问页表映射|完成翻译并填充翻译缓存|无 page fault 路径|
|合法匿名页首次访问|按需提供零内容；写入需要可写后备页|通常可无文件读取 I/O；具体分配/回收代价另看环境|
|合法 COW 写入|为写者安排私有可写页，通常涉及复制；条件允许时也可能复用|并非权限错误；不能一律推成读取磁盘|
|合法文件页在 page cache，进程尚无可用映射|补建相应页表映射|可出现无需 I/O 的 minor fault|
|合法文件页或交换页需要 I/O|取得数据，可能等待 I/O 后重试|可出现 major fault|
|没有合适的区域、操作违背权限，或文件后备已失效|不能按普通成功路径满足访问|可能 SIGSEGV、SIGBUS 等；不作为本文故障实验|

`ru_minflt` 统计不需 I/O 即可处理的缺页，`ru_majflt` 统计需要 I/O 的缺页。这不是“物理页数”“设备请求数”或耗时公式：预读、聚合、缓存和并发活动会影响它们。I/O 来源可以是文件或交换区，也未必是本地物理磁盘；没有固定的每次 5–20 ms 预算。[getrusage(2)，ru_minflt/ru_majflt](https://man7.org/linux/man-pages/man2/getrusage.2.html)

缺页处理可能使线程睡眠，也可能在分配时遇到回收。若发生在持锁路径，其他工作会连带等待，因此需关联请求长尾、I/O 和内存压力，而不是只追求计数越小越好。OOM 可能终止进程；本文不承诺在资源不足时靠一个信号处理器继续正常访问。以上针对用户态访问，不能直接套用到内核态不能睡眠或禁止 fault 的上下文。

## 3. 先约定合法字节，再建立映射

### 3.1 普通页接口合同

`mmap` 成功是在地址空间建立映射，并不承诺立即把全部文件读完。普通页文件映射的关键参数如下；具体接口依据 [mmap(2)，DESCRIPTION、munmap、RETURN VALUE](https://man7.org/linux/man-pages/man2/mmap.2.html)。

|参数或结果|本文使用的合同|
|---|---|
|`addr`|传 `NULL` 让系统选址；返回的基址须保存用于解除映射|
|`offset`|文件偏移，类型为 `off_t`，必须满足普通系统页大小的对齐要求；本例固定 0|
|`length`|必须大于 0；**普通页的 length 不必是页大小的倍数**|
|`prot`|按需要选权限；本例只有 `PROT_READ`，与只读打开方式匹配|
|`flags`|本例用 `MAP_PRIVATE`；它不提供整个文件的快照|
|返回值|与 `MAP_FAILED` 比较；失败值不能继续解引用，不能只打印诊断后往下走|
|`munmap`|普通页要求起始地址页对齐，length 不必页倍数；本例用原基址和原 length|

内核按页管理映射，应用却必须按文件和格式的字节长度做边界检查。长度为 1 的文件可用 `length=1` 映射，本例只准读字节 0。末页不足一页的部分有内核尾页语义，但不会由此变成应用可读的文件数据；不能利用尾页填充掩盖越界，更不能访问 EOF 之外的整页。文件被截短后，原先合法的访问可能不再有后备并触发 SIGBUS；先前的 `fstat` 并没有锁定长度。[mmap(2)，信号与尾页说明](https://man7.org/linux/man-pages/man2/mmap.2.html)

显式 `MAP_HUGETLB` 另有对齐约束：offset 按所选 huge page 大小对齐，`mmap` 会按该大小调整长度，而 `munmap` 的地址和长度均须满足 huge page 对齐。不能把本节普通页释放方式未经修改推广过去。[mmap(2)，Huge page mappings](https://man7.org/linux/man-pages/man2/mmap.2.html)

需要固定地址的高级场景还须区分 `MAP_FIXED` 的覆盖风险和 `MAP_FIXED_NOREPLACE` 的拒绝重叠语义。后者在 Linux 4.17 引入；面向不认识该标志的旧内核仍须核对实际返回地址。本文选址交给系统，不通过扫描 `maps` 再强制占用一个“看起来空闲”的地址实现分配。

### 3.2 非页对齐的逻辑窗口：只作纸面推导

假设文件在映射期不变，要读逻辑偏移 `offset` 起的 `N` 字节。普通页大小为 `P`，首先要求 `P>0`；实际若从 `sysconf` 取得，先检查返回值大于 0 且能表示为所用整数类型，不能把 -1 直接转成无符号大数。[sysconf(3)，PAGESIZE 与返回值](https://man7.org/linux/man-pages/man3/sysconf.3.html)

计算顺序是边界合同的一部分：

1. 检查 `0 <= offset <= file_size`、`N > 0`、`N <= file_size - offset`，不用可能先溢出的 `offset + N` 判断范围
2. 计算 `base = offset - offset % P`，再算 `delta = offset - base`；传给 `mmap` 的 base 必须能以 `off_t` 表示
3. 验证 delta、N 可转为 `size_t`，并检查 `N <= SIZE_MAX - delta` 后，才形成 `map_length = delta + N`；若沿用本篇 C 对象的 `PTRDIFF_MAX` 上限，也须检查该和不超过它
4. 逻辑数据从映射基址加 delta 开始，只有 N 字节属于本次请求；保存原基址和 map_length，释放时不用逻辑子指针

纸面取 `P=4096`、`offset=5000`、`N=32`，且文件至少有 5032 字节，则 `base=4096`、`delta=904`、`map_length=936`。这里 4096 是教学输入；936 不必向上凑成 4096 才能传给普通页 `mmap`。本篇不实现通用窗口解析器，后面的程序只收文件名并从 offset 0 映射整个非空文件。

窗口化减少同时持有的地址范围和应用活跃范围，常用于大文件和受限的 32 位地址空间；它不是全进程 RSS、page cache 或预读量的硬上限。映射失败时可拒绝本次请求，或由上层选择有清晰错误处理的显式 I/O；不能继续使用失败指针。

## 4. 私有写、共享可见、同步和持久性各负责什么

`MAP_PRIVATE` 的含义是通过该映射做的修改不会回写原文件；它没有承诺映射后的外部文件修改对该视图永远不可见。下面是允许写入的私有映射的纸面追踪，**不是第 6 节只读程序的实跑**：

|步骤|文件字节|私有读者视图|能得到的结论|
|---|---|---|---|
|准备不再被别人修改的文件|A|尚未映射|应用先建立稳定输入前提|
|建立私有可写映射并读该字节|A|A|初始内容来自该文件对象|
|通过私有映射把字节改为 P|A|P|私有修改不回写文件；COW 为这种分离服务|

此表不加入外部写者，因而不能证明有外部写者时“全文件仍是建立映射瞬间的快照”。`fork` 后写私有内存也可能产生 COW 成本，父子进程大范围写入会增加内存需求；真实复制粒度和大页行为依实现与配置判断。[mmap(2)，MAP_PRIVATE 与 fork 说明](https://man7.org/linux/man-pages/man2/mmap.2.html)

对 `MAP_SHARED`，共享同一后备区域的参与者可观察映射更新，但这只是共享数据的起点：

|需要解决的问题|额外责任|做完后仍不保证什么|
|---|---|---|
|别人何时可以消费一批更新？|应用定义发布/消费顺序，采用满足跨进程条件的同步原语|映射本身不会生成消息边界、版本或一致快照|
|参与者崩溃后怎么办？|定义所有权、锁恢复、代际/版本和损坏拒绝策略|一个锁名或环形 buffer 本身不是恢复协议|
|脏页何时完成写回？|按接口要求调用 `msync(MS_SYNC)` 并检查返回；它等待更新完成|不替代线程/进程同步，也不使多个字段原子提交|
|文件及新名字如何在崩溃后恢复？|根据文件系统/设备合同处理文件数据、元数据、目录同步与恢复设计|一次读回成功或一次调用成功不等于做过断电测试|

例如，写者修改两个字段后让读者消费，读者可能需要“版本和字段一起有效”的应用合同；把内存同步换成 `msync` 无法补上这个合同。反过来，进程间锁能协调读取时点，也不自动保证断电后文件完整。`fsync` 针对文件的数据/元数据同步；名称发布涉及的目录项还需单独考虑目录同步。事务性仍要设计日志、版本和恢复规则。[msync(2)](https://man7.org/linux/man-pages/man2/msync.2.html)、[fsync(2)](https://man7.org/linux/man-pages/man2/fsync.2.html)

共享环形缓冲区的原子索引、缓存线隔离、文件锁粒度和崩溃语义属于完整 IPC 协议的选择项，详见[进程间通信与跨进程同步](../并发与同步/05-进程间通信与跨进程同步.md)。此处不另造一份生产同步实现。

## 5. 资源索引的寿命：路径、FD、文件对象、映射、借出视图

路径用于查找对象；打开的 FD 是进程持有的引用入口；映射又有独立寿命。必须先确定哪个对象在读者使用期间稳定，再考虑何时关 FD。

|对象|拥有/结束方式|关键后果|
|---|---|---|
|目录中的路径名|由名称操作建立、替换或移除|`rename` 发布新名字不会把既有 FD/映射自动切换到新对象|
|FD|成功 `open` 后持有，`close` 结束该 FD 的使用|FD 0 也有效；关 FD 不撤销已成功建立的映射|
|文件对象|打开引用、映射等使它可继续被使用|没有路径名也可能仍可访问；对象寿命不等于所有页永驻 RAM|
|本进程映射|保存基址、长度和拥有者，最终 `munmap`|解除之后所有依赖这片地址的视图必须已退出|
|借出的指针/切片|只在拥有者约定的有效期间使用|一个裸指针、跨进程绝对地址或过期偏移都不替代所有权/边界合同|

`unlink` 去掉的是名称关联；已有映射不因名字消失而自动失效，但内核仍可按正常机制回收或重新取得页面。因此“映射仍活着”不能解释为“页面直到最后引用消失前都锁在内存”。[unlink(2)](https://man7.org/linux/man-pages/man2/unlink.2.html)、[mmap(2)，FD 与 munmap](https://man7.org/linux/man-pages/man2/mmap.2.html)

### 5.1 不可变版本发布：应用协议推导，未实现并发版

只读索引或资源包可按下列责任拆分。此处不是对多线程安全或断电安全的已运行承诺：

1. 写者在另一个文件对象完成新版本，校验格式、长度、版本及必要校验值，完成后不再修改该对象
2. 检查名称发布操作的结果，使后续 `open` 选择新版本；符合相应条件的 `rename` 可提供名称替换的原子性，但不自动提供目录持久性
3. 旧 FD/旧映射继续引用旧对象。所有能写旧对象的参与者都必须遵守不再改写/截短它的合同，包括持有旧 FD、硬链接或其他别名的写者
4. 读者先取得某一代映射的有效使用权，再校验偏移和长度，然后借出视图；停止接收这一代的新借用后，等待已有借用全部结束，才能 `munmap`

第 4 步需要真正的所有权和同步设计，例如明确的锁、引用计数或 epoch 合同；只写“使用 RCU”并没有实现它。可以提前去掉旧名称，但名称回收与等待读者退役是两件事。[rename(2)，名称替换与已有引用](https://man7.org/linux/man-pages/man2/rename.2.html)

`O_RDONLY` 限制的是这个 FD 的操作，`fstat` 只取得元数据，`MAP_PRIVATE` 只规定私有修改的处理方式；三者合起来仍不能禁止其他写者截断原对象。捕获 SIGBUS 也不会重新提供丢失的文件字节。无法控制这些写者时，应重新选择输入取得方式或设计协调协议；即便复制或 `pread`，也需处理读取期间变化，不能无条件称为一致快照。[fstat(2)](https://man7.org/linux/man-pages/man2/fstat.2.html)

游戏资源索引用“版本 + 偏移 + 长度”描述资源，映射拥有者验证它们后再生成本进程指针，便于应对 ASLR、跨进程不同基址和资源换代。纹理包预热与场景切换须同时考虑借用是否结束：调用 `munmap` 或丢弃内容前，先证明仍在执行的渲染/解析任务不会继续访问。C++ 可用 RAII 包装拥有者，但析构调用 `munmap` 本身不会等待外部悬挂视图。

## 6. 完整有限例：关闭 FD 后读取合法端点

这是 Linux、C11、单线程的教学程序，输入只有一个文件名。调用者必须掌握全部写者，保证打开后至 `munmap` 完成期间的普通文件内容和长度不变；程序没有加锁、封印或信号恢复机制，不能自己建立这个前提。第 9 节由仓外验证过程预先创建、运行时不再修改的极小文件满足此前提。

大小来自成功的 `fstat`：先拒绝非普通文件、空文件及负值，再验证可表示为 `size_t` 且不超过本例采用的 `PTRDIFF_MAX` 上限，最后才转换。这个上限服务于整文件 C 对象的使用方式，不是所有操作系统映射的普遍上限；在窄 ABI 上也可能先由 `open`/`fstat` 报可表示性错误。

```c
/* Linux C11, one thread, caller-owned immutable regular file.
 * fstat() does not prevent another process from modifying/truncating it.
 * This observes first/last bytes, not throughput or every page.
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    int fd = -1;
    int result = EXIT_FAILURE;
    size_t length = 0;
    unsigned char *view = MAP_FAILED;
    struct stat info;

    if (argc != 2) {
        fputs("usage: vm-read immutable-regular-file\n", stderr);
        return EXIT_FAILURE;
    }
    fd = open(argv[1], O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd == -1) { perror("open"); goto done; }
    if (fstat(fd, &info) == -1) { perror("fstat"); goto done; }
    if (!S_ISREG(info.st_mode) || info.st_size <= 0) {
        fputs("expected a nonempty regular file\n", stderr);
        goto done;
    }
    if ((uintmax_t)info.st_size > (uintmax_t)SIZE_MAX ||
        (uintmax_t)info.st_size > (uintmax_t)PTRDIFF_MAX) {
        fputs("file too large for this whole-file example\n", stderr);
        goto done;
    }
    length = (size_t)info.st_size;
    view = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
    if (view == MAP_FAILED) { perror("mmap"); goto done; }

    /* Linux: do not retry close(), including after EINTR. */
    const int close_result = close(fd);
    fd = -1;
    if (close_result == -1) { perror("close"); goto done; }

    const unsigned first = view[0];
    const unsigned last = view[length - 1];
    if (printf("bytes=%zu first=%u last=%u\n", length, first, last) < 0) {
        perror("printf");
        goto done;
    }
    if (fflush(stdout) == EOF) { perror("fflush"); goto done; }
    result = EXIT_SUCCESS;

done:
    if (view != MAP_FAILED && munmap(view, length) == -1) {
        perror("munmap");
        result = EXIT_FAILURE;
    }
    if (fd != -1 && close(fd) == -1) {
        perror("close");
        result = EXIT_FAILURE;
    }
    return result;
}
```

执行时资源状态如下：

- 未取得 FD 时 `fd=-1`，只有 `fd==-1` 表示打开失败，不能写成 `if (!fd)` 而漏掉 FD 0
- `mmap` 失败时保留 `MAP_FAILED`，不访问端点；已有 FD 由清理分支关闭
- `mmap` 成功后先关闭 FD，再读取 `[0]` 和 `[length-1]`。映射持有独立寿命，读取不再依赖原 FD 编号
- Linux 下 `close` 报错要诊断，但不要盲目重试，包括 EINTR；重试可能误关已复用的编号。这一做法不直接推广为所有 Unix/POSIX 版本的 EINTR 合同
- `printf` 即时失败或 `fflush(stdout)` 延迟刷新失败都走清理，只有输出刷新成功才设置成功结果。刷新成功不保证远端消费者已经收到或磁盘持久化；异常信号终止也不在普通返回路径保证内
- `munmap` 用原基址和原长度；如果清理调用报错，返回失败并诊断，不能宣称显式清理必然成功。程序不再使用该视图，进程结束也会解除进程映射

`O_NONBLOCK` 使误传 FIFO 时的只读打开不必等待写者，随后 `fstat` 会拒绝非普通文件；这里没有把它当成打开任意对象都不阻塞的承诺，更不能推出普通文件的 fault 是异步的。`O_CLOEXEC` 防止 FD 被意外带入 exec 后程序。没有用这两个标志替代输入不可变合同。[open(2)，O_NONBLOCK/O_CLOEXEC](https://man7.org/linux/man-pages/man2/open.2.html)

清理与输出检查依据 [close(2)，Linux 的错误处理](https://man7.org/linux/man-pages/man2/close.2.html) 和 [fflush(3)，输出缓冲及错误返回](https://man7.org/linux/man-pages/man3/fflush.3.html)。本例只读取两个端点，并非扫描、校验文件全部内容或测量吞吐。

## 7. 匿名 arena、预取和页面大小：收益都有条件

私有匿名映射适合管理大块 arena：地址范围先建立，实际读写再按需取得零内容和后备页。若对象仍有活跃数据，不能把 Linux `MADV_DONTNEED` 当成无损“释放冷页”。对私有匿名页，成功丢弃后后续访问会重新得到零填充内容；纸面上先写 X、再 DONTNEED、再读，不能期待 X 仍在。对文件支持的范围，后续访问可重新从后备取得内容，RSS 的变化也不等于全局文件缓存已消失。这些是类型相关语义，不能与 `POSIX_MADV_DONTNEED` 混为一谈。[madvise(2)，MADV_DONTNEED](https://man7.org/linux/man-pages/man2/madvise.2.html)

对顺序资源扫描，可以把 `MADV_SEQUENTIAL` 作为访问模式提示；对已知将用的热点，`MADV_WILLNEED` 或 `MAP_POPULATE` 可将部分工作提前。但请求预取/预填充不代表所有请求都已成功，也不保证数据永久驻留或后续零 fault。随机索引的无效预读可能增加 I/O 和内存压力；场景切换前预热可能降低首触等待，也可能挤出更有价值的数据。收益应比较预热成本、实际使用比例与请求长尾。[madvise(2)](https://man7.org/linux/man-pages/man2/madvise.2.html)、[mmap(2)，MAP_POPULATE](https://man7.org/linux/man-pages/man2/mmap.2.html)

大页可减少翻译项数量和页表开销，但可能增加内部碎片、分配/整理成本。透明大页与显式 hugetlb 不是同一开关；对照前记录实际页大小及策略，不预设固定 4 KiB/2 MiB。步长增大时若 dTLB miss 上升，应结合可用 PMU 事件、cycles 和延迟判断；减少 TLB miss 不等于减少 page fault。

NUMA 的新分配策略、任务/VMA 范围和文件 cache 的规则也不同，已有页不因线程绑核就自动全部迁到本地。按需分配的首触位置值得测量，但不能把“首次触碰”推广为所有共享文件页的放置定律。[NUMA Memory Policy，Scope](https://docs.kernel.org/6.12/admin-guide/mm/numa_memory_policy.html)

安全边界与这些性能策略独立：用最小读写执行权限，JIT 的 W^X 需按阶段管理；`PROT_NONE` guard page 可帮助暴露触碰，NX/ASLR 可限制执行或提高攻击难度，但都不替代偏移、长度和寿命校验。一个越过应用对象边界但仍落在可读页内的错误，不会因有 guard page 就必定被捕获。

## 8. 观测要回答具体问题

先确定统计对象、采样区间和工作量，再选择工具。以下是诊断入口及限制，**不是本轮已采集的数据**。

|问题|可用入口|读取结果时的限制|
|---|---|---|
|区域在哪里、权限与文件是什么？|`/proc/<pid>/maps`|是地址布局观察，不能证明应用对象边界和未来寿命|
|哪些映射占用驻留内存、共享如何分摊？|`smaps`、`smaps_rollup` 的 RSS/PSS 与脏页字段|RSS 计驻留，PSS 按共享比例分摊；二者都不是 commit。Private/Shared 统计不与 `MAP_PRIVATE/MAP_SHARED` 标志一一对应，COW 还可能形成匿名页|
|文件页面此刻是否驻留？|`mincore`|只是可能立即过时的驻留快照，不证明访问权限合法、页表/TLB 就绪、未来零 fault，也不能冻结 cache|
|这个区间发生多少缺页？|`getrusage` 差值；或 `perf stat -e page-faults,minor-faults,major-faults`|`RUSAGE_SELF` 是本进程所有线程的累计资源；差值仍会包含其他线程/活动。`RUSAGE_THREAD` 是 Linux 的调用线程口径；进程启动和测量边界也需说明|
|当前哪些匿名内存换出？|`status` 的 `VmSwap`|是当前相应匿名私有数据的 swap 存量，不含 shmem swap；不是历史换出次数，零值不能证明从未交换|
|是否出现内存压力、回收或限额事件？|PSI memory、`memory.events`/`.local`、`memory.stat`、系统 `/proc/vmstat`，必要时 I/O 统计|需区分全局、进程、cgroup 和子树口径。`memory.events` 通常层级累计，`.local` 为本层；`cgroup.events` 的 populated/frozen 是群组状态，不是内存压力计数|
|想看更低层页信息或热点？|`pagemap`；按需选择 perf record、eBPF/ftrace、NUMA/THP 统计|受权限、内核版本和工具可用性约束；受限 PFN 可能返回 0，不能据此断言页面不存在|

字段口径依据 [Linux 6.12 /proc，maps/smaps/status 及读取竞态](https://docs.kernel.org/6.12/filesystems/proc.html)、[mincore(2)](https://man7.org/linux/man-pages/man2/mincore.2.html)、[getrusage(2)](https://man7.org/linux/man-pages/man2/getrusage.2.html)、[cgroup v2，memory 与 events](https://docs.kernel.org/6.12/admin-guide/cgroup-v2.html)、[pagemap 的 PFN 权限](https://docs.kernel.org/6.12/admin-guide/mm/pagemap.html)。系统分页活动可借助 `sar -B` 等工具汇总；不能把不同工具同名近似字段直接合并，也不能把系统级活动全归因到一个请求。

例如发现查询 P99 上升且 major fault 增加，只能先提出“需要 I/O 的缺页可能参与等待”的假设；再对齐同一时间区间的 I/O、回收、CPU 和工作量证据。单独的 fault 计数没有给出具体设备，也不能排除锁等待和调度。`smaps` 等采样有开销，运行中多份快照也不形成一个原子全局快照。Windows 的诊断工具有独立语义，本篇没有用 ETW 支持 Linux 结论。

## 9. 本次有限实践与未覆盖路径

2026-10-06 从第 6 节唯一 C 围栏原样提取后运行，源码为 2099 bytes，SHA256 为 `dda5cd10acba2e0778e31ad9e676bcfbef26852aa31da24d328aee5f01fa3081`。实际环境：x86_64 Linux 6.18.44、GCC `(Debian 14.2.0-19) 14.2.0`、glibc `2.41-12+deb13u4`，`getconf PAGESIZE` 返回 4096，运行目录文件系统为 tmpfs。它验证的是本机普通文件映射与程序分支，不是物理存储读取。

构建使用 C11、`-Wall -Wextra -Wpedantic -Werror`，分别为 O0、O2，以及 O1 的 ASan+UBSan；三次编译均 exit 0、stdout/stderr 为空。将正文 C 原样保存为 `core-example.c` 后，可用下列等效相对路径命令复现构建；实际运行记录另保存了绝对路径、工具二进制指纹和逐次返回：

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O0 core-example.c -o vm-read-O0
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 core-example.c -o vm-read-O2
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined core-example.c -o vm-read-ASan-UBSan
```

每组都运行下表 7 个输入，共 21 次实际返回。文件由本次实践自行创建：`one.bin` 为 ASCII A，`two.bin` 为 ASCII AZ，`page-plus-one.bin` 为 A + 4095 个零字节 + Z（总计真实页大小加 1），`empty.bin` 为空文件；`directory` 是自建目录，`missing.bin` 确认不存在。创建完即不再修改，并在运行后复核文件长度与 SHA256 未变。表内 `\n` 明确表示输出末尾的一个换行，`空` 表示零字节输出。

|传入参数|实际 exit|实际 stdout|实际 stderr（LC_ALL=C）|
|---|---|---|---|
|无参数|1|空|`usage: vm-read immutable-regular-file\n`|
|`missing.bin`|1|空|`open: No such file or directory\n`|
|`empty.bin`|1|空|`expected a nonempty regular file\n`|
|`directory`|1|空|`expected a nonempty regular file\n`|
|`one.bin`|0|`bytes=1 first=65 last=65\n`|空|
|`two.bin`|0|`bytes=2 first=65 last=90\n`|空|
|`page-plus-one.bin`|0|`bytes=4097 first=65 last=90\n`|空|

调用方式为 `LC_ALL=C LANG=C ./vm-read-O0 one.bin`，另两组对应替换程序名并使用同一输入表；无参数行不带文件名。Sanitizer 组每次还设置 `ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` 与 `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`。这组输入未产生 sanitizer 诊断；已知执行环境不支持 LSan 的 ptrace 场景，因此未重做该探针，关闭 leak detection 意味着**没有泄漏检测覆盖**。单线程例不适用 TSan，未运行它。

1 字节和 2 字节正例说明本程序没有错误地把 length 限成页倍数；4097 字节例读取两个不同页中的合法端点，但没有扫描中间页。安全拒绝例检查参数、打开失败与非空普通文件约束。成功输出发生在 FD 关闭后的源码路径上，不等于另做了 syscall trace；没有记录 fault 次数或计时。fixture 刚写入，cache 状态未控制，故不标为冷缓存实验。

下面几项只按接口合同作纸面预期（PAPER_EXPECTED），不以代码中存在分支冒充实跑覆盖：`fstat` 失败/尺寸超限、`mmap` 失败、`close` 失败、`printf`/`fflush` 输出失败、`munmap` 失败及系统资源耗尽。FD 0 在源码中按有效 FD 处理，但本轮不另构造关闭标准输入的运行场景。

同样未执行 COW 写入、共享发布/写回、版本替换、多读者退役、匿名 DONTNEED、截断/SIGBUS、UAF、竞争、死锁、性能压力、OOM、断电恢复、Windows 或 UE 资源加载。未运行 perf/mincore/pagemap 采集，也没有修改 drop_caches、sysctl、cgroup、THP、NUMA 设置。第 2、3、4、5、7 节的追踪是机制推导，不能从端点读取成功推出它们均已实测。

## 10. 如何选择显式 I/O，以及以后怎样比较

资源数据必须完成同一份业务工作，才有选择依据：

|需要与代价|更值得研究的方向|
|---|---|
|稳定不可变索引，多次随机访问并希望直接读取字节|映射可能减少显式拷贝和调用管理，但 fault 的等待隐藏在普通访存里，地址和借出视图寿命必须受控|
|希望明确看到每个请求的错误、长度和完成时点|`read`/`pread` 或异步显式 I/O 更便于组织请求边界；仍须处理短读、文件变化和缓冲区寿命|
|文件巨大、32 位地址空间或映射数受限|窗口化或显式分块，承担窗口换代、校验和借用管理成本|
|一次性顺序扫描|比较缓冲读和映射实际完成扫描的总成本，不因 mmap 调用很快就宣布吞吐更高|
|大量并发请求需要批量与完成队列|进一步研究线程池 pread / io_uring；异步接口不会消除设备延迟和缺页，也不天然得到零拷贝|

`io_uring` 的队列、注册缓冲和完成协议属于[异步 I/O 主篇](03-io_uring与异步I-O.md)的扩展阅读，本节只比较显式请求完成与隐式 fault 的责任差异，不借邻篇数字宣称性能已经验证。映射失败的降级也应由上层明确选择，而不是在同一函数中隐藏两种完全不同的错误与寿命语义。

未来对照实验应使用相同文件内容、字节/索引查询、访问序列和结果校验：

1. 把打开/建映射、首次实际触碰、重复访问、释放分开记录，再给出覆盖完整工作的总时间。不能拿读完全部字节的 `read` 与只建立 VMA 的 `mmap` 比较
2. 保存文件大小、文件系统/挂载参数、内核、libc、编译选项、CPU、页大小、THP/NUMA 与 cgroup 条件；随机工作负载保存种子及访问分布
3. 缓存准备过程单独说明。**新进程或第一次运行不等于冷 cache**，生成文件本身也可能使数据在缓存里；第二遍仍可能遇到回收。没有控制并确认时，写“缓存状态未知”，不能靠轮次命名冷热
4. 顺序、随机和不同步长各有目的，步长按真实页大小解释；异步方案记录队列深度、完成批量与 CPU 使用。队列深度 1/4/16/64 可作为将来某组输入，而不是所有实现的固定验收值
5. 将 wall time、CPU 时间或 cycles、fault、RSS/PSS、必要 I/O/压力指标对应起来；请求延迟样本足够时报告中位数、P95/P99 和最大值，并写清样本与计时边界，能耗仅在有测量条件时加入
6. 在另行隔离设计的回收研究中，观察工作集、匿名/文件页比例、PSI、限额和长尾的关系；direct reclaim 的出现受多种条件影响，不存在本文已经找到的统一容量阈值

全局 `drop_caches` 会影响系统缓存和其他工作，也不自动生成可比较的“纯冷”环境；本篇不提供把它作为默认前置步骤的命令。[Linux 6.12 vm sysctl，drop_caches](https://docs.kernel.org/6.12/admin-guide/sysctl/vm.html#drop-caches)

游戏资源方案可进一步比较按需加载、预热不足和过量预读在场景切换首帧/长尾上的影响，同时校验旧映射是否仍被借用。这些是未来测量问题，不是用本文三个正例就能回答的容量或生产结论。

完成本文学习后，应能追踪无 fault/minor/major/非法访问的分叉；为映射给出字节边界和拥有者；解释某个计数究竟测到了谁、何时、什么范围；并把已运行输入、纸面错误路径和未来性能问题分别写清。做到这些，才有基础选择 mmap，而不只是在清单里勾选 API。

## 11. 来源范围与历史文本

本次静态基线是 Linux man-pages 6.19（所读 HTML 页脚的版本）、Linux 6.12 的固定版本文档，以及 Microsoft `VirtualAlloc` 的 reserve/commit 说明。关键定位是：mmap 的参数/PRIVATE/尾页/hugetlb/C 对象限制；page tables 的 MMU/TLB/fault；overcommit 的模式/计账；`/proc` 的 maps/smaps/status；getrusage 的 SELF/THREAD 和 fault 字段；mincore 的瞬时快照；madvise 的 DONTNEED；msync/fsync 的同步边界；close/open/fstat/fflush/rename/unlink/sysconf 的相应接口合同。文中在各条结论旁给出对应的一手入口。

这些阅读不等于逐行审过完整 Linux 源码、所有体系结构手册或整套 POSIX 标准。Open Group 的 POSIX.1-2024 mmap 官方页在准备阶段返回 HTTP 403，保留该未读边界，不声称直接核完标准；Linux 手册的 STANDARDS 标签也不能替代标准原文。实际运行内核与文档版本分别记录，不把 Linux 6.12 文档当成本机内核版本。版本发布和读者退役是依据接口推导的应用责任，不是内核自动完成的协议。

### 历史基线与原验证建议（原文保留）

以下是原有的 2026-08-20 基线、来源和验证建议，逐字保留供追溯。它们不证明当日或本次执行过所列工具、基准或平台验证；当前范围和实际结果以本篇前文为准。

> 验证入口：使用 perf、/proc、mincore 与 strace 复现缺页和 mmap 行为，记录延迟分位数。
> 知识基线：主流 x86-64/ARM64 分页、POSIX mmap、Linux 6.x 行为。
> 最后更新：2026-08-20。
> 来源：[Linux mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html)、[Linux pagemap](https://www.kernel.org/doc/Documentation/admin-guide/mm/pagemap.rst)。
> 验证入口：`/proc/<pid>/maps`、`smaps`、`mincore`、perf page-faults 与自编译 C 程序。
> 验证与基准：对匿名映射、文件映射和顺序/随机访问运行基准测试，记录缺页次数与 P99 延迟。

以下关联导航保留原有检索标签和说明，其中“异步批量 I/O 零拷贝”是历史导航描述，不是本篇对所有 io_uring 操作的保证。关联文章用于进一步学习，不代表它们的实现或性能结论已经由本文核验。

---

## 关联知识与工程落地

- **前置依赖**：
  - [01-进程线程虚拟内存与系统调用](01-进程线程虚拟内存与系统调用.md)：进程虚拟地址空间与分页模型。
- **存储栈与异步 I/O 结合**：
  - [03-文件系统与存储栈](03-文件系统与存储栈.md)：Page Cache 刷盘与直接 I/O。
  - [07-Linux系统编程/03-io_uring与异步I-O](03-io_uring与异步I-O.md)：异步批量 I/O 零拷贝。
  - [08-计算机体系结构与性能/05-虚拟化与硬件辅助隔离](../硬件体系结构与性能/05-虚拟化与硬件辅助隔离.md)：扩展页表（EPT/NPT）。
- **游戏引擎与服务端落地**：
  - [游戏知识/12-引擎源码分析/08-FMallocBinned与内存池源码分析](../../03-引擎架构与资源系统/对象模型与生命周期/07-容器与内存管理源码.md)：大页内存预分配与预热。
- **分类与领域入口**：
  - [06-操作系统 README](../../../00_Index/学习路线/编程与计算机基础.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/学习路线/编程与计算机基础.md)
