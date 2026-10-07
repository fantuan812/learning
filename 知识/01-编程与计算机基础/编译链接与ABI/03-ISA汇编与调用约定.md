---
type: Reference
title: "ISA、汇编与调用约定"
status: stable
verified: []
maturity: L2
updated: 2026-10-07
sources:
  - id: sysv-amd64-2018
    title: "AMD64 psABI Version 1.0 / Draft 1.0，2018-01-28，3.2"
    resource: https://github.com/hjl-tools/x86-psABI/wiki/x86-64-psABI-1.0.pdf
  - id: aapcs64-2025q1
    title: "AAPCS64 2025Q1，基础硬浮点 PCS"
    resource: https://github.com/ARM-software/abi-aa/blob/c51addc3dc03e73a016a1e4edf25440bcac76431/aapcs64/aapcs64.rst
  - id: microsoft-x64
    title: "Microsoft x64 calling convention，2026-10-06 快照"
    resource: https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170
  - id: microsoft-stack
    title: "Microsoft x64 stack usage，2026-10-06 快照"
    resource: https://learn.microsoft.com/en-us/cpp/build/stack-usage?view=msvc-170
  - id: intel-sdm-2024
    title: "Intel SDM Vol.2，325383-086US，普通近 CALL/RET"
    resource: https://cdrdv2-public.intel.com/843829/325383-sdm-vol-2abcd-dec-24.pdf
  - id: arm-a64-guide-2025
    title: "Arm A64 ISA Guide 1.3，102374_0103_01_en，3/26/27"
    resource: https://documentation-service.arm.com/static/68cd1a81cccf2a5517018d62
  - id: gcc14-overall
    title: "GCC 14.2，Overall Options"
    resource: https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Overall-Options.html
  - id: gcc14-optimize
    title: "GCC 14.2，Optimize Options"
    resource: https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Optimize-Options.html
  - id: gcc14-debug
    title: "GCC 14.2，Debugging Options"
    resource: https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Debugging-Options.html
  - id: gcc14-c
    title: "GCC 14.2，C Dialect Options"
    resource: https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/C-Dialect-Options.html
  - id: binutils-objdump
    title: "GNU binutils 2.43，objdump"
    resource: https://sourceware.org/binutils/docs-2.43/binutils/objdump.html
  - id: binutils-readelf
    title: "GNU binutils 2.43，readelf"
    resource: https://sourceware.org/binutils/docs-2.43/binutils/readelf.html
  - id: clang18-cross
    title: "Clang 18.1.8，CrossCompilation"
    resource: https://releases.llvm.org/18.1.8/tools/clang/docs/CrossCompilation.html
  - id: clang18-attributes
    title: "Clang llvmorg-18.1.8，NoInlineDocs / DisableTailCallsDocs"
    resource: https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/clang/include/clang/Basic/AttrDocs.td
  - id: llvm-mca18
    title: "llvm-mca llvmorg-18.1.8，Description / Load-Store Unit"
    resource: https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/llvm/docs/CommandGuide/llvm-mca.rst
  - id: n4861-atomics
    title: "C++ N4861 工作草案，atomics.order"
    resource: https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/atomics.tex
---
# ISA、汇编与调用约定

> 知识成熟度：L2。本文主责是从具体 C 函数签名推到调用边界的寄存器、栈、结果与保存责任，再用真实产物解释编译器选择。
> 知识基线：SysV AMD64 2018 文档、AAPCS64 2025Q1 基础硬浮点 PCS、Microsoft 默认 x64 2026-10-06 页面快照；指令语义只采用下文列出的 Intel/Arm 选读段落。
> 最后更新：2026-10-07。三平台对照均为 PAPER_EXPECTED（规则推导）；E1 的本机观察另列。没有 Windows/AArch64、手写汇编、性能、并发或异常展开运行证据，局部 E1 不提升全文成熟度。

## 1. 先确定正在解释哪一层

读到“参数在 RDI”时，先问目标 ABI，不能只问 CPU 是否支持 x86-64。同一条普通近 `call` 的指令效果可以服务 SysV AMD64 或 Windows x64，但两个调用方对参数位置的约定不同；把其中一边的表搬给另一边，CPU 并不会替你纠正。

| 层次 | 决定什么 | 本文可追踪的例子 |
|---|---|---|
| ISA | 寄存器和指令的架构可见效果、编码、异常及架构内存顺序 | x86-64 普通近 CALL 将控制流返回地址压栈；A64 BL 把它写入 LR |
| ABI / PCS | 二进制参与方的参数、结果、布局、栈和保存合同 | 同一个五参数函数在 SysV 与 Windows 的位置不同 |
| 汇编表示 | 如何书写或显示指令、操作数和伪指令 | Intel 与 AT&T 是 x86 汇编语法选择，不能用 `-Mintel` 解码 A64 |
| 编译器 | 在语言和目标合同内选择合法实现 | 内联、常量传播、寄存器分配、尾调用或省略帧指针 |
| 微架构 | 对给定机器码的具体执行成本 | 依赖链、执行端口、预测、缓存命中会改变时间 |
| 语言内存模型 | 哪些跨线程访问与同步在语言层有效 | acquire 读到哪一次 release 序列的值，决定所述同步是否成立 |

Intel SDM Vol.2 指令格式引言展示可变组成字段；Arm A64 Guide §3 明确 A64 指令为固定 32 位，“64”不是指令编码长度。RISC-V 留作另一个 ISA 家族的学习入口，本篇没有核对或建立第四套调用约定。load/store、算术和 branch 应按具体指令阅读，不能一概说所有算术都会写 flags，也不能从一个 branch 推出它的预测成功率。MMIO 的可见访问与设备顺序还需要编译器、设备及平台合同，单个 `volatile` 关键字不足以包办。

## 2. 三套调用合同：先分类，再分配位置

以下表格全部是 PAPER_EXPECTED，不是三平台编译结果。固定前提为普通用户态、有完整原型的 C 函数、小端、自然对齐、8 位 byte；`int` 为 32 位，`U64` 明确是 8 字节 `unsigned long long`，`double` 为 8 字节且按本例自然对齐。采用 SysV AMD64 LP64、Microsoft 默认 x64 和 AAPCS64 基础硬浮点规则。LP64 的 `long` 通常 8 字节，Windows LLP64 的 `long` 为 4 字节，不能只由“64 位指针”推出二者相同。

不覆盖变参实现、非平凡 C++ 对象、过度对齐、宽向量、`__vectorcall`、soft-float、SVE/SME/APX 或操作系统另订的 PCS 变体。下面“首个参数”始终指该独立签名的首个参数，不能移用于前面已消耗寄存器的另一签名。SysV 依据 §3.2.3；AAPCS64 依据 Parameter passing Stage A–C / Result return；Windows 依据 Parameter passing / Return values。

### 2.1 标量：独立队列与形参序号槽

先对比 `void consume_u64(U64 x)` / `U64 produce_u64(void)`，以及 `void consume_double(double x)` / `double produce_double(void)`：

| ABI | consume_u64 的 x | produce_u64 结果 | consume_double 的 x | produce_double 结果 |
|---|---|---|---|---|
| SysV AMD64 | RDI | RAX | XMM0 低 64 位 | XMM0 低 64 位 |
| Microsoft x64 | RCX | RAX | XMM0 低 64 位 | XMM0 低 64 位 |
| AAPCS64 | X0 | X0 | D0（V0 低 64 位） | D0 |

再给完整签名 `double mixed(int a, double b, int c, double d, int e)`，输入依序为 1、2.0、3、4.0、5。返回的 double 分别在 XMM0、XMM0、D0；参数并不是都按上表首位置重复：

| 实参 | SysV：GP 与 SSE 两队列 | Windows：四个形参槽 | AAPCS64：GP 与 FP 两队列 |
|---|---|---|---|
| a=1 | EDI，GP0 | ECX，第 1 槽 | W0，GP0 |
| b=2.0 | XMM0，SSE0 | XMM1，第 2 槽 | D0，FP0 |
| c=3 | ESI，GP1 | R8D，第 3 槽 | W1，GP1 |
| d=4.0 | XMM1，SSE1 | XMM3，第 4 槽 | D1，FP1 |
| e=5 | EDX，GP2 | 第 5 槽在栈上；入口 RSP+40 的槽低 32 位 | W2，GP2 |

SysV 的 GP 序列为 RDI、RSI、RDX、RCX、R8、R9，SSE 序列为 XMM0–XMM7；AAPCS64 使用 X0–X7 与 V0–V7。Windows 第 n 个形参在前四槽时，按类型选择该槽的 GP 或 XMM 视图，而不是遇到 double 才从 XMM0 重新计数。因此本例的 Windows 第二实参在 XMM1，剩余的 XMM0 并不补给它。int 只消费对应的 32 位值，不能把寄存器未使用的高位当成调用合同的保证。

### 2.2 聚合：参数与返回分别推

只作类型推导的定义如下，三个对象均为 16 字节、8 字节对齐，成员偏移分别为 0、8；这里不是额外的可运行工程：

```c
/* PAPER_EXPECTED: types for the following independent signatures */
typedef unsigned long long U64;
typedef struct { U64 a, b; } Pair64;
typedef struct { double a, b; } H2;
typedef struct { double a; U64 b; } M;
```

分别问 `void consume_pair(Pair64 p)`、`void consume_h2(H2 p)`、`void consume_m(M p)`，各自从未占用的参数寄存器开始：

| 类型 | SysV：按两个 eightbyte 分类 | Windows 默认 x64 参数 | AAPCS64 参数 |
|---|---|---|---|
| Pair64 | a→INTEGER，b→INTEGER；RDI=a、RSI=b | caller 复制完整对象到 16 字节对齐的临时存储，RCX 传该副本地址 | 非 HFA 的两 double-word 复合对象，X0=a、X1=b |
| H2 | a→SSE，b→SSE；XMM0=a、XMM1=b | 同样传 caller 副本地址，RCX 不是 a 的数值 | 两个同类 double 构成 HFA，D0=a、D1=b，每成员占一个 FP 寄存器 |
| M | a→SSE，b→INTEGER；XMM0=a、RDI=b | 同样传副本地址，不拆成 XMM+GP | 成员不同类，不是 HFA；X0 带 a 的 64 位对象位模式，X1=b |

这解释了为什么“结构体 16 字节”不是唯一分类条件。M 在 AAPCS64 的 X0 搬运的是 double 的位模式，没有发生 double 到整数的数值转换；H2 的两个独立 double 在 SysV 分属两个 SSE eightbyte，也不能套用单个向量的 SSEUP 打包规则。Windows 的参数副本属于按值调用的实现，不能改写成“callee 获得对 caller 原对象的普通引用”。

现在独立改问 `Pair64 produce_pair(void)`、`H2 produce_h2(void)`、`M produce_m(void)`。返回值要重新按结果规则处理，不能直接复制参数寄存器表：

| 返回类型 | SysV 结果 | Windows 结果 | AAPCS64 结果 |
|---|---|---|---|
| Pair64 | RAX=a、RDX=b | caller 提供结果缓冲区，地址作为隐藏首参数放 RCX；callee 写结果并把同一地址放 RAX | X0=a、X1=b |
| H2 | XMM0=a、XMM1=b | 同上，不能把 16 字节 C struct 当作 `__m128` 的寄存器返回规则 | D0=a、D1=b |
| M | XMM0=a、RAX=b | 同上 | X0=a 的位模式、X1=b |

Windows 隐藏结果参数会移动后续显式参数槽。例如 E1 的 `Pair64 make_pair(U64 a, U64 b)` 若按 Windows 规则纸面推导，应为 RCX=结果地址、RDX=a、R8=b，RAX 返回地址；不是 RCX=a、RDX=b。SysV 同一签名为 RDI=a、RSI=b，RAX/RDX 返回两成员；AAPCS64 为 X0=a、X1=b，X0/X1 返回两成员。

### 2.3 只剩一个寄存器，聚合如何回退

下面都是独立的纸面调用，未加入 E1 运行矩阵。关注的是“剩一个槽时需要两个槽”的规则，两个 ABI 因寄存器数不同而使用不同数量的前置参数。

- SysV：`void gp5(U64 a0,U64 a1,U64 a2,U64 a3,U64 a4,Pair64 p,int tail)`。前五项用掉 RDI 至 R8，只剩 R9。p 需要两个 INTEGER 槽，无法完整分配，已试分配给 p 的槽撤销，p 整体放栈；随后 tail 可使用 R9D。不是 p.a 在 R9、p.b 在栈。
- AAPCS64：`void gp7(U64 a0,U64 a1,U64 a2,U64 a3,U64 a4,U64 a5,U64 a6,Pair64 p,int tail)`。前七项令 NGRN=7。p 的两个 double-word 不满足 C.12，C.13 将 NGRN 置 8，p 整体入栈；tail 也入栈，不能捡 X7。此例 p 从入口 SP 起占 16 字节，tail 在下一 8 字节槽的低 32 位。不要借用 AAPCS32 的分拆规则。
- FP 耗尽对照：`void fp7(double a0,double a1,double a2,double a3,double a4,double a5,double a6,H2 h,double tail,int k)`。SysV 先占 XMM0–6；h 需要两个 SSE 槽，整体回退栈后 XMM7 仍留给 tail，k 用 EDI。AAPCS64 先令 NSRN=7；HFA h 不满足 C.2，C.3 令 NSRN=8，h 在栈上，tail 也在栈上，但 k 仍用 W0。HFA 失败耗尽的是 FP 队列，不是 GP 队列。

Windows 默认 x64 没有上述“两个寄存器装一个 16 字节聚合”的过程；这类参数本来就是一个副本指针，占其形参位置的一个槽。超过前四个位置后该指针也按栈参数处理。

### 2.4 间接结果地址不是控制流返回地址

设 `typedef struct { U64 a,b,c; } Triple64;`，24 字节、自然对齐、非 HFA，函数为 `Triple64 result(int n, double x)`。只做 PAPER_EXPECTED：

| ABI | caller 提供的结果缓冲区地址 | n / x | callee 完成后的地址返回要求 |
|---|---|---|---|
| SysV | 隐藏首 GP 参数 RDI | ESI / XMM0 | RAX 必须为同一结果地址 |
| Windows | 隐藏首形参槽 RCX | EDX / XMM2 | RAX 必须为同一结果地址 |
| AAPCS64 | 专用 X8，不消费 X0–X7 | W0 / D0 | 基础 PCS 不要求 X0 返回此地址，也不要求保留 X8 |

SysV 的 Triple64 三个 INTEGER eightbyte 经分类清理成为 MEMORY；Windows 大于可直接返回的此类小对象；AAPCS64 非 HFA 且不能直接用结果寄存器容纳，因此由 caller 预留大小和对齐充分的存储。Windows 的 x 用 XMM2 是隐藏首槽导致的整体右移；SysV 隐藏参数只挤占 GP 序列，FP 仍从 XMM0 开始。

这块内存用来承载“数据结果”；CPU 要跳回哪条指令则是另一个控制流返回地址，普通 x86-64 调用把后者放在栈上，A64 BL 把后者写入 LR。两种地址的生命期、位置与责任不同。

### 2.5 未被上述表覆盖的边界

变参不是继续照抄固定参数表就结束：SysV 所据版本用 AL 提供向量参数寄存器使用数的上界（0–8）；Microsoft 对相应位置的浮点参数另有 GP 复制要求；AAPCS64 还需核对具体系统变体。使用匹配原型和该实现的 `va_list`，本篇不自制 `va_arg`。

`extern "C"` 指定语言链接，不能单靠名字形状稳定布局、异常、所有权、标准库对象或不同平台 ABI。结构体断言只能证实所写布局前提；`sizeof(void*)==8` 更不能证明 `unsigned long` 同宽。对象格式、符号导出、动态库升级与 C++ ABI 边界仍由尾部链接的《编译链接与ABI全流程》负责，本篇不复制动态库或 ODR 实验。

## 3. 栈、保存责任与正常调用的因果链

### 3.1 对齐必须写清时间点和存储所有者

令 S 为 callee 刚到入口、尚未执行序言时的 SP 值。下表仍属前述普通标量子集：

| ABI | call 前与 callee 入口 | 特殊区间和使用边界 |
|---|---|---|
| SysV AMD64 | call 前 RSP mod16=0；call 压入 8 字节返回地址，所以入口 S mod16=8，即 (S+8) mod16=0 | 当前 RSP 以下 128 字节 red zone 可放不需跨函数调用存活的临时数据；经常用于 leaf，但不是只允许 leaf 使用；不能当跨 call 保存区，也不套到内核 |
| Windows x64 | 普通调用点 RSP mod16=0；call 后入口 S mod16=8；正文区域维持对齐，序言/尾声及文档所述 leaf 等情形另论 | caller 为 callee 预留至少 32 字节 home/shadow area。入口 [S+8,S+40) 是四槽；第 5 参数从 S+40 起。预留不等于寄存器已经写入槽，区域供 callee 使用，caller 不能在其中保留跨该 call 的活值 |
| AAPCS64 | 公共接口 SP mod16=0；通过 SP 访问内存时也须 16 字节对齐；BL 本身不压栈 | 首个栈参数在入口 SP。SP 以下为 inactive region，不允许读写；基础 PCS 没有 SysV red zone 或 Windows 固定 home area |

SysV 的更宽向量栈参数还可能要求 32/64 字节边界，不能把本表简化为“所有函数时时刻刻 16 对齐”。Windows 当前 RSP 下方也没有 SysV red-zone 保证。Windows 参数 shadow space 与 Intel CET shadow stack 是不同机制；本文不分析 CET/PAC、特权切换或完整异常行为。

### 3.2 保存的是入口值，且有明确宽度

“callee-saved”表示 callee 如果使用这些寄存器，须使正常返回后的值与入口相同；保存可以避免、可以换位置，不能要求每函数机械 push 一遍。caller-saved 的活值若需跨调用保留，由 caller 安排可靠存储或其他寄存器。

| ABI | 本文的核心被调用者保全子集 | 不能据此推导的事 |
|---|---|---|
| SysV AMD64 | RBX、RBP、R12–R15 的 64 位值；栈责任要求恢复 RSP | 核心 XMM 数据寄存器不要求由 callee 保全；FP 控制/状态、DF 等另有规范规则 |
| Windows x64 | RBX、RBP、RDI、RSI、R12–R15、RSP；XMM6–XMM15 低 128 位 | 相应 YMM/ZMM 高位是 volatile；新扩展和 FP 控制状态不由此简表穷尽 |
| AAPCS64 | X19–X29 全部 64 位及 SP；V8–V15 仅低 64 位 | 不能把整个 128 位 V8–V15 都当已保全；X18 角色依平台，不能一律当临时或认为“把它当 callee-saved 就够” |

可移植手写 A64 代码应避开 X18，先核平台规定；IP0/IP1 还可能被链接器 veneer 使用。LR 保存责任要结合是否会再调用：BL 会覆写 LR，一个正常返回到原 caller 的非叶函数必须先安排好原返回地址。`ret`/`RET` 不替你恢复所有非易失寄存器、栈帧或对象状态。

### 3.3 从 wrap(3,4) 到 add2 再正常返回

语义为 `add2(a,b)=a+b`，`wrap(a,b)=add2(a,b)+1`，结果依序为 7、8。以下是作者按规则写的最小状态示意，**不是编译器输出，也不是可独立汇编的完整函数**；省略符号、展开元数据等，仅演示无额外栈参数、局部对象或其他保存项的正常路径。不提取或执行这些片段。

SysV 的输入在 RDI=3、RSI=4：

```text
PAPER_EXPECTED / SysV x86-64 / semantic sketch
wrap entry: RSP=S, S mod16=8; [S] is wrap's caller return address
reserve 8:  RSP=S-8, now call-aligned
call add2:  RSP=S-16, [S-16] is the continuation inside wrap
add2:      compute RAX=3+4=7; preserve required ABI state
near ret:  consume [S-16], RSP=S-8, resume wrap
wrap:      compute RAX=7+1=8; release its 8 bytes, RSP=S
near ret:  consume [S], RSP=S+8, resume wrap's caller
```

Windows 的输入在 RCX=3、RDX=4：入口 S mod16=8；wrap 预留 40 字节后 RSP=S−40，其中 32 字节为它调用 add2 提供的 home area，另外 8 字节使调用点对齐。call 再压 8 字节，因此 add2 入口为 S−48，它可用的 home area 为 [S−40,S−8)。add2 产生 RAX=7 并正常 ret 后 RSP=S−40；wrap 加 1，释放这 40 字节至 S，再 ret 取回自己的 caller 地址。wrap 入口已有的上层 home area 与这次给 add2 分配的区域不是同一块。

A64 的输入为 X0=3、X1=4：

```text
PAPER_EXPECTED / AAPCS64 / semantic sketch
wrap entry: SP=S, S mod16=0; X30=original caller continuation
save X29,X30 in a new 16-byte frame record: SP=S-16
set X29 to that record; BL add2 overwrites X30 with wrap continuation
add2: compute X0=3+4=7; RET through its incoming LR; SP remains S-16
wrap: compute X0=7+1=8; restore saved X29 and original X30; SP=S
RET through restored LR: return to wrap's original caller
```

这里 8、40、16 都依赖本例的精简条件，不能拿去分配任意函数的栈帧。AArch64 帧链要求由平台选择，不是每个函数都必须出现同一个 `stp/ldp` 序言。Intel 普通近 CALL/RET 的栈效果与 Arm 指南 §26 的 BL/LR/RET 是指令依据；是否需要保存哪个寄存器则来自 ABI。

纸面反例能直接指出违反的合同：SysV 把仍要用的值放 red zone 后再 call，callee/返回地址可覆盖相关位置；A64 不保存仍需使用的旧 LR 就 BL，最后 RET 无法按原计划回到外层 caller；x86 预留栈空间后漏恢复就 ret，读取的不是所需原返回地址。不运行这些破坏状态的程序，也不承诺违规必然表现为同一种 fault。

## 4. E1：三个完整源文件、两个翻译单元

这一局部例只验证固定输入的正常调用和结果，不覆盖全部数域。U64 使用无符号语义，此处无溢出；mixed 的这些小整数和中间和可精确表示，使用等值判断不代表建议对任意浮点算法照搬。没有堆分配、线程、外部文件/网络、动态加载或无限循环；stdio 仅输出摘要。

将以下三个围栏分别原样保存为 `src/api.h`、`src/callee.c`、`src/caller.c`。api.h 不是第三个单独编译的 TU。布局断言失败表示本示例前提不适用，应停止并核目标，不能通过强制 pack 或删除断言继续。

### 4.1 api.h（完整）

<!-- E1_SOURCE_BEGIN api.h -->
```c
#ifndef ISA_E1_API_H
#define ISA_E1_API_H

#include <limits.h>
#include <stddef.h>

typedef unsigned long long U64;
typedef struct { U64 a; U64 b; } Pair64;

_Static_assert(CHAR_BIT == 8, "8-bit bytes required");
_Static_assert(sizeof(U64) == 8, "8-byte U64 required");
_Static_assert(ULLONG_MAX == 18446744073709551615ULL, "64 value bits required");
_Static_assert(_Alignof(U64) == 8, "natural U64 alignment required");
_Static_assert(sizeof(double) == 8, "8-byte double required");
_Static_assert(_Alignof(double) == 8, "natural double alignment required");
_Static_assert(sizeof(int) == 4, "4-byte int required");
_Static_assert(sizeof(Pair64) == 16, "16-byte Pair64 required");
_Static_assert(_Alignof(Pair64) == 8, "Pair64 alignment required");
_Static_assert(offsetof(Pair64, a) == 0, "a offset required");
_Static_assert(offsetof(Pair64, b) == 8, "b offset required");

U64 add2(U64 a, U64 b);
U64 wrap(U64 a, U64 b);
U64 pair_sum(Pair64 p);
Pair64 make_pair(U64 a, U64 b);
double mixed(int a, double b, int c, double d, int e);

#endif
```
<!-- E1_SOURCE_END api.h -->

### 4.2 callee.c（完整）

<!-- E1_SOURCE_BEGIN callee.c -->
```c
#include "api.h"

U64 add2(U64 a, U64 b)
{
    return a + b;
}

U64 pair_sum(Pair64 p)
{
    return p.a + p.b;
}

Pair64 make_pair(U64 a, U64 b)
{
    Pair64 p = {a, b};
    return p;
}

double mixed(int a, double b, int c, double d, int e)
{
    return (double)a + b + (double)c + d + (double)e;
}
```
<!-- E1_SOURCE_END callee.c -->

### 4.3 caller.c（完整）

<!-- E1_SOURCE_BEGIN caller.c -->
```c
#include "api.h"
#include <stdio.h>

U64 wrap(U64 a, U64 b)
{
    return add2(a, b) + 1ULL;
}

int main(void)
{
    Pair64 input = {3ULL, 4ULL};
    U64 w = wrap(3ULL, 4ULL);
    U64 sum = pair_sum(input);
    Pair64 made = make_pair(5ULL, 6ULL);
    double m = mixed(1, 2.0, 3, 4.0, 5);

    if (w != 8ULL || sum != 7ULL || made.a != 5ULL ||
        made.b != 6ULL || m != 15.0) {
        fprintf(stderr, "FAIL wrap=%llu pair_sum=%llu pair=%llu,%llu mixed=%.17g\n",
                w, sum, made.a, made.b, m);
        return 1;
    }
    printf("wrap=%llu pair_sum=%llu pair=%llu,%llu mixed=%.0f\n",
           w, sum, made.a, made.b, m);
    return 0;
}
```
<!-- E1_SOURCE_END caller.c -->

main 使用真实条件检查，不依赖可能被 NDEBUG 取消的 assert。U64 的 `%llu` 与 typedef 一致；可精确表示的 mixed=15 用 `%.0f` 打印，失败诊断保留更多数字。成功 stdout 的完整合同是下面一行加 LF，stderr 为空，进程 exit 0；数值检查失败写 stderr 并 exit 1。旧例 `main` 返回 7 本身是合法 C 程序的进程结果，不是语法或 ABI 错误；只是不能把它与这里明确制定的 exit 0 成功合同混为一谈。

```text
wrap=8 pair_sum=7 pair=5,6 mixed=15
```

## 5. 从源码到当前产物，再到证据

### 5.1 实际 O0 配方与停止条件

本次确认的环境是 Linux 6.18.44、x86_64，driver `/usr/bin/x86_64-linux-gnu-gcc-14`，版本 Debian 14.2.0-19（GCC 14.2.0），默认 target `x86_64-linux-gnu`。GNU ld、readelf、objdump 实际为 Debian binutils 2.44。这里的 GCC 14.2 官方手册对应 driver 主版本；binutils 的选项解释选读 2.43 手册，支持性由这次 2.44 实际命令另证，不能把手册版本写成宿主版本或称本例由 Clang 执行。

工作目录中只放上述 `src`，另准备新的空 `o0` 输出目录，失败记录不可覆盖。以下是实际 argv 的可读形式，cwd、源码 SHA、每个工具的绝对路径、全部 stdout/stderr 和终止状态在独立证据包保存。每一子进程局部固定 `LC_ALL=C`、`LANG=C`，清除 `LD_PRELOAD`、`LD_AUDIT`、`LD_LIBRARY_PATH`，令 `DEBUGINFOD_URLS` 为空；还清除会注入编译搜索路径的 GCC_EXEC_PREFIX、COMPILER_PATH、LIBRARY_PATH、CPATH、C_INCLUDE_PATH、CPLUS_INCLUDE_PATH。不修改全局环境，不打印完整环境，不从 CFLAGS/LDFLAGS 拼接隐式参数。

编译/链接/静态工具限时 30 秒，程序限时 5 秒。任何一步失败或超时先保存原流，停止依赖步骤，不消费残留 object 或运行旧文件。工具缺失、目标不匹配也停止，不安装或改跑另一 ABI。

<!-- E1_COMMANDS_BEGIN O0 -->
```bash
/usr/bin/x86_64-linux-gnu-gcc-14 -std=c11 -g -O0 -c src/callee.c -o o0/callee.o
/usr/bin/x86_64-linux-gnu-gcc-14 -std=c11 -g -O0 -c src/caller.c -o o0/caller.o
/usr/bin/x86_64-linux-gnu-gcc-14 -std=c11 -g -O0 -S src/callee.c -o o0/callee.s
/usr/bin/x86_64-linux-gnu-gcc-14 -std=c11 -g -O0 -S src/caller.c -o o0/caller.s
/usr/bin/x86_64-linux-gnu-gcc-14 -std=c11 -g -O0 -v o0/caller.o o0/callee.o -Wl,-v -o o0/abi-demo
/usr/bin/x86_64-linux-gnu-readelf -h -Ws o0/caller.o o0/callee.o
/usr/bin/x86_64-linux-gnu-objdump -drwC -Mintel o0/caller.o o0/callee.o
/usr/bin/x86_64-linux-gnu-readelf --debug-dump=frames o0/caller.o o0/callee.o
/usr/bin/x86_64-linux-gnu-readelf -h o0/abi-demo
./o0/abi-demo
```
<!-- E1_COMMANDS_END O0 -->

前两个 `-c` 各生成一个 object；两个 `-S` 只生成汇编文本，不产生或更新 `.o`。driver 链接只消费本目录本次成功生成的两个 object；`-v` 与转交 ld 的 `-v` 用于记录实际工具链。随后 readelf/objdump 消费各自明确的现存产物。两个 TU 不用 LTO、fast-math、`-march=native`、noinline 或强制帧指针开关，也不编译纸面签名矩阵。

独立 O2 复核沿用同一三源字节、同一 driver 版本，采用另一个新的空 `o2` 目录：相同配方只把 `-O0` 改成 `-O2`，所有产物/消费路径的 `o0` 改成 `o2`。作者 O0 一次、独审 O2 一次，共两次正常程序运行；不为追求某条指令或尾调用额外加配置。O2 的完成状态必须看独审实际记录，不能从这条配方推断已执行。

### 5.2 实测摘要与静态判读

2026-10-07 的作者 O0 已完成两次 `-c`、两次 `-S`、一次 driver 链接和上述静态读取，均 exit 0；随后只运行一次 abi-demo，stdout 精确为 §4 的 36 字节（含 LF），stderr 0 字节，exit 0。链接的 stdout 为 ld 版本行，stderr 为主动开启 `-v` 的 4804 字节详细流程，已完整保留，不能把它误计成程序 stderr。固定数值检查都通过；失败分支只做源码审阅，未注入错误输入或另跑负例。独立 O2 复核也于 2026-10-07 完成：从该终稿重新提取同一三源，只将优化级别和独立产物目录改为 O2/o2，完成两次 `-c`、两次 `-S`、一次 driver 链接及相同静态读取，均 exit 0；程序只运行一次，stdout 同为 §4 的 36 字节，stderr 为空，exit 0。作者 O0 与独审 O2 合计两次正常程序运行，未增加真实签名矩阵、负例或其他平台。

| 已观察对象 | 本次 O0 实际事实 | 对规则的有限印证 |
|---|---|---|
| 两个 object | ELF64、小端、AMD x86-64、ET_REL | 与本机 SysV 示例目标一致，头信息本身不证明全部 ABI |
| 符号 | caller.o 定义 wrap/main，引用 add2/pair_sum/make_pair/mixed；callee.o 定义后四者 | 两 TU 的定义/引用链明确 |
| wrap | 入口 `push rbp; mov rbp,rsp; sub rsp,0x10`；0x1e 的 call 有指向 add2 的 `R_X86_64_PLT32`，之后 RAX 加 1，leave/ret | 这是本次 compiler 选择，区别于 §3 的最小纸面序言 |
| Pair64 与 mixed | pair_sum 入口用 RDI/RSI；make_pair 返回 RAX/RDX；mixed 保存 EDI/XMM0/ESI/XMM1/EDX 并计算 double 结果 | 与这几个具体签名的分类相符，没有编译其余纸面类型 |
| CFI | 两个 object 均有 `.eh_frame`；wrap 的 FDE 为 [0,0x29)，入口 CFA=RSP+8，push 后为 RSP+16，再改以 RBP 为基准，返回前改回 RSP+8 | 元数据和本次序言可对照，未执行展开或证明所有异常路径 |
| 链接和执行 | verbose 实际调用 /usr/bin/ld，ld 为 2.44；可执行文件头为 ET_DYN / Position-Independent Executable；四项数值与退出合同通过 | 本次默认 PIE 是正常结果；不是跨 ABI 或动态绑定时序实测 |

对实际 wrap 再追一次栈：入口 S≡8；push RBP 后 S−8≡0；减 16 后 S−24≡0，因此 call 点满足对齐；call 又压 8 字节，add2 入口是 S−32≡8。add2 的 push/pop RBP 与正常 ret 之后，wrap 回到 S−24；leave 恢复 RSP=S 和原 RBP，最后 ret 回到外层 caller，RSP=S+8。纸面与实际常数不同，却维持同一调用合同。callee.o 的 add2 在 push RBP 后没有继续减 RSP，局部参数存储在当前栈指针以下且没有再 call，是此产物使用 red zone 的可见实例。

本次 O2 的独立 `wrap` 仍有符号，机器码范围为 `.text` 的 [0,0x12)：先 `sub rsp,8`，0x4 的 call 带对 add2 的 `R_X86_64_PLT32` 重定位，返回后恢复 RSP，再把 RAX 加 1 并 ret；它没有建立 RBP 帧指针，但仍有 `.eh_frame`，对应 FDE 中 CFA 从 RSP+8 变为 RSP+16，再回到 RSP+8。`main` 的 `.text.startup` 中直接调用 add2，随后加 1；该调用点不再经过 wrap 调用，这与展开 wrap 运算的形态一致，仅凭这份反汇编不区分编译器内部具体优化步骤，对外的 wrap 定义仍保留。add2/pair_sum 成为寄存器加法后直接返回，make_pair 仍经 RAX/RDX 返回成员，mixed 仍按原来的 GP/FP 输入合同计算；这些是本版本产物的形态，不能转成性能收益或完整 ABI 证明。

冻结源 SHA256：api.h 为 `fda835be93b7779ccf17a7b81a52096502a8466543291f012ede73d57a8caa02`；callee.c 为 `fa00263b58afa3f704c17b2d4f01cf3054b3a4275f4556a47ae60ffc0c433f4c`；caller.c 为 `427403d5eda6d96a806a2d96f119a62fe221a919d65a9f7dda75edb8ad92faa2`。两配置比较须先核同一源码，不能把改过的正文配旧日志。

看产物时按这个顺序连成证据链：

1. `readelf -h` 先确认 class、data、machine、type，再选择解码工具。object 的 ET_REL 与成功链接的 PIE 的 ET_DYN 各有用途，ET_DYN 本身不能当作“误生成共享库”。
2. `readelf -Ws` 区分 callee 定义和 caller 中 UND 引用；保留完整表头。GNU 传统输出的 Bind 是第 5 字段，Ndx 是第 7 字段；把 `$7=="GLOBAL"` 当过滤条件会漏掉目标，不能由空结果认定没有符号。本例直接检查完整表，不需要未提供的 libplugin.so。
3. `objdump -drwC` 将调用指令与重定位并读。object 中尚未决的重定位是链接输入，不是链接失败；名字含 PLT 的重定位也不是运行时绑定结果的观察。
4. 从调用点向前追值：mixed 的 a/c/e 分别进入 EDI/ESI/EDX，b/d 进入 XMM0/XMM1；Pair64 的两个成员从各自 GP 参数位置进入，make_pair 的返回从 RAX/RDX 接收。随后向后追 SP、返回寄存器和仍活跃的值，别把临时搬运误作 ABI 位置改变。
5. `readelf --debug-dump=frames` 查看当前存在的 CFI（Call Frame Information）。CFI 按指令区间描述 CFA、保存寄存器和返回地址的恢复规则；frame pointer 是代码中某寄存器承担的寻址角色。两者不是同一概念，存在 CFI 不等于存在 RBP 帧链，看到 RBP 序言也不能证明所有异常路径都可正确展开。

O0 的某种序言不是 ABI 的强制文本；O2 可能把 caller.c 中的 wrap 内联进 main，但未开 LTO 的跨 TU add2 调用仍提供观察入口。若 tail-call 存在，先确认尾调用条件与栈状态，而不是要求每次 O2 都出现。`noinline` 只限制内联，不冻结常量传播、尾调用或全部寄存器选择；`-fno-inline` 同样不是固定符号/栈形态的证明。调试行号和变量位置是辅助对应，优化后变量可能不存在；Compiler Explorer 或优化报告也只显示所选版本/目标的一次编译行为。

### 5.3 交叉生成与调试的下一步边界

AArch64 在本篇只有纸面推导。若另有已验证的交叉工具链，下一步应先确认 target、目标标准头、sysroot、汇编器及库：对本文两 TU 分别以 `-S` 生成各自 `.s`，另以 `-c` 生成各自 `.o`，之后让支持该目标的 objdump 消费 `.o` 并让 readelf 核 machine/data/重定位。即使不链接，`api.h` 与 stdio 所需目标头也不会凭一个 `--target` 自动出现。这里不提供带虚构 sysroot 的可执行命令，也没有运行 cross 编译；成功生成 A64 object 仍不等于 ARM 真机运行成功。

有调试需要时，可在具备匹配符号和工具的环境，以这次真正链接的 abi-demo 为入口，在 add2/调用点断下，结合当前指令检查参数和 SP，再正常返回。这是可选人工调试方法，本次未运行 GDB/LLDB；单次寄存器截图不能替代其他路径或其他 ABI 的验证。

## 6. 排障：先找失效的合同，再解释差异

| 症状或问题 | 先保留的证据与下一检查 | 能排除与不能断言 |
|---|---|---|
| 参数像是错位 | 双方完整原型、目标/选项、调用点与 callee 入口；按具体类型重新分类 | 先查 sret、GP/FP 队列和 Windows 位置槽；不能先归因于 CPU bug |
| 结果正确但进程非零 | main 返回值、stdout/stderr、exit/signal | 函数算出 7 与进程 exit 0 是不同合同；旧 main 返回 7 合法 |
| 栈或返回异常 | 找到失败指令，追 SP 时点、控制流返回地址、保存项及各条实际退出路径 | 不能只凭崩溃断定是“16 对齐错了”；不要通过执行破坏栈反例诊断 |
| 符号没出现 | 完整表头、输入是 object 还是可执行文件、链接/strip/优化记录 | 内联、删除、可见性、选错产物均可能；一个 noinline 属性不是所有符号保留保证 |
| 额外 load/store 或 RBP 使用不同 | 同源同 target 的选项、当前反汇编与 CFI、值的活跃区间 | 可能是局部变量、spill 或帧布局；静态 load 数量不是 cache miss 数 |
| object 有 UND 或 PLT 名称 | 定义方符号、相应重定位、实际链接结果 | 未决引用可完全正常；不能据此描述已发生动态绑定 |
| 不识别选项或目标 | 工具绝对路径/版本、完整 stderr、ELF 头 | 先停该依赖；不把空输出当通过、不靠安装或静默更换目标掩盖 |
| 布局断言失败或跨边界值不同 | 数据模型、对齐、pack、编译开关和各成员偏移 | 不用强制 pack “修好”语言对象到网络协议的映射；线格式应显式序列化 |

评审调用约定时至少逐项回答：声明是否一致；具体参数与返回是否分类正确；隐藏参数是否移位；每个调用点的栈准备是否充足；各正常退出路径是否恢复责任；变参是否使用匹配平台协议；C++ 对象、异常和所有权是否有单独接口合同。警告选项和符号检查能发现部分问题，不能认证所有 ABI 兼容性。

发现优化相关回归，保存失败源码、命令、产物和原流，再回到已知正确且与目标一致的配置，完整重建并做相关验证。单纯删内联汇编、改 intrinsic 或“恢复默认优化级别”并非通用恢复方案：intrinsic 也有指令集、类型及对齐前提，默认配置也未必是产品基线。

## 7. 性能用途保留为可检验的问题

E1 太短，进程启动、装载和 stdio 足以影响全程时间；本次没有 Benchmark、perf、llvm-mca、绑核、调频、压力或重复测量。反汇编可以提出“某调用增加搬运”“某路径分支更多”的假设，不能直接给出请求吞吐或 p99。

| 原有研究用途 | 需要分离的变量与证据 | 本文已经提供的部分 |
|---|---|---|
| 栈对齐与参数数目 | 合法调用点的布局；时间实验还需明确工作量 | 三 ABI 的对齐时点、home/red zone/inactive 区与正常追踪 |
| 分支 | 输入分布、实际执行次数、预测与 PMU 事件 | 只保留控制流阅读方法，不把分支条数等同误判数 |
| SIMD | 相同语义/精度、目标扩展、向量宽度、数据对齐、吞吐测量 | 普通标量边界明确，宽向量另查，不作真实矩阵 |
| inline / outline 调用 | 优化前后同源产物、代码尺寸与实际负载 | E1 产物入口和优化形态的解释边界 |
| 端序 | 对象表示与序列化合同、合法访问和结果 | 固定小端前提；不把 native struct 内存直接当线协议 |

如果以后测量，先固定业务操作和分母，再确定计时区间、输入、热身/冷启动是否属于问题、重复策略与误差需求。吞吐是完成量/时间；单操作成本可以是 cycles/已完成 operations。IPC=`instructions/cycles`、分支误判率=`branch-misses/branches` 只有对应事件可用、同一测量区间与计数范围匹配时才可解释。`cache-misses/cache-references` 必须先查该 PMU 的事件定义，不能一律称所有 cache 层的真实 miss rate。

短进程总计数不能拿来除以未记录的“操作次数”；一个 `volatile` 汇总也不保证想测的全部中间计算保留。CPU 频率、SMT、NUMA、调度、缺页和后台负载可能成为解释变量，应记录适用环境；这不是本篇执行改系统设置的指令。长尾升高可以引出这些候选原因，但没有配套证据不能选定唯一原因。

llvm-mca 18.1.8 依赖所选 CPU 调度模型；其 LSUnit 的别名假设、对 cache/内存类型的局限和乐观 load latency，使模型估计不能充当端到端实测。模型比实测乐观也不足以单独认定是缓存或频率。ASan/UBSan 等检测工具只在其检查与执行覆盖内提供线索，不是完整 ABI 或无 UB 的证明。本文不规定所有任务至少 30 次、循环 1e8 次、CV 小于 5% 或 p99 回退不超过 10%；实际阈值须来自负载、统计目的和项目预算。

## 8. 并发：ABI 正确不等于发布协议正确

本节是 C++ N4861 工作草案 [atomics.order] 的纸面说明，与 E1 的无线程 C11 样例分开。设 producer 先写普通数据 `payload=42`，然后对 atomic `ready` 做 release；consumer 对同一个 ready 做 acquire，并且明确读到了该 release 或其 release sequence 中的值，再读 payload。在无其他冲突访问等前提下，这条“先写数据 → release → 被该 acquire 读到 → 后读数据”的同步关系才支撑发布。

只看到 acquire 这个词，不知道它从哪次写读取，不能宣布数据已发布。改为 relaxed 仍保证该 atomic 访问相对于同对象其他原子访问不可分，但不提供这里所需的普通数据发布保证。若因此让普通共享访问缺乏所需同步，可能构成语言层的数据竞争；不因为 x86 常见指令顺序或某次看起来正常就变合法。fence 和 volatile 也不会自动修复任意数据竞争。

这里没有另建 memory_order.cpp，没有运行 relaxed 竞态对照或 TSan。TSan 报告可用于定位它实际观察到的问题；没有报告、重复很多次或未复现，都不是“所有竞争已经排除”的证明，也不能承诺有限重复必能暴露错误。排查这类问题应先证明语言同步关系，再查编译映射与 ISA；调用约定的寄存器表解决不了它。

## 9. 来源、证据范围与历史保留

本轮于 2026-10-07 复读已封存来源的以下局部；完整文件 hash 校验不表示全文通读。公开网页的取得日期也不是对应编译器源码 revision。

| 来源身份 | 实际选读位置 | 本文使用范围 |
|---|---|---|
| AMD64 psABI 2018-01-28，封面 Version 1.0、页脚 Draft 1.0 | §3.2.1–3.2.3，打印页 17–25；附录 A.2.1–A.2.2 | 用户态参数/返回/栈/保存规则和内核 red zone 边界；不称 2026 最新扩展全集 |
| AAPCS64 2025Q1，固定 commit c51addc3dc03e73a016a1e4edf25440bcac76431，2025-04-07 | Scope、HFA、Machine Registers、Stack/Frame Pointer、Subroutine calls、Parameter passing Stage A–C、Result return | 基础硬浮点规则；平台变体和新扩展排除 |
| Microsoft 两页，2026-10-06 快照 | calling 页 defaults/parameters/returns/saved registers；stack 页 allocation/function types | 默认 x64；页标更新时间分别 2026-05-21、2025-11-05，未运行 Windows |
| Intel SDM Vol.2，325383-086US，2024-12 | 指令格式引言；CALL 3-139/140、near64 操作 3-143；RET 4-566/567 | 普通近调用/返回；不是完整 ISA、CET 或异常审核 |
| Arm A64 Guide 1.3，102374_0103_01_en，2025-09-19 | §3、§26、§27 开头 | 官方学习指南的 A64 位宽、BL/LR/RET；不是完整 Arm ARM |
| GCC 14.2 官方手册 | Overall 的阶段/-c/-S/-o/-v；Optimize 的 O0/O2/frame pointer/inlining；Debugging 的 -g；C Dialect 的 -std=c11 | 实际 E1 的 driver 选项；本轮 web 两次 debug 页面直接 open 失败后，沿官方链接读取成功，失败记录保留 |
| GNU binutils 2.43 手册 | objdump 的 -d/-r/-M；readelf 的 -h/-s/-W/frames | 选项用途；实际安装 2.44 另有版本与命令记录 |
| Clang 18.1.8 固定文档/源码 | command guide 阶段与代码生成；AttrDocs 的 NoInlineDocs/DisableTailCallsDocs；CrossCompilation；UsersManual 优化报告与 debug 相关段落 | 仅辅助方法/边界，不冒称 Clang 或 cross 已跑 |
| llvm-mca 18.1.8；C++ N4861 固定 atomics.tex | Description / Load-Store Unit；[atomics.order] | 模型限制与语言同步条件，未作性能/线程运行 |

局部 E1 的源、完整 argv/原流、版本、静态产物摘要和 SHA256 在该修订的独立证据包中；正文自带全部可复现源码与构建链。本文仍没有 H2/M/Triple/寄存器耗尽、Windows、AArch64、手写汇编、变参、异常展开、C++ 兼容、并发与性能的运行结果。规则推导、固定输入成功、产物观察、独立全文审核和仓库门禁是不同证据，互相不能替代。

### 历史前言原文

下面两段按原顺序连续保留，共 632 字节；日期、泛化“官方参考”和“按命令实验”的措辞属于旧稿历史，不是当前基线、本轮执行指令或已经运行的记录。H1 已保留在活动正文开头。本篇在 2026-08-20 已有整篇实质正文及示例；a7159db 对本篇只追加导航，另保留 PR15/17 迁移与导航贡献，不将其他主篇的重写日期挪用于本篇。

> 知识基线：以当前标准、协议或工具链版本为准，平台差异需实测。
> 最后更新：2026-08-20。
> 官方参考：https://man7.org/。
> 验证与基准：按文中命令执行最小实验并记录指标。

> 知识成熟度：L2
> 知识基线：当前标准与工具链版本，平台差异需验证。
> 最后更新：2026-08-20。
> 官方参考：https://github.com/llvm/llvm-project/tree/main/llvm/docs
> 验证与基准：按文中步骤执行实验并记录指标。
来源：https://refspecs.linuxfoundation.org/elf/x86_64-abi-0.99.pdf｜验证与基准：objdump/readelf/Compiler Explorer。


### 原参考入口的历史身份

紧随其后的原参考块原字节保留。2012 PDF 实为 Draft 0.99.6 / 2012-07-02，只用于核对旧引用身份，与上述 2018 PDF 分开。LLVM main 是滚动目录，man7 是通用入口/手册镜像；旧 Arm latest 入口在准备时未获得可读正文，不能用新固定 PDF 倒填它的历史内容。原获取失败与后续成功各自留存。

## 参考

- https://github.com/llvm/llvm-project/tree/main/llvm/docs
- https://refspecs.linuxfoundation.org/elf/x86_64-abi-0.99.pdf
- https://developer.arm.com/documentation/102374/latest
- https://man7.org/linux/man-pages/man1/objdump.1.html


## 关联
- [编译器优化与 GPU 异构](../硬件体系结构与性能/04-编译器优化与GPU异构.md)
- [编译、链接与 ABI](../../../00_Index/学习路线/编程与计算机基础.md)



---

## 关联知识与工程落地

- **前置依赖**：
  - [01-处理器存储层次与性能工程](../硬件体系结构与性能/01-处理器存储层次与性能工程.md)：存储层次与寄存器物理模型。
- **编译与调试闭环**：
  - [04-编译器优化与GPU异构](../硬件体系结构与性能/04-编译器优化与GPU异构.md)：编译器优化流水线。
  - [10-编译链接与ABI/01-编译链接与ABI全流程](01-编译链接与ABI全流程.md)：目标文件符号与 ABI 调用约定。
  - [11-工程调试与性能分析/01-调试与性能分析方法论](../../08-工程实践与质量/调试与性能分析/01-调试与性能分析方法论.md)：GDB/LLDB 汇编级调试与栈帧还原。
- **分类与领域入口**：
  - [08-计算机体系结构与性能 README](../../../00_Index/学习路线/编程与计算机基础.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/学习路线/编程与计算机基础.md)
