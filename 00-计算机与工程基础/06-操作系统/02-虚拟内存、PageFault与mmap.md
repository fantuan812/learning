# 02-虚拟内存、PageFault 与 mmap

> 知识成熟度：L2
> 验证入口：使用 perf、/proc、mincore 与 strace 复现缺页和 mmap 行为，记录延迟分位数。
> 知识基线：主流 x86-64/ARM64 分页、POSIX mmap、Linux 6.x 行为。
> 最后更新：2026-08-20。
> 来源：[Linux mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html)、[Linux pagemap](https://www.kernel.org/doc/Documentation/admin-guide/mm/pagemap.rst)。
> 验证入口：`/proc/<pid>/maps`、`smaps`、`mincore`、perf page-faults 与自编译 C 程序。
> 验证与基准：对匿名映射、文件映射和顺序/随机访问运行基准测试，记录缺页次数与 P99 延迟。

## 1. 地址空间

1. 虚拟地址空间隔离进程并提供稀疏映射。
2. 页表把虚拟页映射到物理页框。
3. TLB 缓存近期地址转换。
4. TLB miss 会触发页表遍历或软件填充。
5. 用户态和内核态权限由页表位控制。
6. NX 位阻止数据页执行。
7. ASLR 随机化代码、堆和栈位置。
8. guard page 捕获栈增长越界。
9. canonical address 约束有效地址位。
10. 大页减少 TLB 压力但增加内部碎片。

## 2. Page Fault

1. 缺页异常可能是合法按需分配。
2. 也可能是权限错误或野指针。
3. minor fault 不需要磁盘 IO。
4. major fault 需要从存储读取页面。
5. 首次触碰匿名页通常触发零页映射或分配。
6. 写时复制在写入时复制共享页面。
7. 文件映射页由页缓存支持。
8. 内核可预读相邻文件页。
9. 频繁 fault 会放大尾延迟。
10. `perf stat -e page-faults,major-faults` 可计数。
11. fault handler 期间线程可能睡眠。
12. 信号 SIGSEGV 表示不可恢复访问。
13. 用户态 fault 与内核态 fault 处理路径不同。
14. OOM killer 可能终止分配失败进程。
15. 锁内触发 IO fault 可能造成长尾。

## 3. mmap 语义

1. `mmap` 创建文件或匿名映射。
2. `MAP_PRIVATE` 使用写时复制。
3. `MAP_SHARED` 写入可回写并对其他映射可见。
4. `PROT_READ/WRITE/EXEC` 控制访问权限。
5. `MAP_POPULATE` 请求预填充页表。
6. `MAP_FIXED_NOREPLACE` 避免覆盖已有映射。
7. `munmap` 解除整页范围映射。
8. 解除后继续访问是未定义行为并可能 SIGSEGV。
9. 文件长度变化可能导致总线错误。
10. `msync` 控制脏页回写时机。
11. `madvise` 提供访问模式提示。
12. `MADV_DONTNEED` 允许回收匿名页。
13. `MADV_HUGEPAGE` 请求透明大页策略。
14. 映射偏移通常需页大小对齐。
15. 返回地址必须按 `MAP_FAILED` 检查。

## 4. Linux 观测

1. `/proc/pid/maps` 展示区域权限和文件。
2. `smaps` 展示 RSS、PSS、私有脏页。
3. `smaps_rollup` 提供汇总统计。
4. `pagemap` 可关联虚拟页与 PFN（权限受限）。
5. `vmstat` 观察 pgfault/pgmajfault。
6. `sar -B` 观察系统分页活动。
7. `perf record -e page-faults` 定位热点。
8. eBPF 可跟踪缺页入口和延迟。
9. ftrace 记录 mm_vmscan 事件。
10. cgroup memory.events 记录压力和 OOM。
11. PSI memory 展示内存阻塞时间。
12. NUMA 机器需观察节点本地性。
13. `numastat` 比较本地与远端访问。
14. THP 统计可从 `/sys/kernel/mm/transparent_hugepage` 查询。
15. 采样前固定 workload 和内核版本。

## 5. 工程模式

1. 大文件随机读可使用窗口化 mmap。
2. 映射窗口需限制最大 RSS。
3. 顺序扫描配合 `MADV_SEQUENTIAL`。
4. 一次性读取配合 `MADV_DONTNEED`。
5. IPC 共享内存必须设计版本和一致性协议。
6. 共享内存对象需处理崩溃遗留锁。
7. 环形缓冲区使用原子索引和缓存线填充。
8. 持久化映射需考虑崩溃一致性。
9. 日志写入要明确 msync 与 fsync 差异。
10. 映射权限遵循最小权限原则。
11. JIT 内存应采用 W^X 分阶段切换。
12. 映射地址不可作为跨进程稳定句柄。
13. 32 位进程需防止地址空间碎片。
14. 资源释放应在 RAII 析构中执行 munmap。
15. 映射失败要提供降级路径。

## 6. 验证程序

```c
int fd=open("data.bin",O_RDONLY); struct stat st; fstat(fd,&st);
void* p=mmap(NULL,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);
if(p==MAP_FAILED) perror("mmap");
volatile unsigned char x=((unsigned char*)p)[0]; (void)x;
munmap(p,st.st_size); close(fd);
```

## 7. 检查清单

1. 记录页大小、THP、NUMA 配置。
2. 分别统计 minor 和 major fault。
3. 测量冷缓存与热缓存。
4. 测量首触延迟和稳定吞吐。
5. 压测并发映射和解除映射。
6. 注入文件截断场景。
7. 检查权限错误与 SIGBUS。
8. 观察 RSS、PSS 和 cgroup 限额。
9. 比较 read、pread、mmap 三种方案。
10. 保存 perf、smaps 和内核版本证据。

## 8. 结论

虚拟内存把地址隔离、按需分配和文件缓存统一起来；mmap 不是自动更快的 IO，而是一种映射契约。必须以缺页、RSS、TLB 和尾延迟数据验证设计。

## 9. 案例：大文件索引

使用只读 `mmap` 映射索引文件，记录每个查询触发的 minor fault。

随机查询会产生较差局部性，预读可能反而增加内存压力。

顺序扫描可使用 `madvise(MADV_SEQUENTIAL)` 提示内核。

热点页可使用 `MADV_WILLNEED`，但必须验证预取收益。

文件被其他进程截断后，访问超出新长度的页面可能触发 SIGBUS。

生产代码应捕获信号并通过版本化文件或 RCU 替换避免截断竞态。

## 10. 实验：read 与 mmap

生成 1GB 文件，分别使用 `read`、`pread` 和 `mmap` 扫描。

测试顺序访问、随机访问和 4KB/2MB 步长。

记录 wall time、CPU cycles、minor/major fault、RSS 和 page cache 命中。

首次运行代表冷缓存，第二次运行代表热缓存，不能混为一谈。

在不同内存压力下重复，观察 mmap 的回收和重新缺页。

当工作集小于内存时，mmap 可能减少拷贝；工作集超过内存时，抖动可能更严重。

## 11. PageFault 诊断

minor fault 可以在不发起阻塞存储 I/O 的情况下完成，例如补建页表或命中已在内存中的 page cache；major fault 表示处理该缺页时需要等待 I/O，常见来源是文件页或交换区，未必就是本地物理磁盘。

使用 `perf stat -e page-faults,minor-faults,major-faults` 采样。

通过 `/proc/<pid>/smaps` 查看每个映射的 RSS、PSS 和权限。

检查 `VmSwap` 判断是否发生交换。

结合 iostat、/proc/vmstat 或 ETW，确认 major fault 的 I/O 来源及其是否由存储延迟主导；不要仅凭计数推断具体设备。

缺页高不一定是问题，需结合延迟和吞吐解释。

## 12. 共享映射与一致性

`MAP_SHARED` 的脏页最终写回文件，不能替代事务日志。

`msync(MS_SYNC)` 提供写回请求，但不等价于数据库级提交协议。

多进程更新同一页必须设计锁或原子协议。

使用文件锁时注意锁粒度和进程崩溃后的释放语义。

## 13. 匿名映射与内存池

`MAP_ANONYMOUS|MAP_PRIVATE` 适合大块 arena 分配。

首次写入触发 demand-zero page，读取可能共享零页。

`madvise(MADV_DONTNEED)` 可主动归还冷页，但会丢失匿名内容。

大页减少 TLB miss，却可能增加内部碎片和分配延迟。

实验时同时比较 4KB、透明大页和显式 huge page。

## 14. io_uring 对比

`io_uring` 通过 SQ/CQ 环形队列减少系统调用和线程切换。

它解决异步 IO 调度问题，不会消除缺页或存储设备延迟。

固定缓冲区可减少注册开销，但占用不可回收内存。

比较同步 pread、线程池 pread 和 io_uring 的 P99 延迟。

记录队列深度、完成批量大小和 CPU 利用率。

## 15. Benchmark 设计

固定文件大小、文件系统、挂载参数和内核版本。

分别设置队列深度 1、4、16、64。

控制随机种子并保存访问偏布。

每轮运行前明确是否 drop_caches，避免结果不可比。

报告平均值、中位数、P95、P99 和最大值。

同时记录能耗或 CPU 时间，防止只优化墙钟时间。

## 16. 常见错误

映射后立即 unlink 文件不会释放已映射页，直到最后一个引用消失。

忘记 `munmap` 会造成虚拟地址空间和 VMA 元数据增长。

映射长度必须按页对齐处理文件尾部。

指针跨进程共享通常无效，应使用偏移量而不是绝对地址。

fork 后写时复制会放大内存，父子进程应避免修改共享大页。

## 17. 验收清单

能解释 VMA、页表、TLB 和 page cache 的关系。

能用 perf 区分 minor fault 与 major fault。

能安全处理文件截断导致的 SIGBUS。

能设计 mmap 文件格式的版本和校验。

能根据访问模式选择 read、mmap 或 io_uring。

能用实验数据解释预读、回收和大页的收益与代价。

## 18. 内存回收实验

逐步扩大工作集，观察 direct reclaim 开始的阈值。

记录 PSI memory pressure，关联请求延迟长尾。

在 cgroup 限额下重复测试，比较全局内存和容器内存行为。

检查匿名页与文件页的比例，判断回收对象。

## 19. TLB 与访问步长

使用不同步长访问同一数组，比较 4KB 页与 2MB 页。

步长增大时，TLB miss 可能成为主要瓶颈。

通过 perf 记录 dTLB-load-misses 和 cycles。

不要把 TLB 优化误认为减少 page fault；二者发生在不同层次。

## 20. 安全边界

映射文件权限应遵循最小权限原则，敏感数据避免写入共享映射。

使用 `PROT_NONE` 保护 guard page 检测越界访问。

随机化地址布局提高攻击难度，但不能替代边界检查。

## 21. 最终验收

能够画出一次访问从虚拟地址到磁盘页的完整路径。

能够设计冷缓存与热缓存两套可复现实验。

能够解释 RSS、PSS、匿名页和文件页的差异。

能够给出 mmap 在游戏资源加载中的适用边界。

## 22. 游戏资源案例

将只读纹理包映射到地址空间，按需触发页面加载。

资源索引使用偏移量，避免 ASLR 导致的绝对指针失效。

场景切换前可按可见性预热热点页。

切换完成后对冷资源调用 MADV_DONTNEED，降低工作集。

压测时模拟多个场景并发加载，记录首帧和卡顿 P99。

比较预读过量、预读不足和按需加载三种策略。

## 23. 证据保存

保存 `/proc/meminfo`、`smaps_rollup` 和 cgroup.events 快照。

记录 perf 命令、采样频率、内核版本和 CPU 型号。

将原始数据与脚本一同归档，支持他人复现。

每次结论都应关联具体指标和采样证据。

异常结果先复核实验环境，再调整实现。

Benchmark 结果只在同一硬件与内核条件下横向比较。
