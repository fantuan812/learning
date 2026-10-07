---
type: Mechanism
title: "编译器优化与 GPU 异构"
status: stable
verified: []
maturity: L2
updated: 2026-10-07
sources:
  - id: llvm-vectorizers-18
    title: "LLVM 18.1.8 Auto-Vectorization"
    resource: "https://releases.llvm.org/18.1.8/docs/Vectorizers.html"
  - id: clang-users-18
    title: "Clang 18.1.8 Users Manual"
    resource: "https://releases.llvm.org/18.1.8/tools/clang/docs/UsersManual.html"
  - id: clang-extensions-18
    title: "Clang 18.1.8 __builtin_assume"
    resource: "https://releases.llvm.org/18.1.8/tools/clang/docs/LanguageExtensions.html#builtin-assume"
  - id: clang-thinlto-18
    title: "Clang 18.1.8 ThinLTO"
    resource: "https://releases.llvm.org/18.1.8/tools/clang/docs/ThinLTO.html"
  - id: clang-command-18
    title: "Clang 18.1.8 Command Guide"
    resource: "https://releases.llvm.org/18.1.8/tools/clang/docs/CommandGuide/clang.html"
  - id: c11-n1570
    title: "C11 committee draft N1570, 6.7.3.1"
    resource: "https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf"
  - id: cpp20-n4861
    title: "C++20 working draft N4861, stmt.for, fixed revision"
    resource: "https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/statements.tex#L565-L615"
  - id: cuda-1263-programming
    title: "CUDA 12.6.3 Programming Guide, selected sections"
    resource: "https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-programming-guide/index.html"
  - id: cuda-1263-best-practices
    title: "CUDA 12.6.3 Best Practices Guide, selected sections"
    resource: "https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-best-practices-guide/index.html"
  - id: cuda-1263-sync
    title: "CUDA Runtime API 12.6.3, API synchronization behavior"
    resource: "https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/api-sync-behavior.html"
  - id: cuda-1263-error
    title: "CUDA Runtime API 12.6.3, Error Handling"
    resource: "https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/group__CUDART__ERROR.html"
  - id: cuda-1263-device
    title: "CUDA Runtime API 12.6.3, Device Management"
    resource: "https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/group__CUDART__DEVICE.html"
  - id: cuda-1263-memory
    title: "CUDA Runtime API 12.6.3, Memory Management"
    resource: "https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/group__CUDART__MEMORY.html"
  - id: cuda-1263-types
    title: "CUDA Runtime API 12.6.3, cudaErrorIllegalAddress"
    resource: "https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/group__CUDART__TYPES.html"
---

# 编译器优化与 GPU 异构

> 知识成熟度：L2。本文解释优化合法性、编译决策与 GPU 工作生命周期，并据固定一手资料作静态核对；没有编译、运行或性能测量证据，`verified` 保持为空。
> 知识基线：LLVM/Clang 18.1.8、NVIDIA CUDA 12.6.3 指定章节；C11 委员会草案 N1570 §6.7.3.1、C++20 工作草案 N4861 [stmt.for]。不是所有工具链、GPU 或语言方言的通用保证。
> 最后更新：2026-10-07。七组 `PAPER_EXPECTED` 是在各自输入、单位和假设下可手算的预期，不是采集结果。本文未核验本机编译器、GPU、驱动或设备能力；未核验不等于硬件不存在。

一段循环经过向量化后，为什么可能更快、保持不变，甚至给出错误结果？先要区分三个问题：转换是否保留程序承诺的结果，目标机器是否值得采用这种转换，这次业务工作是否实际受益。把工作移到 GPU 又多出一条责任链：提交操作后，谁还在访问数据，何时可以读取结果，何时可以释放资源。本文沿这两条链讲清楚选择依据。

## 1. 先定义相同的工作，再讨论优化

比较 CPU 与 GPU 前，先固定输入范围、输出含义、数据初始驻留位置和完成终点。例如“输入在 host，算完后 host 可使用全部输出”包含必要的传输与完成等待；只量 kernel 不等于量完这一工作。如果准备长期复用 device 上的中间结果，应该另定义整个流水线的同等 CPU 工作，再分摊传输，而不是删掉本来必须返回 host 的输出。

数值规则也属于工作合同。输入是否允许 NaN/Inf、有符号零是否有意义、归约是否允许改变加法次序，都影响转换是否合法。实数的结合律不自动适用于有限精度浮点；容差应由参考算法、输入规模和误差目标确定。Clang 18.1.8 的 `-ffast-math` 允许可能有损的变换，并包含不保留 NaN/Inf 和有符号零等假设。依赖有限性检测的验证代码不能在未说明的情况下套用这些假设。[Clang 浮点选项](https://releases.llvm.org/18.1.8/tools/clang/docs/UsersManual.html#cmdoption-ffast-math)

### 1.1 P1：别名怎样改变跨迭代依赖

**PAPER_EXPECTED。** 本例是 C11 的 SAXPY：`y[i] = a*x[i] + y[i]`。以下是完整的函数源 `saxpy.c`，没有 `main`，未编译、未运行。函数调用合同是 `0 <= n <= 1000`；非空时 `x` 至少有 n 个已初始化可读 float，`y` 至少有 n 个已初始化可读写 float，访问有效且满足 restrict 约束；空输入不访问数组。纸面输入限小整数，使下述乘加及结果精确可表示，不据此承诺任意浮点值逐位一致。

```c
void saxpy(float *restrict y, const float *restrict x, float a, int n) {
    for (int i = 0; i < n; ++i) {
        y[i] = a * x[i] + y[i];
    }
}
```

取 `a=2, n=3`，两个有效、非重叠数组 `x=[1,2,3]`、`y=[2,3,4]`。每次迭代只写自己的 `y[i]`，后一次不读取前一次改写的对象；结果依次是 `[4,7,10]`。在这些前提下没有跨迭代读写依赖，可以作为向量化候选。编译器仍可能因目标成本或循环太短不采用它。

C11 restrict 约束的是一次关联块执行中的**访问关系**：通过基于该限定指针的左值访问某对象，且该对象在这次执行中被修改时，其他访问也必须满足同一 based-on 约束及相应类型要求。它不是“所有指针数值都不同”或“任何两块内存永不能重叠”。N1570 的例 3 允许两个限定输入指针共同读取未修改数组；编译器也可不利用 restrict 提供的别名信息。调用者必须对真实访问负责。[N1570 §6.7.3.1，段 1–10](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)

反例先把函数中两个 `restrict` 都移除，保留同样的顺序循环。令 `buffer=[1,2,3,4]`，`x=&buffer[0]`、`y=&buffer[1]`、`a=2, n=3`，所有访问都在数组内：

| 迭代 i | 本次读取 x[i] | 本次读取 y[i] | 写入 y[i] | 此后 buffer |
| --- | ---: | ---: | ---: | --- |
| 0 | 1 | 2 | 4 | [1,4,3,4] |
| 1 | 4 | 3 | 11 | [1,4,11,4] |
| 2 | 11 | 4 | 26 | [1,4,11,26] |

如果把读取全部提前到原始快照，结果会是 `[1,4,7,10]`，与此顺序合同不同。依赖来自前一次写入成为后一次输入，不是来自循环的语法形状。这个合法重叠调用可走保留顺序的实现；若编译器无法静态判定别名，也可能生成运行时区间检查，让不相交调用走向量分支、重叠调用走顺序分支。移除 restrict 不意味着必然无法向量化。[LLVM 运行时指针检查](https://releases.llvm.org/18.1.8/docs/Vectorizers.html#runtime-checks-of-pointers)

把同一重叠调用交给上面的 restrict 函数会违反本例的访问约束。这是应拒绝的调用，不存在本文可保证的“UB 输出”；不能用一个虚假的承诺消灭真实依赖。C11 `restrict` 也不是标准 C++ 关键字，本篇不把它搬进 CUDA C++ 例子，也不混用未核对的 C++ 扩展。

### 1.2 P2：检查建立事实，assume 使用事实

**PAPER_EXPECTED。** 此处只指 Clang 18.1.8 扩展 `__builtin_assume`，不是通用标准关键字，也不是由 N4861 支持的 C++23 `[[assume]]` 教程。以下完整小函数可在支持该扩展的 C 编译环境中作为源输入；未编译、未运行：

```c
int positive_count(int n) {
    if (n <= 0) {
        return 0;  /* 本例定义：拒绝非正计数 */
    }
    __builtin_assume(n > 0);
    return n;
}
```

| 输入 | guard 是否拒绝 | assume 是否可达 | PAPER_EXPECTED |
| ---: | --- | --- | --- |
| 0 | 是 | 否 | 返回 0，不进入需要正数的后续工作 |
| 3 | 否 | 是 | n 未改变，真不变量成立，返回 3；不保证有额外优化 |

assume 的参数**不求值**，它把条件作为优化器可依赖的不变量；执行中违反条件是未定义行为。因此用它替换 guard 不会得到受控拒绝，写 `__builtin_assume(++n > 0)` 也不会把 n 加一。要改变 n，应先在定义良好的范围内用普通语句完成操作，再表达真实且无副作用的条件。[Clang 18.1.8 `__builtin_assume`](https://releases.llvm.org/18.1.8/tools/clang/docs/LanguageExtensions.html#builtin-assume)

## 2. 编译器如何从合法转换走到目标产物

前端先按选定语言方言检查声明、类型和语句，生成带有相应语义的中间表示 IR；错误程序不会因为换成 GPU 或开高优化级别而变合法。中端在这些语义约束下传播常量、消去无用计算、内联和变换循环。后端再结合目标指令与资源约束，进行指令选择、寄存器分配和调度。它们是理解交接的模型，不要求实现严格分成三个独立程序；构建、目标文件与装载的职责见[编译链接与 ABI 全流程](../编译链接与ABI/01-编译链接与ABI全流程.md)。

以 P1 为例：别名信息不足时，变换必须保留可能的顺序依赖，或为合法快速路径增加运行时检查；证明可并行后，代价模型还要估计检查成本、向量宽度、尾部处理和目标可用操作。未知对齐不等于必不能向量化，目标可能支持非对齐操作，也可能选择另一策略。循环向量器把相邻迭代合成宽操作；SLP 向量器则寻找可合并的相似独立标量操作。它们不是同一个开关。[LLVM 18.1.8 Vectorizers：Loop、SLP、代价模型与尾部](https://releases.llvm.org/18.1.8/docs/Vectorizers.html)

CPU SIMD 的连续访问、归约和尾部都要同时满足依赖与数值合同。数据布局会影响加载方式，归约可能需要跨 lane 合并，但生成更宽指令不等于提高端到端吞吐。布局、缓存和带宽如何制约供给，沿[处理器存储层次与性能工程](01-处理器存储层次与性能工程.md)继续；寄存器与调用边界沿[ISA、汇编与调用约定](../编译链接与ABI/03-ISA汇编与调用约定.md)继续。本篇不重做这两篇的缓存、NUMA、PMU 或 ABI 实验。

### 2.1 报告、产物与收益分别回答什么

| 证据层 | 能回答的问题 | 不能由它直接推出的结论 |
| --- | --- | --- |
| 语义与输入检查 | 某种转换是否保留这个工作合同 | 优化器必然选择它 |
| pass remark | 支持报告的某个 pass 做了什么、为什么未做 | 全部 pass 的完整记录、任何加速比 |
| 汇编或反汇编 | 这份具体构建生成了什么目标代码 | 实际输入走哪条版本化分支、整体更快 |
| 正确性与有边界的测量 | 同一任务在该配置下是否正确、是否受益 | 其他硬件、规模或部署必有同样结果 |

接续 P2 的**纸面证据卡**：若取得 loop-vectorize 成功 remark，只能进入产物检查；若取得 missed/analysis，先定位该循环的具体阻碍，核依赖与输入，不宣布整个编译器“未优化”；若产物里有向量指令，还须验证实际路径、正确性和完整工作量，才有资格填写加速比。这里没有实际取得上述任何报告或产物。[Clang 优化报告的覆盖限制](https://releases.llvm.org/18.1.8/tools/clang/docs/UsersManual.html#options-to-emit-optimization-reports)

### 2.2 源输入完整的未来静态对照

以下配方只以 1.1 节完整 `saxpy.c` 为输入，限定支持 x86 后端的同一 Clang 18.1.8 安装、`x86_64-unknown-linux-gnu` 目标与 `x86-64` 指令基线；目标选择不表示本机已具备该环境。两组均为 C11、`-O2`，沿用同一浮点设置、不启用 fast-math，仅改变 loop/SLP 自动向量化开关。`-S` 生成汇编而不链接，所以不需要虚构 `main` 或缺失的 `bench.c`。**本轮未执行这些命令。**

```bash
clang -std=c11 -O2 -target x86_64-unknown-linux-gnu -march=x86-64 \
  -S -Rpass=loop-vectorize \
  -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize \
  -fsave-optimization-record saxpy.c -o saxpy.auto.s
clang -std=c11 -O2 -target x86_64-unknown-linux-gnu -march=x86-64 \
  -S -Rpass=loop-vectorize \
  -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize \
  -fsave-optimization-record -fno-vectorize -fno-slp-vectorize \
  saxpy.c -o saxpy.no-loop-slp.s
```

第二组叫“关闭 loop/SLP 自动向量化对照”，不叫“绝无 SIMD 的标量二进制”。标量浮点也可能用向量寄存器或指令编码，库与其他变换又是另外的范围。`-O2` 本身不保证 scalar，`-O3` 也只是允许更多可能增加编译时间或代码尺寸的优化尝试，不保证运行更快。[Clang 优化级别](https://releases.llvm.org/18.1.8/tools/clang/docs/CommandGuide/clang.html#code-generation-options)

报告可能成功，也可能因代价或合法性被拒绝，不能预先填写固定的 `vectorized loop` 输出。保存原始诊断与 optimization record 后，可在已有兼容工具时用 opt-viewer 阅读；它是查看途径，不是新的正确性判定。如果另加 x86 目标的 Intel 汇编语法选项，必须标明其目标适用性，不能作为所有平台配方。

若要继续做 CPU 性能对照，先提供完整 runner：初始化、合法输入、参考结果、实际工作消费、计时终点和退出状态都要闭合。当前函数与两条命令只承担代码生成观察用途。不能计时一个无可观察用途而被删除的循环，也不能把 `O2` 对 `O3+native` 的多变量变化归因于某一个 flag。

## 3. 内联、LTO 与 PGO 改变的是优化信息

内联把被调用函数的操作放到调用处，使调用参数的常量、范围与控制流有机会继续传播。例如调用者传入固定系数，看到函数体之后可能消去通用分支；省去一次调用只是收益来源之一。复制函数体也会增加代码尺寸和资源压力，因此“调用次数多”仍不构成无条件内联指令，需同时看产物和实际工作。

跨翻译单元时，单独编译常看不到另一个模块的函数体。LTO 让优化使用参与链接的中间表示扩大分析范围；ThinLTO 在编译阶段生成 bitcode 和紧凑模块摘要，链接时合并摘要索引并作全局分析，随后在各模块的并行后端完成函数导入与优化。摘要提供选择信息，导入才让需要的函数内容在后端可见，最终仍由合法性与成本决定变换。编译和链接输入必须匹配相应模式，链接器也须支持它；只在最后命令附一个 flag 不能替此前缺失的信息补出函数体。[Clang 18.1.8 ThinLTO：Introduction、Basic usage](https://releases.llvm.org/18.1.8/tools/clang/docs/ThinLTO.html)

PGO 提供的是工作负载信息。例如哪个分支常走、哪个函数更热，可影响基本块布局与内联取舍。插桩路线依次是：以匹配的生成选项编译和链接 → 用有代表性的输入运行 → 用 `llvm-profdata merge` 合并并转换原始资料 → 用匹配的 profile-use 选项重建。即使只有一份 raw profile，格式转换这一步仍有意义。采样 profile 与插桩 profile 不能任意混用；源码变化导致资料不再可用时，应检查诊断并重新判断。[Clang PGO 与插桩流程](https://releases.llvm.org/18.1.8/tools/clang/docs/UsersManual.html#profile-guided-optimization)

如果 profile 只覆盖小批量常见请求，少见的大批量路径可能被当成不重要；这是训练输入影响布局选择的机制，不是“有真实 profile 就一定更快”。本篇没有采集或使用 profile。构建、ODR 和 ABI 的限制仍属于前述编译链接主篇，LTO 不赋予程序违反语言合同的权利。

## 4. CUDA 向量加法：从输入到唯一成功终点

下面使用 NVIDIA CUDA 12.6.3 的 host/device 模型。kernel 按 grid、block、thread 组织，host 发出 launch 不等于所有线程已经写完输出。为了把错误、寿命和完成性讲清，本文交付**有界 kernel、宿主接入合同、完整成功轨迹和逐失败状态表**，不是可直接运行的完整 `main`，也没有隐藏的恢复函数或空接口。

### 4.1 P3：输入、语句边界和尾部地址

**PAPER_EXPECTED。** 输入 N 是先经宿主检查的整数，取 `0..1000`；非空选择 B=4 以便手算，宿主须确认这一启动配置在目标上合法，这不是调优建议或本机观察。计算 bytes 前先确认 `N <= SIZE_MAX / sizeof(float)`，随后以 `size_t(N) * sizeof(float)` 计算；N=0 直接走空工作终点，跳过分配、复制、launch 和数组读取。非空的 grid 为 `N/B + (N%B != 0)`，不依赖可能溢出的 `N+B-1`；本域 grid 最大 250。

以下是 CUDA C++ 源文件 `vector_add_kernel.cu` 的**完整 kernel 定义**，依赖 CUDA Runtime 头，未编译、未运行；宿主调用与失败控制不包含在此文件中。a、b、c 必须为足够容纳 N 个 float 的有效 device 数组，输入已完成必要发布，c 不与输入发生会破坏本例独立性的重叠，且没有并发写者。N 已由宿主限制为上述域，不给 kernel 添加 C11 restrict 承诺。

```cuda
#include <cuda_runtime.h>

__global__ void add_bounded(const float* a, const float* b,
                            float* c, unsigned int n) {
    const unsigned long long i =
        static_cast<unsigned long long>(blockIdx.x) *
        static_cast<unsigned long long>(blockDim.x) +
        static_cast<unsigned long long>(threadIdx.x);
    if (i < static_cast<unsigned long long>(n)) {
        c[i] = a[i] + b[i];
    }
}
```

乘法的两个操作数先扩宽，再做乘加；不能先以窄类型相乘，溢出后才转换。此处只使用一维 grid/block，合法启动域中的最大 i 为 999；N 非 B 的倍数时，最后一块生成的尾部 i 可能超出 N，但 `i<n` 把这些访问挡住。换成更大的域、多维索引或 grid-stride loop，必须重新证明乘加和范围，不能直接继承这个小域的保证。

宿主取得 ha、hb、hc 后，输入初始化必须让两个赋值都处于循环体内。下面仅是 CUDA C++ 宿主语句节选，ha/hb 为已成功分配的 N 元素数组，N 为已检查的非空输入：

```cpp
for (unsigned int i = 0; i < N; ++i) {
    ha[i] = static_cast<float>(i);
    hb[i] = 1.0f;
}
```

原例 `for (int i=0; i<n; ++i) ha[i]=float(i); hb[i]=1.0f;` 的循环体只有第一个赋值，第二条语句中的 i 已超出 for 初始化声明的作用域。在原完整文本中可静态证明其不合法；本文没有取得过 nvcc 对它的诊断。[N4861 [stmt.for]，固定源码 565–615 行](https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/statements.tex#L565-L615)

| 输入/状态 | 配置或判定 | PAPER_EXPECTED |
| --- | --- | --- |
| N=0 | 不发零网格 launch | 空结果，无资源，无元素读取 |
| N=1，B=4 | grid=1，i=0..3 | 仅 i=0 写入，输出 [1] |
| N=5，B=4 | grid=2，i=0..7 | i=0..4 各写一次，5..7 不访问；输出 [1,2,3,4,5] |
| N=1000，B=4 | grid=250 | 最后一个合法 i=999，预期 c[999]=1000 |
| N=1001 或负输入 | 宿主域检查拒绝 | 不先转换负值为无符号数，不分配或提交工作 |

hc 在成功 D2H 回读前不消费。回读成功后检查**全部 N 项**：先检查每项有限，再要求 `hc[i] == float(i+1)`。本域的整数及加一结果精确可表示，故这里可采用等值比较；不是把统一 `1e-5` 容差推广到一般浮点任务。N=5 若得到 `[1,2,99,4,5]`，首尾检查会误放行，全量检查在 i=2 报 got=99、expected=3；若该项为 NaN，则有限性检查失败。只判断 `abs(error)>tol` 可能遗漏 NaN，校验器的浮点编译合同也必须保留该检测。[CUDA 正确性与参考比较](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-best-practices-guide/index.html#verification)

### 4.2 P4：取得资源不等于可以释放资源

**PAPER_EXPECTED。** 基本例使用普通 `new (std::nothrow) float[N]` 取得的 pageable host 数组，逐次检查是否为 null；用 `cudaMalloc` 取得 device 数组，逐次直接检查 `cudaError_t`，仅在成功后登记所有权。指针初始为空，失败调用的输出不当成有效资源使用。三组输入/输出没有共享写者；host 分配全部完成后才进入 device 分配，device 分配全部完成后才提交 copy。

宿主接入前提是一个已隔离的单 host 线程教学任务：没有其他线程、库或 stream 向同一上下文提交旧工作，Runtime 和当前设备适合该任务，既有错误已按宿主协议处理，相关资源状态有效。**这些是接入前提，不是本轮核验事实，也不能由一次 `cudaGetLastError()` 返回成功建立。** 所有基本 copy 与 launch 使用该线程同一 default stream；本例没有跨 stream 并行，后续完成边界用 host 侧 `cudaDeviceSynchronize()`。

每个资源依次可能处于：未取得 → 已取得且未发布使用 → 访问可能在途 → 全部相关访问已成功完成 → 已成功释放。失败后还可能进入“状态未知”。host 与 device 缓冲要分别登记；不能用“有指针”代替访问已结束的证明。

正常释放配对为 `new[]/delete[]`、`malloc/free`、`cudaMalloc/cudaFree`。如果未来改用 `cudaMallocHost` 或 `cudaHostAlloc`，对应 `cudaFreeHost`；给已有 host 内存做 `cudaHostRegister`，则先在合法完成条件下检查 `cudaHostUnregister` 成功，再由原分配者释放内存，不能改用 `cudaFreeHost`。只给已成功取得且尚未成功释放的资源执行配对操作。[Runtime Memory Management：cudaFree、cudaFreeHost、cudaHostUnregister](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/group__CUDART__MEMORY.html)

### 4.3 N=5 的完整成功轨迹

下表所有成功状态均为**假设每步直接检查通过后的推导**，不是实际 API 日志。H 表示已取得的 host 集合 `{ha,hb,hc}`，D 表示 device 集合 `{da,db,dc}`。表中一次包含多个同类操作时仍逐个检查，前一项失败就不执行后一项。

| 顺序 | 具体操作与通过条件 | 状态为何变化 |
| --- | --- | --- |
| 1 | 检查 N=5、B=4、bytes 可表示与接入前提 | 非空工作成立，尚无资源或提交 |
| 2 | 依次分配 ha、hb、hc，各自非 null；完整执行输入初始化 | H 全取得，a=[0,1,2,3,4]、b=[1,1,1,1,1]；hc 尚不能读 |
| 3 | 依次 `cudaMalloc` 取得 da、db、dc，每次为 `cudaSuccess` | D 全取得，尚未发布使用 |
| 4 | `cudaMemcpy(da,ha,bytes,cudaMemcpyHostToDevice)` 成功；随后 db←hb 的同方向 copy 成功 | 向同一 stream 发布输入；pageable H2D 返回可能只证明 staging 完成，不能据此释放全部 device 资源 |
| 5 | `add_bounded<<<2,4>>>(da,db,dc,5)`；立即检查 `cudaPeekAtLastError()` 为成功 | 该观察点无可见失败，kernel 仍可能在执行；c 不可消费 |
| 6 | 直接检查 `cudaDeviceSynchronize()` 返回成功 | 所有前序相关 device 任务已完成，包括输入依赖和 kernel；这仍不证明数学结果正确 |
| 7 | `cudaMemcpy(hc,dc,bytes,cudaMemcpyDeviceToHost)` 返回成功 | 该回读已完成，hc 才成为可校验输出 |
| 8 | 全 N 项有限且等于 [1,2,3,4,5] | 本输入满足数值合同，相关访问已成功完成 |
| 9 | 在仍合法的 Runtime/资源状态下依次 `cudaFree(dc)`、`cudaFree(db)`、`cudaFree(da)`，逐项成功才登记已释放；然后 `delete[] hc`、`delete[] hb`、`delete[] ha` | 每次 CUDA 清理均直接检查；host 配对 delete[] 不返回 CUDA 状态；最终 H、D 均为空 |
| 10 | 仅当前述条件全成立时给出一次成功摘要，例如“checked=5, result=correct, cleanup=complete”，若宿主为独立程序才映射 exit 0 | 唯一非空成功终点；没有实际 stdout 或退出码证据 |

N=0 直接满足空结果、无相关访问和无待清理资源，汇入同一个成功判定，摘要明确“empty”。初始化、输入、配置或任何非空步骤失败都不能偷走这条成功出口。

### 4.4 每个失败点能做什么、必须在哪里停止

只分三类处理：**尚未发布任何相关使用**、**全部相关访问已成功完成**、**完成性或设备状态未知**。前两类才可能在仍合法的资源与 Runtime 条件下走匹配清理；仅仅“逆序”不是清理安全性的证明。

| 失败观察点 | 已取得/已发布事实 | 允许的后续与终点 | 必须跳过或禁止 |
| --- | --- | --- | --- |
| 输入非法或配置前提未建立 | 未取得、未发布 | 拒绝输入或停在接入检查，非成功 | 分配、copy、launch、结果读取 |
| ha/hb/hc 任一分配失败 | H 仅含此前成功取得项；D 空；没有相关 GPU 使用 | 保存分配阶段；配对 delete[] 已取得 H，失败终点 | 访问失败指针，初始化未取得数组，全部 CUDA 工作 |
| da/db/dc 任一分配失败 | H 已齐；D 仅含此前成功项；尚无 copy/kernel | 保存首错及拥有集合；H 从未发布，可配对释放；仅当具体错误后的 Runtime/资源合同仍合法时，逐项尝试释放已获 D 并检查结果；否则 D 交宿主处理 | 把失败输出登记为拥有；继续分配、H2D、launch、校验；声称必能释放全部 D |
| 第一次或第二次 H2D 失败 | H、D 已齐；相关 copy 已调用，完成性或设备状态可能未知 | 记录哪次 copy、直接返回码和已发布集合，进入下面的宿主接入门 | launch、D2H、读 hc、依赖提交与性能计时；盲 free、host 析构或复用 |
| launch 后 Get/Peek 报错 | 先前 H2D 可能仍有访问；不能因 launch 错误就证明它们退场 | 保存“launch 后观察点”及首错，进入接入门 | 把错误位置直接当根因；假定没有 kernel 就没有旧 copy；自动清理 |
| `cudaDeviceSynchronize` 失败 | 前序相关任务的完成性或设备状态未获本合同所需证明 | 保存返回码、阶段和资源状态，进入接入门 | 继续 D2H、消费、计时、盲重试或 reset；普通 return 导致 host 缓冲隐式析构 |
| D2H 失败 | 之前同步已成功，但新的 copy 的完成性/结果未知 | 保存该观察点；hc 不作为结果，进入接入门 | 从旧同步推断新 copy 已完成；读 hc、自动释放参与访问的缓冲 |
| 回读成功但 i=2 得到 99 或 NaN | 全部相关访问已成功完成，数值错误 | 记录失败下标/参考值；在合法 Runtime/资源条件下逐项匹配清理，最终仍为失败 | 以 API 成功、首尾正确或 checksum 掩盖全量不符；记录性能成功 |
| 校验已通过，但某 `cudaFree` 失败 | 此前访问已完成；仅此前成功释放项可标已释放，当前释放状态不确定 | 记录清理错误；不无条件继续 CUDA 清理，未决资源交接入门；保留既有完成证据，按宿主的具体状态协议处置剩余资源 | 把指针清空当释放证明、二次 free、盲重试、最终成功 |

首个错误应另存为首因记录；只在条件允许时产生的后续清理错误作为次因追加，不能覆盖它。诊断至少包含阶段、直接返回码、已取得集合、已发布访问和最后一个成功完成边界。某个同步或 copy 返回错误，可能是在转报旧异步失败；报错位置和根因位置并不相同。[Runtime Error Handling](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/group__CUDART__ERROR.html)、[cudaDeviceSynchronize](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/group__CUDART__DEVICE.html)

**未知状态的宿主接入门：** 停止后续依赖提交、输出消费和性能计时，并把仍持有的缓冲及状态交给一个能按具体 Runtime 版本、错误类型和完成/终止协议负责的宿主。交接前，可能在途的 host 内存不得因普通 return、异常展开或作用域结束而析构；也不得释放或复用可能被访问的 device 内存。宿主必须先证明相关访问均已合法退场且资源仍可操作，或采用经单独确认的不可继续终止方案，才能处置未决资源。本文没有实现这种通用宿主，没有证明任意错误后的安全释放全序，因此状态表停在这个明确失败终点，**不宣称已经回收全部资源**。准备可嵌入的生产函数时，这一接入条件必须先落实，不能用一个空的 `handle_error()` 替代。

`cudaGetLastError()` 会读取并清除当前 host 线程、对应 Runtime 实例的错误状态；`cudaPeekAtLastError()` 不清除。二者成功都不是 kernel 完成证据，清状态也不会修复设备。既有错误必须先记录与处理，不能静默清掉制造成功。`cudaFree` 文档对普通分配说的是“可能”隐式同步，对 stream-ordered allocator 的指针另有无隐式同步规定，不能把它当任意失败后的屏障；`cudaStreamDestroy` 也可能在工作未结束时返回。reset 不被本文当作恢复证明。对明确的 `cudaErrorIllegalAddress`，12.6.3 文档说明进程已不一致，继续使用 CUDA 需要终止并重新启动进程；这只支持该错误的 fatal 边界，不支持把所有错误一概归类或承诺原地 reset 修好。[cudaFree 合同](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/group__CUDART__MEMORY.html)、[cudaErrorIllegalAddress](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/group__CUDART__TYPES.html)

## 5. 复制、stream 与 host 寿命：重叠前先画依赖

“同步 API”和“异步 API”不能只凭名称判定。CUDA 12.6.3 Runtime §2 按参数与方向给合同；它比 Best Practices 面向教学的简写更细。本例的同步 `cudaMemcpy` 通过 default stream 发出，方向指针必须匹配，不把 host/device 指针随意互换。[API synchronization behavior](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/api-sync-behavior.html)

| 同步 cudaMemcpy 的方向 | host 返回时能据此知道什么 |
| --- | --- |
| pageable host → device | copy 前有 stream 同步；pageable 数据复制到 staging 后可返回，最终 DMA 未必完成 |
| pinned host → device | 对 host 同步，不能把这一条外推到其他方向或 Async 参数组合 |
| device → pageable/pinned host | 此 copy 已完成，才可依该成功回读消费目标 |
| device → device | 不提供 host 侧同步，不能当 host 的完成屏障 |
| host → host | 对 host 完全同步 |

这也是基本例保留同步检查与资源寿命的原因。即便某个特定 pageable H2D 已完成 staging，本文仍让 host 输入保留到全部相关访问成功结束，以便采用统一且明确的拥有协议；失败返回时不假定 staging 已完成。

`cudaMemcpyAsync` 的 Async 后缀不保证任何参数下都立即返回。pageable host 传输可能阻塞或先 staging，API 也可能因资源等原因阻塞；不能依赖未文档化的具体阻塞行为。想让 host/device copy 与 device compute 预期重叠，需要相应设备并发能力、pinned host 内存、允许重叠的 stream 语义，以及独立数据块或显式依赖；这些是必要设计条件，不是收益保证。pinning 有成本且占用稀缺资源，应评估复用和总成本，不应为降低一次 copy 占比就默认把所有数据 pin 住。[Best Practices §9.1–9.1.2](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-best-practices-guide/index.html#data-transfer-between-host-and-device)

同一 stream 中先 copy、再 kernel、再 copy 的设备依赖有序。不同 stream 没有自动继承这条依赖：共享数据时可用已正确记录的 event 与 `cudaStreamWaitEvent` 建立等待，host 读取/释放还须有对应的成功完成观察。host 输入在 DMA 读取结束前不覆盖或释放，host 输出在 DMA 写入结束前不读取或释放；device 中间缓冲也要等最后一次消费者退场。stream 对象销毁与这些数据的寿命是两件事。

default stream 还应分清两种模式：legacy 使用每个 device 的特殊 NULL stream，具有文档规定的隐式同步关系；per-thread 模式给每个 host 线程一个普通 default stream。不能把 legacy 下观察的跨 stream 等待直接当作 per-thread 保证，也不能靠启用多 stream 就推导必有重叠。实际可重叠程度取决于设备能力、命令发布顺序与依赖。[Programming Guide §3.2.8.5.1–5.5](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-programming-guide/index.html#streams)

## 6. P5：CPU/GPU 的盈亏与重叠时间线

**PAPER_EXPECTED。** 以下所有数值单位为假设的 ms，比较相同任务、相同输入/输出驻留位置、相同正确性和完成边界；“其他”已含本模型约定的剩余成本。第一张表无阶段重叠、无重复计费，故 GPU 总时间可逐项相加。

| 案例 | CPU 总时间 | H2D | kernel | D2H | 其他 | GPU 总计 | 传输/GPU | 模型内选择 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| A | 20 | 2 | 3 | 2 | 1 | 8 | 4/8=50% | GPU 为 20/8=2.5 倍加速 |
| B | 5 | 0.5 | 6 | 0.5 | 1 | 8 | 1/8=12.5% | CPU 更快，GPU 总时间为 CPU 的 1.6 倍 |

A 否定“传输占比必须小于 30% 才能领先”，B 说明占比小也不充分：这个百分比没有包含 CPU 方案的时间。应先问业务能否避免不必要的往返、中间结果是否能驻留 device、多个操作能否批量摊销，再比较完整终点。只缩短热点而保留串行部分会限制整体收益，可结合 [Best Practices §3.1.3.1 Amdahl 分析](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-best-practices-guide/index.html#strong-scaling-and-amdahls-law)理解，但这些数字并非手册跑分或本机预测。

第二个模型有两个相互独立的 chunk A/B，每块 H2D=2、kernel=3、D2H=2 ms。假设一个 copy 引擎与一个 compute 引擎能并行，同类任务各自不能并行；有两份无冲突的缓冲，正确 stream 依赖已成立，发布、分配等额外成本暂设 0。完全串行是 `2*(2+3+2)=14 ms`，一种合法重叠安排如下：

| 引擎 | 假设时间段 |
| --- | --- |
| copy | A H2D 0–2；B H2D 2–4；A D2H 5–7；B D2H 8–10 |
| compute | A kernel 2–5；B kernel 5–8 |

终点取关键路径的 10 ms，不是把各阶段总和 14 当实际 wall time，也不是按两个 stream 直接除二得到 7。A 输出到 7 才能读，B 到 10 才能读；每块资源至少保留到其最后一次访问完成。若 B H2D 改写 A kernel 正在读的同一个缓冲，这条时间线就不合法，先修资源与依赖，不能把数据竞争称为优化。设备能力、pinning 与 stream 条件未经核验时，10 ms 只是一张理想模型的算术结果。

## 7. P7：分歧、资源驻留和合并访存是三个问题

**PAPER_EXPECTED。** 本节仅采用 NVIDIA CUDA 的概念，不借用 AMD wavefront 或 ROCm 的结论。一个 warp 有 32 个线程；SM 可以在就绪 warp 间选择工作来隐藏等待，但有多少线程、哪些 lane 在执行、一次访问需要多少事务，分别回答不同问题。[Programming Guide §4.1–4.2](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-programming-guide/index.html#simt-architecture)

### 7.1 分歧看同一 warp 的 active lanes

设同一 warp 的 lane 0..15 走路径 A，16..31 走路径 B；执行 A 时 active=16，执行 B 时 active=16，其余 lane 不参与对应指令。若 32 个 lane 全走 A，则只需要 A 路径。仅不同 warp 走不同分支，不构成这个“同 warp 分歧”的例子。

不能由 16/32 直接算成总时间翻倍，实际还取决于路径长度、代码生成和其他瓶颈。Volta 以来的独立线程调度保留每线程执行状态并允许更细粒度调度，没有消除 SIMT 分歧成本，也不授权程序依赖隐式 warp 同步。应根据真实依赖使用适当同步协议，不能用“GPU 不擅长复杂分支”替代分析。

### 7.2 occupancy 看资源允许多少 warp 驻留

occupancy 是每个 SM 的 active/resident warp 数相对最大可驻留 warp 数的比率，不等于当前发射效率或时间收益。寄存器、shared memory、block/warp 上限及分配粒度共同限制驻留；由静态资源算出的上限与分析器实际采样的 achieved occupancy 必须分开。[Best Practices §10.1–10.1.1](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-best-practices-guide/index.html#occupancy)

用一个**抽象资源机器**手算，不当作任何真实 GPU 规格：SM 最多 8 warp、最多 2 block，每 block 4 warp。配置甲的寄存器预算只容纳 1 block，故驻留 block 上限 `min(2,8/4,1)=1`，occupancy 上限 `4/8=50%`；配置乙的资源允许 2 block，上限变成 `8/8=100%`。这不能推出时间减半，还缺就绪程度、指令级并行、带宽和调度证据。如果一个 block 的需求就超过合法资源上限，首先解决能否 launch，而不是追求更高比率。

### 7.3 合并访存看地址覆盖多少事务

另设 CC 6.0+ 的单次 warp 全局读，每个 lane 读 4 字节，32 个 lane 全参与，base 按 32 字节对齐。按 CUDA 12.6.3 的 32-byte 事务覆盖模型：

| lane 的地址 | 有效数据 | 覆盖的 32-byte 区段 | 事务覆盖字节 | 覆盖利用率 |
| --- | ---: | ---: | ---: | ---: |
| base + 4*lane | 32*4=128 B | 4 | 128 B | 100% |
| base + 32*lane | 32*4=128 B | 32 | 1024 B | 12.5% |

连续方案覆盖 byte 0..127；步长方案每个 lane 落在不同区段。这里计算的是单次 warp 访问的事务区段，不是 DRAM 实测字节、cache miss 计数或“必快 8 倍”。[Best Practices §9.2.1–9.2.1.1](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-best-practices-guide/index.html#coalesced-access-to-global-memory)

因此，地址若像第二行且语义允许调整，先考虑让相邻 lane 访问相邻数据或复用中间结果；地址已紧凑而资源使驻留 block 很少，再核寄存器/shared memory 与分配粒度。提高 occupancy 若引入 spill 或减少每线程资源，可能增加另一处成本。只改 block 大小会同时改变 warp 数、尾部、资源分配和调度，不能用一个最大 occupancy 数字完成选型。

## 8. 未来验证：先把问题变成能判定的证据

本节保留原有编译对照、CUDA 正确性、性能分析与部署比较的用途，列出未来接入条件；本轮不运行文章程序、不安装工具、不查询设备、不改频率/亲和性/权限，也不新增 runner 或 CI。工具可用性不足时，结论停在静态规则与纸面模型；CPU 结果只证明 CPU 侧，未核对的模拟器不能补出 GPU 运行证据。

### 8.1 每次选择一个问题，而非铺满参数矩阵

| 要验证的问题 | 先固定的条件与最小变化 | 应保存的观察与停止条件 |
| --- | --- | --- |
| loop/SLP 是否改变代码生成 | 2.2 节同源、同目标、同数值规则，只变两个自动向量化开关 | remarks、产物和源码版本；编译失败则不进入计时 |
| 内联或 LTO 是否有价值 | 同一完整应用和工作负载，只改对应策略；检查实际参与的模块 | 调用/代码尺寸与整体时间；信息缺失先修构建，不把 flag 存在当成功 |
| 向量加法是否正确 | 4 节有界输入、0/1/非整块/上界、全量参考、逐失败终点 | 完整程序及原始退出状态；错误或未决资源状态阻止性能结论 |
| GPU 是否受资源约束 | 固定合法输入、目标和工作边界；选择少量合法 block 配置 | ptxas 资源资料、实际可用 kernel 指标及时间；超限先查合法性 |
| 传输是否能与计算重叠 | 固定 chunk/拥有关系/stream 依赖，再改变策略 | 系统时间线、完成事件和 host 总时间；错误时间线先修依赖 |
| CPU 数据供给是否限制 SIMD | 明确元素数及总字节，保留相同输出；布局/tile 是单独问题 | 按 CPU 主篇界定的缓存/带宽与计数口径，不能把纸面行数作 PMU |

规模用“元素数”还是“MiB”必须标清，多个数组的总容量与每数组容量也不同。吞吐可用完成元素/s 或业务操作/s；若报告 GB/s，先写清是算法逻辑字节还是某条链路的传输字节。IOPS 只在真正以 I/O 请求为工作单位时有意义。功耗、RSS、显存上限、错误率或尾延迟是否是验收指标，应由项目目标决定，不强迫所有微基准填满同一张表。

GPU 的未来工作顺序应为：先将 4 节接入合同落实为可审查的完整 host 程序，确认固定 toolkit/driver/目标与编译输入，再用 nvcc 构建并检查构建退出状态；先做全结果核对与 Compute Sanitizer 等正确性检查，再考虑 Nsight Systems 的系统时间线、Nsight Compute 的 kernel 指标以及 ptxas 的资源报告。每一步失败都保留原流，并阻止依赖它的下一结论。本文没有核对这几种工具的全部具体 CLI、metric 名或版本兼容，故不给缺少完整宿主的 `nvcc ... && ./...` 假完整配方；具体命令要按届时固定版本重新确认。

CPU 的 perf 计数、采样/火焰图，或按问题选择的 strace/bpftrace，只能回答各自观测层的问题；`-fsanitize` 是特定错误检测，`-Rpass` 是编译决策报告，不能互换。采样、插桩和 profiler 会改变观察条件，权限与平台支持也要记录。现有 CPU 主篇负责 PMU 事件含义，本篇不把工具启动成功当完整测量有效。

### 8.2 P6：小样本 p99 可以算，但不能承担过多推断

**PAPER_EXPECTED。** 本例自己定义 nearest-rank：对 N 个升序样本，p 分位取第 `ceil(p*N)` 项（排名从 1 开始），p99 的 p=0.99；样本单位为一次完整请求的 ms。N=5、20、30 时，p99 的排名分别为 5、20、30，都等于各组样本最大值。

取 20 个样本 `[1,2,...,19,100] ms`：p50 排名 10，值 10；p95 排名 19，值 19；p99 排名 20，值 100。仅将最后一个样本换成 20 ms，p50/p95 不变，样本 p99 变成 20 ms。这是已给样本的算术，不证明真实分布的 p99 改善 5 倍，也不表示小样本不能计算分位数。

因此不把“至少 5 次”“预热 5 次、测 20 次”“重复 30 次”视作通用可信度保证。应先定义请求、批次或进程启动哪一个是样本，决定 warmup、JIT、加载、分配与等待是否计入，再保存原始样本并考察相关性、噪声与所需置信。少量批次可以如实报告样本分布和最大值，不能冒充可靠的服务尾延迟估计。这里的排名规则与数字是教学定义，不归因于 CUDA 手册的统计定理。

性能 CI 也应先过正确性门槛。若项目选择“相对基线退化 10%”触发告警，那是明确的项目政策，需要可比环境和噪声分析支撑；单次超线不能自动归因于某 flag。确认回归后再选择定位、回退或 bisect，不把告警阈值写成硬件规律。

### 8.3 失败诊断、恢复与报告

| 症状 | 先检查的因果环节 | 合理下一步与停止边界 |
| --- | --- | --- |
| 未向量化 | missed/analysis、合法依赖、别名检查和目标成本 | 若确有顺序依赖就保留它；没有事实不能增加 assume/restrict |
| O3、内联或 LTO 后变慢 | 同一输入实际代码路径、尺寸、资源压力与完整时间 | 保留已知正确基线作单因素比较；不能直接指定统一关闭顺序 |
| CUDA copy/launch/sync 错误 | 4.4 节首错观察点、所有权与最后完成边界 | 先停依赖/消费；未知状态交宿主，不能继续 profiler 当作成功运行 |
| 非法访问 | 索引、范围、资源寿命和实际错误证据 | 缩小合法输入可帮助复现，但缩 grid 不是修正越界的证明 |
| 超时或 watchdog | 执行是否能终止、工作量、同步依赖与环境限制 | 在正确性与宿主错误协议成立后再研究拆分；“降低 block”不是通用恢复 |
| kernel 快但总时间慢 | 传输、发布、等待、分配与数据复用是否计入 | 依据时间线减少必要外的往返；pinned/Async 是候选策略，不是自动修复 |
| 数值不符 | 参考算法、输入、有限性、归约顺序及浮点选项 | 隔离有疑问的优化并恢复已知正确数值配置；不能仅关 fast-math 就宣布修好 |

回滚应回到与部署、数据格式和 ABI 相容的已知正确版本或配置，分别保留功能修复与可疑优化的证据；无完成协议的 CUDA 错误不是热切换二进制就能安全结束的工作。每个失败、超时、缺失样本都应保留，不能删掉后只报成功分布。

下面合并排障记录与可复现报告的用途。它是**未来填写模板**，不是本轮实验结果；不适用字段写“不适用”，未采集字段写“未采集/未运行”，不能留一个空格让人误判为零：

```text
状态：未运行 / 构建失败 / 执行失败 / 数值失败 / 正确但无收益 / 已测
源码版本与完整输入：
编译器、方言、target ISA、编译/链接选项、浮点合同：
实际 CPU/GPU、CUDA/driver、OS、部署方式与资源限制：
样本单位、输入规模/分布、数据驻留位置、参考算法：
计时起止、warmup/JIT/分配是否计入、重复数量与原始样本：
优化 remark、产物、CPU cycles/cache 事件的精确定义与原流：
GPU H2D/kernel/D2H、关键路径、host 总时间及 profiler 原文件：
全结果数值判定、非有限策略、最大误差、首个失败下标：
首错阶段/直接返回码/退出码、已获资源、已发布访问、完成边界：
合法清理的次错与尚未解决的资源状态：
结论、适用范围、项目阈值、回滚版本/选项及未验证项：
```

### 8.4 部署比较保留为条件问题

裸机、容器、VM 的名称不能单独决定性能方向。未来比较须记录调度与资源配额、vCPU/CPU 的计时口径、内存位置、设备暴露方式、PCIe 拓扑、驱动/Runtime 和后台负载；wall time 不等于宿主 CPU 使用时间。频率、功耗上限、SMT、NUMA 和亲和性是应记录及在获准条件下控制的变量，不是本文要求读者立即更改的设置。

“VM 的 p99 一定更大”“直通必能迁移或必不能迁移”都需要具体实现、配置和证据。VMX/SVM、EPT/NPT、IOMMU、virtio、SR-IOV、MIG、vGPU 与直通属于不同层级或产品机制；本轮 CUDA 选读不为其具体隔离、容量或迁移保证背书。这里保留“部署变化会改变测量解释”的比较目的；地址翻译与隔离原理继续阅读[虚拟内存、PageFault 与 mmap](../操作系统与系统I-O/02-虚拟内存、PageFault与mmap.md)及[权限安全与隔离](../操作系统与系统I-O/04-权限安全与隔离.md)，产品能力另查固定官方合同。

## 9. 实际来源范围与历史保留

本轮在 2026-10-07 重新阅读固定来源的以下选段，不把保存整页或 PDF 等同于通读，也不把规则核对当运行。文章的代码、表格和模型均未执行，没有优化 remark、机器码、profile、GPU 输出、时延、带宽或 profiler 数据。

| 来源 | 实际选读位置 | 本文承担的结论 |
| --- | --- | --- |
| [LLVM 18.1.8 Vectorizers](https://releases.llvm.org/18.1.8/docs/Vectorizers.html) | Loop/SLP 默认与关闭方式；Diagnostics；Unknown trip count；Runtime Checks of Pointers | 自动向量化、版本化、尾部、成本与报告 |
| [Clang 18.1.8 Users Manual](https://releases.llvm.org/18.1.8/tools/clang/docs/UsersManual.html) | Options to Emit Optimization Reports；ffast-math；PGO；插桩构建、merge、profile-use | 报告边界、浮点假设、代表性资料 |
| [Clang 18.1.8 Language Extensions](https://releases.llvm.org/18.1.8/tools/clang/docs/LanguageExtensions.html#builtin-assume) | `__builtin_assume` | 不求值的真实不变量承诺 |
| [Clang 18.1.8 ThinLTO](https://releases.llvm.org/18.1.8/tools/clang/docs/ThinLTO.html)及[Command Guide](https://releases.llvm.org/18.1.8/tools/clang/docs/CommandGuide/clang.html) | Introduction、Basic；driver 阶段、Stage/Target Selection、Code Generation 的 O 级别与 LTO 模式 | 摘要/导入/后端、编译链接输入和优化级别 |
| [C11 N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) | §6.7.3.1 段 1–10，印刷页 123–124 | restrict 访问约束；委员会草案身份，非购得 ISO 正式文本 |
| [C++ N4861 固定源码](https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/statements.tex#L565-L615) | [stmt.for]，commit `6aea7f6be0895b9dd361c6562bdee2f3809e4fa0` | for 语句体与初始化声明作用域 |
| [CUDA 12.6.3 Programming Guide](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-programming-guide/index.html) | §3.2.2 向量加法；§3.2.8.5.1–5.5 Streams；§3.2.12 Error Checking；§4.1–4.2 | device 数据流、顺序、完成/错误观察、SIMT 与驻留 |
| [CUDA 12.6.3 Best Practices](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-c-best-practices-guide/index.html) | §3.1.3.1；§6.1.1–6.1.2；§9.1–9.1.2；§9.2.1–9.2.1.1；§10.1–10.1.1 | 整体收益、参考校验、传输、32-byte 覆盖、occupancy |
| [CUDA 12.6.3 Runtime API](https://docs.nvidia.com/cuda/archive/12.6.3/cuda-runtime-api/api-sync-behavior.html) | §2；Error Handling 的 Get/Peek；Device Management 的 DeviceSynchronize；Memory Management 的 Memcpy/Free/FreeHost/HostUnregister；Types 的 IllegalAddress | 参数/方向同步合同、配对和失败保证边界 |

CUDA 来源留存的是 web 工具实际返回的指定行文本，不是服务器 HTTP 原始 HTML 或响应头；早期部分辅助取源/解析进程没有完整原始 stdout/stderr，不能补造。准备期间 ThinLTO 错误路径 404、辅助解析缺模块、压缩内容误解码，以及 CUDA 同步来源 URL 的元数据更正均有记录；正确的固定 Clang 路径与 DEVICE 页面后来核对成立。这些是资料处理边界，不是编译失败、设备故障或正文实验日志。

本文没有覆盖所有 AMD/ROCm、GPU 型号、Nsight/Compute Sanitizer CLI、虚拟化产品或模拟器，也没有证明任意 CUDA 错误后的通用安全恢复。H2D/launch/同步/D2H 的未知状态必须遵守 4.4 节接入门；正常成功链已在 4.3 节明确到清理与唯一成功终点。

### 9.1 两段历史前言

以下两段按原先顺序逐字保留。它们属于 2026-08-20 的历史文本；其中“当前版本”、man7/浮动文档入口和实验要求不充当本轮固定版本或运行证据。历史上已有实质全文，后续也有 OKF、2026-09-03 关联导航和迁移/链接维护；本次不是首次历史贡献。

历史前言一：

> 知识基线：以当前标准、协议或工具链版本为准，平台差异需实测。
> 最后更新：2026-08-20。
> 官方参考：https://man7.org/。
> 验证与基准：按文中命令执行最小实验并记录指标。

历史前言二：

> 知识成熟度：L2
> 知识基线：当前标准与工具链版本，平台差异需验证。
> 最后更新：2026-08-20。
> 官方参考：https://llvm.org/docs/、https://docs.nvidia.com/cuda/
> 验证与基准：按文中步骤执行实验并记录指标。
来源：https://llvm.org/docs/｜验证与基准：perf、Nsight/rocprof、编译器优化报告。


### 9.2 原参考入口

以下四项保留原参考块，仅作为历史与延伸入口；本轮支持具体结论的版本与选读位置见上表。

## 参考

- https://llvm.org/docs/Vectorizers.html
- https://llvm.org/docs/OptimizationRemark.html
- https://docs.nvidia.com/cuda/cuda-c-programming-guide/
- https://docs.nvidia.com/nsight-systems/


## 关联
- [ISA、汇编与调用约定](../编译链接与ABI/03-ISA汇编与调用约定.md)
- [工程调试与性能分析](../../../00_Index/学习路线/工程实践与质量.md)



---

## 关联知识与工程落地

- **前置依赖**：
  - [01-处理器存储层次与性能工程](01-处理器存储层次与性能工程.md)：多级缓存与内存带宽。
  - [03-ISA汇编与调用约定](../编译链接与ABI/03-ISA汇编与调用约定.md)：向量寄存器与汇编展开。
  - [13-数学与算法基础/02-概率线性代数与数值计算](../../02-数学与游戏算法/数学与数值计算/02-概率线性代数与数值计算.md)：三维向量变换与数值精度。
- **游戏引擎与图形落地**：
  - [游戏知识/02-渲染与图形/README](../../../游戏知识/02-渲染与图形/README.md)：GPU 着色器与渲染管线。
- **分类与领域入口**：
  - [08-计算机体系结构与性能 README](../../../00_Index/学习路线/编程与计算机基础.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/学习路线/编程与计算机基础.md)
