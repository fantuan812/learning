---
type: Mechanism
title: "01-编译链接与ABI全流程"
status: stable
verified: []
maturity: L2
updated: 2026-10-06
sources:
  - id: cpp20-n4861
    title: "C++20 working draft N4861, fixed source revision"
    resource: "https://github.com/cplusplus/draft/tree/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source"
  - id: itanium-fixed
    title: "Itanium C++ ABI, revision 2837827a"
    resource: "https://github.com/itanium-cxx-abi/cxx-abi/blob/2837827a025f43d1fcfc37ec053d1314cfd1bba3/abi.html"
  - id: elf-gabi
    title: "ELF Object File Format 4.3 DRAFT, 2026-10-06 snapshot"
    resource: "https://gabi.xinuos.com/elf/07-pheader.html"
  - id: gcc-14-2
    title: "GCC 14.2 documentation"
    resource: "https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Overall-Options.html"
  - id: binutils-2-43
    title: "GNU binutils 2.43 documentation"
    resource: "https://sourceware.org/binutils/docs-2.43/ld/Options.html"
  - id: man-pages-6-15
    title: "Linux man-pages 6.15 source archive"
    resource: "https://www.kernel.org/pub/linux/docs/man-pages/man-pages-6.15.tar.xz"
  - id: microsoft-pe
    title: "Microsoft PE format, 2026-10-06 public-page snapshot"
    resource: "https://learn.microsoft.com/en-us/windows/win32/debug/pe-format"
---

# 01-编译链接与ABI全流程

> 知识成熟度：L2。主要承诺是解释并核对语言规则、目标文件、链接器、装载器与 ABI 的责任链；局部 Linux 样例不代表整篇或其他平台已运行验证，`verified` 保持为空。
> 当前知识基线：C++20 工作草案 N4861（固定 revision，非正式 ISO 出版文本）；ELF gABI 4.3 DRAFT 快照、固定 Itanium C++ ABI、GCC 14.2 / binutils 2.43 手册、man-pages 6.15；Windows 仅核对 Microsoft Learn 指定页面。实际阅读位置见第 7.5 节。
> 最后更新：2026-10-06。本次修订前七节的整条构建与排障链，纠正 ODR、符号、节/段、PIC、ABI 与搜索路径的混用。第 7.4 节只记录本次真实样例结果；没有运行跨 TU ODR 违规、Windows、UE、LTO 诊断、性能或压力实验。

## 1. 软件构建全生命周期概览

一个函数在源码里“有定义”，为什么最后仍会找不到？因为编译器看到的声明、链接器实际收到的对象、可执行文件记录的依赖和装载器找到的库，是四组不同的事实。排障要沿着数据实际经过的边界找证据。

### 1.1 从文本到进程：每一步交接什么

下面是常见 GCC 驱动工具链的观察模型，不是 C++ 要求的四个严格隔离进程。N4861 [lex.phases] 描述的是必须表现得如同发生的翻译阶段，允许实现融合；LTO 也可把部分优化工作延后至链接期。[GCC 14.2 Overall Options](https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Overall-Options.html) 的 `-E`、`-S`、`-c` 让我们在不同交接点停下。

```text
main.cpp + sum.h ──C++预处理──> main.ii ──编译──> main.s ──汇编──> main.o
sum.c    + sum.h ──C预处理───> sum.i    ──编译──> sum.s  ──汇编──> sum.o
                                                                    │
                                                     ar收集成员 → libsum.a
main.o + libsum.a ──由C++ driver调用链接器──> app-static

sum.c + sum.h ──C driver，PIC，控制可见性──> sum.pic.o
sum.pic.o ──链接为共享对象──> libsum.so
main.o + libsum.so ──链接──> app-shared（记录DT_NEEDED及运行路径）
app-shared ──内核读取程序头/解释器路径，动态装载器处理依赖与重定位──> 进程
```

- 预处理输入是当前文件及其实际包含的头、宏和搜索路径，输出反映这一套配置展开后的文本。C++ 预处理文件用 `.ii`，C 用 `.i`。原 `.cpp` 相同不意味着不同构建得到相同 TU。
- 编译进行语义检查并生成目标代码；这里用 `-S` 保存汇编。汇编器再把汇编变成可重定位 object，留下尚需连接到其他定义的符号引用和重定位项。
- `ar` 把 object 收进 archive；它没有把库变成进程，也没有完成所有引用的最终解析。链接器接收 object 和库，选择成员、布局内容、处理可完成的重定位，并保留动态链接所需的信息。
- 最终 C++ 程序由 `g++` 链接，使 driver 按其配置选择启动文件及语言运行库。直接调用 `ld` 不会自动等同于这一整套 driver 行为。
- 对本例动态 ELF 程序，内核根据程序头准备映像并转交 `PT_INTERP` 指定的解释器；动态装载器继续查找依赖、处理运行时重定位等。链接成功只说明完成了当次链接要求，不证明运行环境提供了兼容库。

### 1.2 贯穿全文的合法小工程 E1

保存下面三个完整文件到新建实验目录的 `src/`。固定输入只有 `2` 和 `3`；`CalculateSum` 的简单 `int` 相加不承诺处理任意输入的有符号溢出。四个源码围栏均为本篇作者教学程序，不是标准、引擎或官方文档逐字代码。

`src/sum.h` 同时能被 C 和 C++ 包含。导出宏的 Windows 分支仅展示设计形态，本文没有构建该分支；`SUM_STATIC` 用于不需要导入/导出标记的静态选库构建。

<!-- sample-source: src/sum.h -->
```c
#ifndef ABI_SUM_H
#define ABI_SUM_H

#if defined(SUM_STATIC)
#  define SUM_API
#elif defined(_WIN32)
#  if defined(BUILDING_SUM)
#    define SUM_API __declspec(dllexport)
#  else
#    define SUM_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__)
#  define SUM_API __attribute__((visibility("default")))
#else
#  define SUM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif
SUM_API int CalculateSum(int a, int b);
#ifdef __cplusplus
}
#endif

#endif
```

`src/sum.c` 由 C driver 编译；三个小对象为观察数据节提供材料。它们和实现函数在共享库构建中采用默认 hidden，可公开的入口由头文件显式标记。

<!-- sample-source: src/sum.c -->
```c
#include "sum.h"

int sum_seed = 7;
int sum_zero;
const char sum_label[] = "sum";

int SumImplementation(int a, int b) {
    return a + b;
}

int CalculateSum(int a, int b) {
    return SumImplementation(a, b);
}
```

`src/main.cpp` 用 C++20 编译。返回值检查是程序行为，不依赖可能被 `NDEBUG` 移除的断言。

<!-- sample-source: src/main.cpp -->
```cpp
#include "sum.h"
#include <cstdio>

int main() {
    const int value = CalculateSum(2, 3);
    std::printf("%d\n", value);
    return value == 5 ? 0 : 1;
}
```

第 7.2 节给出完整操作顺序。先理解各产物代表什么，再把输出用于判断，而不是只看终端最后有没有打印 `5`。

## 2. 目标文件物理拓扑结构（ELF 与 PE/COFF）

### 2.1 文件类型、section 与 segment 是不同维度

ELF 头的 `e_type` 描述文件类型：`ET_REL` 是可重定位文件，`ET_EXEC` 是可执行文件，`ET_DYN` 用于共享对象，也用于 PIE 可执行文件。扩展名不是类型判据；`libsum.a` 则是 archive 容器，内部成员才是 ELF object。`ELF64` 表示 ELF 数据表示类别，不能直接推出 CPU 为 x86-64，还要看 `e_machine`。本次查看 `readelf -h`，不按文件名猜测。

```text
ELF Header：class、data encoding、machine、type及表的位置
    ├─ Section Header Table：链接/分析视图
    │    .text/.data/.bss/.rodata 等内容 + 符号、字符串、重定位等元数据
    └─ Program Header Table：执行/装载视图（主要用于可执行文件与共享对象）
         PT_LOAD：文件区间 → 虚拟地址区间及p_flags
         PT_INTERP：动态解释器路径
         PT_DYNAMIC：动态链接信息
```

Section 按用途组织文件内容；segment 描述装载所需的范围，一个 segment 可以覆盖多个 section。`.text` 常贡献到可执行范围，但最终装载权限要看程序头的 `p_flags` 及系统规则，不能仅凭节名承诺 `r-x`。可重定位 object 通常还没有完整进程映像所需的程序头。`readelf -S` 和 `readelf -l` 因而回答不同问题。[gABI 第 7 章 Program Loading](https://gabi.xinuos.com/elf/07-pheader.html)

| 常见节或表 | 用途与容易误判的边界 |
| --- | --- |
| `.text`、`.rodata` | 代码、只读数据的常见组织；优化可删去或合并内容。vtable 的实际归属受重定位、ABI 与工具链影响，不保证固定在 `.rodata` |
| `.data`、`.bss` | 本例用于观察非零初始化与零初始化对象。`SHT_NOBITS` 的内容不占文件字节，但 section header 仍有大小等元数据 |
| `.symtab` / `.dynsym` | 前者常用于链接或调试分析，可被剥离；后者服务动态链接，既可含定义也可含未定义导入，不是纯“API 导出清单” |
| `.strtab` / `.shstrtab` | 前者通常保存符号名相关字符串；后者保存 section 名称。符号表关联哪个字符串表由表间索引决定，不能混为一张表 |
| `.rel.*` / `.rela.*` | 常见重定位节命名；真正格式由 section 类型决定，描述何处需要按哪种规则修补 |

`.bss` 的文件表示与装载零填充是相邻但不同的事实：对 `PT_LOAD`，当 `p_memsz > p_filesz` 时，超过文件初始化区域的内存字节应为零。这不规定内核一定用某种“零页映射”实现，也不表示整个 ELF 文件省掉所有相关元数据。[gABI 第 3 章 Sections，`sh_offset`/`sh_size` 与特殊节](https://gabi.xinuos.com/elf/03-sheader.html)

### 2.2 重定位连接“引用”与“定义”

编译 `main.cpp` 时声明让编译器知道如何生成调用，却没有给出最终 `CalculateSum` 地址。object 用符号和重定位表达待完成工作。一个重定位项至少应这样读：

- `r_offset`：修补位置。对 `ET_REL` 是被修改 section 内的偏移；对可执行文件或共享对象是该存储位置的虚拟地址
- `r_info`：目标相关的重定位类型及符号表索引。并非每一种重定位都需要某个外部函数符号
- addend：`REL` 从待修补位置取得隐式加数，`RELA` 通过 `r_addend` 显式提供加数

计算方式及写回宽度由目标 psABI 的重定位类型决定，例如某些类型计算符号与当前位置之间的差，而非写入绝对地址。链接时能完成的部分由链接器处理，依赖运行时地址或符号绑定的部分可能留给装载器。必须把 `readelf -rW` 与符号表、反汇编一起读，不能把它叫“外部符号绝对地址表”。[gABI 第 6 章 Relocation，6.1](https://gabi.xinuos.com/elf/06-reloc.html)

### 2.3 PE/COFF 的简短对照

Microsoft 的 COFF object 是链接器输入，PE image 是装载器输入。PE 的 RVA 是映像内相对基址的虚拟地址，通常不等于磁盘文件偏移；VA 则包含实际映像基址。IAT 的项用于导入符号地址，基址重定位处理映像未落在首选基址时的地址调整。这些机制可与 ELF 的“引用解析”“地址修正”类比，但不能把 PE 写成另一套 ELF 节名，也不能从 ELF 的 PLT 推导 Windows IAT 的全部绑定过程。[Microsoft PE Format：General Concepts、Import Address Table、The .reloc Section](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)

## 3. 符号修饰（Name Mangling）与链接期决议

### 3.1 语言声明与目标文件名字之间还有平台 ABI

C++ 重载、命名空间、模板等需要让工具链区分实体，常由 mangling 编码进符号名；具体编码是 ABI 的事。`extern "C"` 指定 C language linkage，相关名字表示、调用约定等依实现决定。它不是跨平台“强制裸名”开关，也不把 C++ 类、异常或 `std::string` 自动变成 C 能安全处理的接口。[N4861 declarations.tex，[dcl.link]](https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/declarations.tex#L8151-L8270)

E1 的头用 `__cplusplus` 保护 linkage specification，所以 C 编译器不必理解 C++ 语法。C 定义和 C++ 调用者各自按对应语言编译，再通过兼容的平台 C ABI 衔接。本次 object 中观察到名字为 `CalculateSum`；这只是本目标事实。Microsoft 的 C 修饰名文档按调用约定和目标区分前导下划线、后缀及 ARM64EC 等情形，足以反证“C 函数在所有平台都叫 `_foo`”或“extern C 在所有平台都叫 `foo`”。[Microsoft Decorated names：Format of a C decorated name](https://learn.microsoft.com/en-us/cpp/build/reference/decorated-names?view=msvc-170)

如果调用者按 C++ linkage 声明、实现却按 C linkage 提供，二者在一些 ABI 上会形成不匹配的符号名，导致链接错误。但看到任意 `undefined reference` 时先核实际输入和原始符号，不能立即归咎于漏写 `extern "C"`；本次没有另加该变体实验。

### 3.2 binding、visibility、COMDAT 与 ODR 不可互相替代

在本节的 ELF 链接语境，binding 区分 `STB_LOCAL`、`STB_GLOBAL`、`STB_WEAK` 等；visibility 则另行描述定义是否对组件外可见、是否可被抢占。常说的强定义，需落到具体符号条目确认。链接多个 `ET_REL` 时，同名已定义 global 符号冲突通常报错；与同名 weak 定义并存时，global 定义优先。其他 weak 行为不能凭“总是任意取一个”概括成跨平台规则。[gABI Symbol Table：5.2、5.4](https://gabi.xinuos.com/elf/05-symtab.html)

C++ 标准没有规定所有 `inline` 或模板实体都必须生成 weak 符号。Itanium ABI 5.2 描述 vague linkage、COMDAT 分组及部分函数/静态数据/虚表副本的发射与去重。COMDAT 去掉重复实现，是 ABI 层机制；它不检查类的每个成员是否同义，更不能让语言层不符合 ODR 的程序变合法。[固定 Itanium ABI：5.2 Vague Linkage](https://github.com/itanium-cxx-abi/cxx-abi/blob/2837827a025f43d1fcfc37ec053d1314cfd1bba3/abi.html)

本例 `main.o` 的 `U CalculateSum` 只表示在这一符号表里未定义，随后可以由 archive 成员或 DSO 提供。最终动态程序中仍出现 `U` 也可能是合法动态导入。`nm -C` 的可读名用于理解签名，原始名、版本后缀和 visibility 用于判断链接器真正匹配了什么。

## 4. 单一定义规则（ODR）与配置引起的布局差异

### 4.1 为什么成功链接不能证明跨 TU 定义一致

C++ 允许满足特定条件的类、inline 实体、模板等在不同 TU 中有多个定义；普通非 named-module 情形要求相同 token 序列，并满足名称查找等条件。若同一 `PlayerData` 在一个 TU 包含 `guildId`、另一 TU 不包含，就违反该条件。这里应判为 ill-formed, no diagnostic required（IFNDR），不是“语言保证链接成功后静默崩溃”；也不能推广成所有 ODR 错误都无需诊断。[N4861 [basic.def.odr]，多定义条件及诊断要求](https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/basic.tex#L567-L706)

风险的因果链是：宏改变 TU 中的类型定义 → 编译器按各自视图生成成员访问或虚调用 → 若在同一个程序边界混用，不再满足语言和 ABI 的共同前提。某个平台可能表现为偏移误读、破坏数据或错误虚调用，也可能被诊断或呈现其他行为；不能给违规程序规定固定输出。链接器通常处理符号和重定位，不是把所有类定义做语义一致性验证后再“合并类”。

### 4.2 合法观察 E2：两个完整独立程序

下面 `src/layout.cpp` 每次只形成一个 TU、一个程序。它先确认 `PlayerData` 是标准布局，再使用 `offsetof`，分别构建宏关/宏开两个程序。二者不互链、不传对象、不加载对方 DSO，因此不制造上面的跨 TU 违规组合。

<!-- sample-source: src/layout.cpp -->
```cpp
#include <cstddef>
#include <cstdio>
#include <type_traits>

struct PlayerData {
    int id;
#ifdef ENABLE_GUILD_SYSTEM
    int guildId;
#endif
    float health;
};

static_assert(std::is_standard_layout_v<PlayerData>);

int main() {
    std::printf("size=%zu align=%zu health_offset=%zu\n",
                sizeof(PlayerData), alignof(PlayerData),
                offsetof(PlayerData, health));
    return 0;
}
```

两行输出只说明各自实际目标的大小、对齐与成员偏移；不要硬编码为“所有系统无宏必为 8、有宏必为 12”。即使某目标两种布局碰巧有相同大小，token 不一致问题也不会因此消失。

### 4.3 原虚表反例的正确用途：静态分析，不运行

```cpp
// 静态反例片段，不构成可运行工程，也不构建宏不一致的组合。
struct BaseService {
    virtual void ServiceA() = 0;
#ifdef ENABLE_HOTFIX_SERVICE
    virtual void HotfixService() = 0;
#endif
    virtual void ServiceB() = 0;
};
// 若同一程序中的提供方和调用方对该实体采用不同宏配置，违反ODR。
// 不给它配main，不承诺ServiceB调用打印Hotfix或必然崩溃。
```

在采用相应 Itanium 布局规则的特定实现中，增加虚函数可能改变调用方和提供方理解的虚表项次序，这解释了原反例想表达的 ABI 风险。但虚表有 address point、类型信息、调整项和析构项等；不能不经目标 ABI 分析就把它画成固定 `slot 1 = 地址偏移 8` 的普遍事实。Itanium 文档也明确平台供应商最终决定具体 ABI。[固定 Itanium ABI：Introduction、2.5 Virtual Table Layout](https://github.com/itanium-cxx-abi/cxx-abi/blob/2837827a025f43d1fcfc37ec053d1314cfd1bba3/abi.html)

预防应落在统一公共头、共享构建配置、核对包含路径与宏、干净地重建所有受影响模块，并明确 SDK 支持的配置组合。GCC 14.2 文档把 ODR 警告放在 LTO 中且默认启用，但不承诺检查完备；`-Werror=odr` 只把实际产生的相应警告升级为错误，不能制造原本漏掉的诊断。无警告、能链接、一次运行正常都不是 ODR 证明。本次未运行 LTO，也未据此声称 Clang 有相同行为。[GCC Warning Options：`-Werror=`、`-Wno-odr`](https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Warning-Options.html)

## 5. 动态链接与运行时装载：GOT 与 PLT

### 5.1 PIC 解决什么，不解决什么

E1 的共享库在支持该选项的 Linux ELF 目标上用 `-fPIC` 生成位置无关代码，使地址相关访问采用适合动态装载的序列，降低必须改写共享代码页的需求。它不等于“整份文件任何位置都没有绝对地址”；数据指针、GOT 内容和动态重定位仍可能依赖装载地址。可写数据也不因代码可共享就成为进程间自动共享状态。`.dll` 更不应一概套用 GCC 的 `-fPIC` 开关。[GCC 14.2 Code Gen Options：`-fpic`、`-fPIC`、`-fpie`](https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Code-Gen-Options.html)

### 5.2 传统 AMD64 ELF PLT 的有条件示意

以下图只表达 AMD64 psABI Draft 0.99.6（2012-07-02）第 5.2 节 small/medium 模型中传统 PLT 的惰性绑定形态。先决条件是该调用确实使用这种 PLT 项，且没有被要求立即绑定；它不是所有架构、所有当前编译选项或 Windows 的统一执行路线。

```text
调用 foo@plt
  └─ PLT通过GOT项间接跳转
       ├─ 尚未绑定：跳到PLT内的解析入口路径
       │             → 传递重定位索引/对象信息给装载器
       │             → 查找符号并修补该GOT项 → 执行foo
       └─ 已绑定：GOT项已有目的地址 → 直接经该项跳往foo
```

传统形态把动态地址写在 GOT 中，从而不必每次重写 PLT 的共享指令。立即绑定（例如合适的链接选项，或 glibc 的单进程 `LD_BIND_NOW=1`）可在启动时提前解析；GCC 的 `-fno-plt` 在适用的 PIC 外部调用中改为调用点通过 GOT 取地址，因而不走这套惰性 PLT 路径。优化、可见性、直接绑定及目标不同也可改变实际代码。判断本产物用 `readelf -rW` 与 `objdump -drwC`，不能从源代码有函数调用就断定必经某个桩。[AMD64 ABI 0.99.6，第 5.2 节与图 5.2](https://refspecs.linuxfoundation.org/elf/x86_64-abi-0.99.pdf)

E1 的默认和 `LD_BIND_NOW=1` 两次调用都应得到 `5`，但等值输出只能检验这个功能结果，不能证明惰性绑定实际发生、解析时点或耗时。惰性绑定可能减少启动时对未调用函数的解析，代价可能转移到首次调用；本次没有测性能，不承诺微秒数或启动收益。[man-pages 6.15 `ld.so(8)`，Environment/LD_BIND_NOW](https://www.kernel.org/pub/linux/docs/man-pages/man-pages-6.15.tar.xz)

## 6. C++ 跨动态库 ABI 稳定性、导出与运行路径

### 6.1 先区分二进制兼容、源兼容与调用点行为

ABI 是二进制参与方共享的约定，包括调用方式、对象布局、名字表示、异常等机制及其平台实现。源码还能重新编译，不代表旧二进制可直接混用；导出名未变，也不证明对象边界兼容。

| 变更 | 需要检查的后果 |
| --- | --- |
| 增删数据成员、改变成员类型或布局选项 | 可能改变大小、对齐和偏移。旧调用方仍按旧布局访问，不能靠成功导出修复 |
| 改虚函数集合、顺序、继承结构 | 可能改变虚表及调整规则，须按具体 ABI 和全部调用方审计，不承诺某个固定 slot |
| 改形参/返回类型或调用约定 | 可能同时改变符号名、传参与返回规则。C linkage 名字可能仍相同，反而不能靠名字发现全部错配 |
| 仅改函数默认实参 | 默认实参不是函数类型的一部分；通常不改变导出签名，却会改变重新编译调用点所使用的值 |
| 跨边界暴露 STL 类型 | 要核供应商、版本承诺、ABI 宏、运行库及构建配置；不能说所有版本的容器布局都不同，也不能仅靠 `-std=c++20` 推断一致 |

例如旧头声明 `void SetBudget(int n = 10);`，新头改为默认 `20`：省略实参的旧调用点已经按旧头生成调用；只换 DSO 不会自动把它改成新值，新编译调用点则使用新默认值。这是源码在调用点补实参的行为，不等于新增无参导出函数。还需遵守默认实参及 inline/模板等相关 ODR 条件，不能以“默认参数不属于函数类型”为由任意混用不一致定义。[N4861 [dcl.fct.default]，调用示例与“不属于函数类型”段](https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/declarations.tex#L3732-L4012)

libstdc++ 从 GCC 5.1 引入的 dual ABI 用不同名字容纳新旧 `std::string`/`std::list` 实现，`_GLIBCXX_USE_CXX11_ABI` 与 `-std` 的选择不是同一件事。Microsoft 的 2026-10-06 页面快照则记录 VS 2015 以后兼容承诺及链接器/Redistributable 不早于最新输入工具集、`/GL`/`/LTCG` 需精确匹配等限制。二者都要求读具体供应商合同，不能概括为“同编译器必兼容”或“换版本必不兼容”。[libstdc++ Dual ABI](https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dual_abi.html)、[Microsoft C++ binary compatibility](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170)

### 6.2 导出宏是接口的一层，不是完整 SDK 合同

第 1.2 节 `SUM_API` 是作者独立的跨平台示意，不是 UE `MODULE_API` 源码，本次没有读取或验证 UE 实现。ELF 共享库使用 `-fvisibility=hidden`，再为公共函数指定 default visibility，可缩小意外暴露的接口。Windows 的 `__declspec(dllexport)`/`dllimport` 是一种方式，`.def` 等导出表控制也是方式，并非唯一途径。[GCC visibility](https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Code-Gen-Options.html)、[Microsoft Exporting from a DLL](https://learn.microsoft.com/en-us/cpp/build/exporting-from-a-dll?view=msvc-170)

本例检查 `CalculateSum` 出现在动态定义符号中，并确认 `SumImplementation` 与 `sum_*` 没成为动态公开 API。不要求 `.dynsym` 总共只有一行：运行库、工具链生成项及动态导入同样可能出现。可见性控制也不隔离恶意调用，更不保证异常类型、分配器、对象布局或标准库 ABI 相容。

设计长期二进制边界时，可优先采用窄 C 风格接口、不透明句柄和显式版本/能力查询；约定创建/销毁成对由同一组件负责，写清缓冲区容量、所有权转移、错误返回、线程与生命周期。若必须跨边界传递 C++ 对象或异常，则把支持的编译器、运行库和配置写进 SDK 合同。E1 只有两个整数输入与一个整数返回，未覆盖资源所有权或异常的运行验证。

### 6.3 `-L` 与运行时依赖查找处在不同阶段

`-Lbuild -lsum` 告诉链接器到哪里选输入，不能保证部署后装载器在那里找库。E1 给 `libsum.so` 设 SONAME，并核 `app-shared` 的 `DT_NEEDED` 为无 slash 的 `libsum.so`；再把字面 `$ORIGIN` 写入 `DT_RUNPATH`，让相邻库可按本对象位置查找。命令中的单引号是防止 shell 提前把 `$ORIGIN` 当普通环境变量展开。

以下是所核 man-pages 6.15 描述的 glibc `ld.so` 语境，不能无条件推广到其他 libc/操作系统：依赖字符串若含 slash，按相对或绝对路径处理；不含 slash 时，再按适用条件考虑 `DT_RPATH`（不存在 RUNPATH 时）、`LD_LIBRARY_PATH`（secure-execution 时忽略）、`DT_RUNPATH`、`/etc/ld.so.cache` 和默认目录。缓存的硬件能力目录、`-z nodefaultlib` 等会改变细节；默认目录可能是 `/lib`/`/usr/lib`，部分 64 位架构用 `/lib64`/`/usr/lib64`，不是所有机器一张固定目录表。

RUNPATH 只帮助查找所属对象直接 `DT_NEEDED` 的依赖。设 `app → A → B`，app 的 RUNPATH 可帮助找到 A，不自动传递去找 B；A 需要自己的适当 RUNPATH 或由其他有效规则找到 B。A 的 `$ORIGIN` 指 A 所在目录，不是 app 目录。这也解释为什么“主程序有 `$ORIGIN`”仍可能缺间接依赖。[man-pages 6.15 `ld.so(8)`：Description、Dynamic string tokens](https://www.kernel.org/pub/linux/docs/man-pages/man-pages-6.15.tar.xz)

本例只覆盖 app 到同目录 libsum.so 的直接依赖，不搭建第三组间接依赖实验。真实部署还要检查每个对象的元数据、目录布局及允许的环境，不用一行 RPATH 配置宣称解决所有打包问题。

## 7. 命令行观察、反例与完整排障

### 7.1 先定位失败阶段

```text
失败发生在哪里？
├─ 预处理/编译阶段：还没得到所需object
│  ├─ 头找不到/声明或类型错误 → 核包含路径、宏、语言模式及真实编译命令
│  └─ 只有部分文件重建 → 核构建依赖与旧object来源，再重建受影响输入
├─ 链接阶段
│  ├─ undefined reference
│  │  ├─ 提供者源文件有定义吗？其object真的在链接命令里吗？
│  │  ├─ archive成员是否含目标定义？是否放在产生引用的object之后？
│  │  ├─ 原始名/签名/调用约定/目标架构是否匹配？
│  │  └─ DSO动态可见性、符号版本、模板实例化或虚表发射条件是否满足？
│  └─ multiple definition → 核重复object、头内普通定义、重复库输入与具体符号
└─ 装载或运行期解析阶段：已有可执行文件
   ├─ 解释器/库找不到 → 先readelf -l/-d核PT_INTERP、NEEDED、slash与搜索规则
   ├─ symbol lookup / version错误 → 找到的是哪份库？是否导出所需名和版本？
   └─ 成功加载但行为不对 → 核ABI、布局、所有权与配置；成功加载不是兼容性证明
```

GNU ld 通常在命令行出现 archive 时按当时未解决的引用提取成员，后面才出现的新引用不会自动使之前的 archive 重搜。它在同目录搜索 `-lsum` 时可优先选 `.so`，所以本例静态选库必须显式列 `build/libsum.a` 并放在 `main.o` 后。此处 `app-static` 仅指 sum 从 archive 取得，系统/标准库仍可动态依赖。[GNU ld 2.43 Options：`-l`、`-L`、`-Bstatic`](https://sourceware.org/binutils/docs-2.43/ld/Options.html)

遇到虚表相关未定义引用，不要一律让所有纯虚函数补实现。普通纯虚函数通常不要求提供定义，显式限定调用等情形另论；如果创建该类或派生类对象，纯虚析构也需要定义。采用所述 Itanium 规则的平台还需查 key function：其定义所在 TU 影响虚表发射，缺定义或漏链接对应 object 都值得检查。“第一个非内联虚函数”也要按该 ABI 的 non-pure 等完整条件判断。[N4861 [class.abstract]](https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/classes.tex#L4105-L4204)、[[class.dtor]](https://github.com/cplusplus/draft/blob/6aea7f6be0895b9dd361c6562bdee2f3809e4fa0/source/classes.tex#L2260-L2271)、[Itanium ABI 5.2.3](https://github.com/itanium-cxx-abi/cxx-abi/blob/2837827a025f43d1fcfc37ec053d1314cfd1bba3/abi.html)

`readelf -Ws` 看条目属性，`nm -u` 看该表未定义项，`nm -D --defined-only` 看动态定义，`readelf -VW` 辅助看符号版本，`objdump -drwC` 连同重定位解释指令。它们是观察工具而非语言正确性证明。优先静态检查；`ldd` 只考虑自己构建且可信的产物，不能对来源不明的可执行文件使用。本例不需要执行 `ldd`。[GNU nm 2.43](https://sourceware.org/binutils/docs-2.43/binutils/nm.html)、[GNU readelf 2.43](https://sourceware.org/binutils/docs-2.43/binutils/readelf.html)、[man-pages 6.15 `ldd(1)` Security](https://www.kernel.org/pub/linux/docs/man-pages/man-pages-6.15.tar.xz)

### 7.2 E1/E2 的完整有限命令

前提：已有适用的 Linux ELF GCC/GNU binutils/glibc 环境，`src/` 保存上文四个源码文件，当前目录是可写的独立实验目录。以下是本次 O0 命令；版本号不满足或目标不同应停止对应平台结论，不安装或改系统配置来伪造条件。所有子进程使用局部空环境加明确 PATH/locale；这既不读取秘密，也不修改 shell 的全局环境，并使 `LD_LIBRARY_PATH`、`LD_PRELOAD`、`LD_AUDIT`、`LD_BIND_NOW` 等不会被继承。固定 PATH 要包含本次实际工具。

```bash
abi_run() { env -i PATH=/usr/bin:/bin LC_ALL=C "$@"; }
abi_run uname -m
abi_run gcc --version
abi_run g++ --version
abi_run gcc -dumpmachine
abi_run g++ -dumpmachine
abi_run g++ -print-prog-name=ld
abi_run ld --version
abi_run ar --version
abi_run readelf --version
abi_run nm --version
abi_run objdump --version
abi_run getconf GNU_LIBC_VERSION
```

这里 `-print-prog-name=ld` 实际返回 `ld`，局部 PATH 解析到 `/usr/bin/ld`；若返回别的路径应核那个工具。后续 `-###` 记录 driver 的拟调用链，实际链接命令的 `-Wl,-v` 输出选定链接器版本，共同避免把另一个 PATH 中的 ld 误当实际使用者。

<!-- sample-commands: build-inspect-run -->
```bash
abi_run mkdir -p build
abi_run g++ -std=c++20 -O0 -E src/main.cpp -o build/main.ii
abi_run g++ -std=c++20 -O0 -S build/main.ii -o build/main.s
abi_run g++ -c build/main.s -o build/main.o
abi_run gcc -std=c17 -O0 -DSUM_STATIC -E src/sum.c -o build/sum.i
abi_run gcc -std=c17 -O0 -S build/sum.i -o build/sum.s
abi_run gcc -c build/sum.s -o build/sum.o
abi_run readelf -hSWrWs build/main.o
abi_run readelf -hSWrWs build/sum.o
abi_run nm -u build/main.o
abi_run nm --defined-only build/sum.o
abi_run objdump -drwC build/main.o
abi_run ar rcs build/libsum.a build/sum.o
abi_run ar t build/libsum.a
abi_run g++ -### build/main.o build/libsum.a -o build/app-static
abi_run g++ -Wl,-v build/main.o build/libsum.a -o build/app-static
abi_run gcc -std=c17 -O0 -fPIC -fvisibility=hidden -DBUILDING_SUM -c src/sum.c -o build/sum.pic.o
abi_run gcc -shared -Wl,-v -Wl,-soname,libsum.so build/sum.pic.o -o build/libsum.so
abi_run g++ -### build/main.o -Lbuild -lsum -Wl,--enable-new-dtags '-Wl,-rpath,$ORIGIN' -o build/app-shared
abi_run g++ -Wl,-v build/main.o -Lbuild -lsum -Wl,--enable-new-dtags '-Wl,-rpath,$ORIGIN' -o build/app-shared
abi_run readelf -hSWl build/app-static
abi_run readelf -dW build/app-static
abi_run readelf -hSWl build/libsum.so
abi_run readelf -dW build/libsum.so
abi_run readelf -hSWl build/app-shared
abi_run readelf -dW build/app-shared
abi_run readelf -rW build/app-shared
abi_run readelf -Ws build/libsum.so
abi_run nm -D --defined-only build/libsum.so
abi_run nm -D -u build/app-shared
abi_run readelf -VW build/app-shared
abi_run objdump -drwC build/app-shared
abi_run /lib64/ld-linux-x86-64.so.2 --version
abi_run ./build/app-static
abi_run ./build/app-shared
abi_run env LD_BIND_NOW=1 ./build/app-shared
abi_run g++ -std=c++20 -O0 src/layout.cpp -o build/layout-off
abi_run g++ -std=c++20 -O0 -DENABLE_GUILD_SYSTEM src/layout.cpp -o build/layout-on
abi_run ./build/layout-off
abi_run ./build/layout-on
```

上面解释器路径由本次 `readelf -l` 的 PT_INTERP 取得后核对；复现到其他目标时必须先读取实际值，不能照抄路径并据此推断 glibc。正向程序总计只运行五次：静态选库、共享默认、共享立即绑定、布局关、布局开各一次；装载器的 `--version` 只是版本探测。

### 7.3 两个预期链接失败：必须命中目标诊断

<!-- sample-commands: expected-link-failures -->
```bash
abi_run g++ build/main.o -o build/missing-provider
abi_run g++ build/main.o build/sum.o build/sum.o -o build/duplicate-provider
```

逐条记录实际 argv、stdout、stderr 和退出码，不使用 shell 的“失败就当通过”替代检查。第一条省略提供者，必须在非零退出的诊断中看到 `CalculateSum` 的 undefined reference；第二条直接列同一强定义 object 两次，必须看到涉及 `CalculateSum` 的 multiple definition。其他失败如工具缺失、语法错误或路径错误，都不是期望反例。不要运行失败产物，也不增加违规 ODR 运行。

正向静态证据还需互相印证：`main.o` 引用目标符号、`sum.o` 提供定义；archive 包含 sum.o；`app-static` 不含 `libsum.so` 的 DT_NEEDED，`app-shared` 则含该项；共享 API 有动态定义而隐藏实现无动态公开定义；RUNPATH 保留字面 `$ORIGIN`。库文件与可执行文件共目录再运行，才构成本例限定环境下的完整链条。

### 7.4 本次实际结果与未覆盖项

2026-10-06 在本机完成作者 O0 验证：架构 `x86_64`，C/C++ 目标三元组均为 `x86_64-linux-gnu`；gcc/g++ `14.2.0 (Debian 14.2.0-19)`，ar/readelf/nm/objdump 及实际链接器 `/usr/bin/ld` 均为 GNU binutils `2.44`。driver 的实际链接输出带 `-pie`，两个应用均为 ELF64 `ET_DYN` PIE。PT_INTERP 为 `/lib64/ld-linux-x86-64.so.2`，该解释器报告 glibc `2.41 (Debian 2.41-12+deb13u4)`。这些是本次观察，不是用 2.43 手册版本代填的机器信息。

源码先写入本页，再按四个源码围栏原字节提取到文件；执行的是第 7.2/7.3 节相同 argv，未另换一个更简单的程序。以下七项是全部正常运行与预期失败，stdout 按行列出，所有正常程序 stderr 为空：

| 对象与条件 | 实际 stdout 或目标 stderr 摘要 | 实际退出码 |
| --- | --- | --- |
| E1 `app-static`，局部干净环境 | `5` | 0 |
| E1 `app-shared`，局部干净环境 | `5` | 0 |
| E1 `app-shared`，仅该进程 `LD_BIND_NOW=1` | `5` | 0 |
| E2 宏关，独立 `layout-off` | `size=8 align=4 health_offset=4` | 0 |
| E2 宏开，独立 `layout-on` | `size=12 align=4 health_offset=8` | 0 |
| 缺提供者，未运行产物 | `undefined reference to`，目标 `CalculateSum` | 1 |
| 重复列 `sum.o`，未运行产物 | `multiple definition of`，含目标 `CalculateSum`，也报告同一 object 内的其他强定义 | 1 |

静态检查把结果与构建链相连：

- `main.o`/`sum.o` 为 `ET_REL`，没有程序头；main 的 `.rela.text` 有针对 `CalculateSum` 的 `R_X86_64_PLT32`，addend 为 `-4`。这是真实目标的一种重定位，不是所有架构共同编码
- `sum_seed`、`sum_zero`、`sum_label` 在本次 sum.o 中分别落在 `.data`、`SHT_NOBITS .bss`、`.rodata`。app-shared 的一个 RW `PT_LOAD` 为 `FileSiz=0x270`、`MemSiz=0x278`，可对应理解文件初始化范围与内存零填充的差别
- archive 成员是 `sum.o`；app-static 的 DT_NEEDED 只有 `libc.so.6`，没有 `libsum.so`。app-shared 的 DT_NEEDED 为 `libsum.so` 和 `libc.so.6`，RUNPATH 为字面 `$ORIGIN`；libsum.so 的 SONAME 为 `libsum.so`
- libsum.so 的动态定义中有 `CalculateSum`；`SumImplementation` 和三个 `sum_*` 对象在普通符号表为 LOCAL，未成为动态公开定义。其 `.dynsym` 仍含工具链的弱未定义项，说明动态符号表不等于一个 API 列表
- app-shared 有 `CalculateSum` 的 `R_X86_64_JUMP_SLOT`，反汇编可见相应 PLT 调用；这是产物结构观察。没有记录运行时 GOT 改写过程，所以不能把三次 `5` 当作惰性解析时序的证据

原始 stdout/stderr、实际 argv、退出码、源字节 hash 与提取映射随本次修订证据保存；本页的完整源码、命令和实际摘要可独立复现，不依赖不可访问的外部工作目录。没有执行原 ODR 违规组合、默认参数变体、间接依赖工程、LTO、Windows/UE、sanitizer 或性能测试；整篇仍为 L2。

### 7.5 来源身份与实际核对范围

下表是 2026-10-06 的实际段落阅读边界，不是“已读完整标准/源码仓”的声明。手册版本与第 7.4 节机器安装版本分别记录。

| 来源 | 版本及本次核对位置 | 支持范围 |
| --- | --- | --- |
| C++ 工作草案 | N4861 tag 对应 revision `6aea7f6be0895b9dd361c6562bdee2f3809e4fa0`；`lex.tex` [lex.phases]，`basic.tex` [basic.def.odr]，`declarations.tex` [dcl.link]/[dcl.fct.default]，`classes.tex` [class.abstract]/[class.dtor] 的相关段落 | 翻译阶段、跨 TU 条件、语言链接、默认实参及纯虚函数；未读完整草案，也非付费 ISO 正式文本 |
| Itanium C++ ABI | revision `2837827a025f43d1fcfc37ec053d1314cfd1bba3`；Introduction、2.5 的布局组成/顺序、5.2 的 vague linkage/虚表/key function | 特定 ABI 机制；文档声明平台供应商有最终定义权，未据此替所有平台保证布局 |
| ELF gABI | 4.3 DRAFT，2026-10-06 页快照；第 3 章 NOBITS/字符串表，第 5 章 binding/visibility，第 6 章 relocation，第 7 章程序头/PT_LOAD/PT_INTERP | ELF 链接与装载结构；快照未固定源仓 commit，不称正式已发布标准 |
| GNU 手册 | GCC 14.2 Overall/Code Gen/Warning 相关选项；binutils 2.43 ld 的选库/PIE/dtags、nm 的 U/动态表、readelf 的头/重定位/动态信息/版本选项 | 文档支持的工具语义，未把文档版本冒充宿主安装版本 |
| Linux man-pages | 官方 6.15 包内 `elf(5)` 的 class/type/machine、`ld.so(8)` 的搜索/token/绑定变量、`ldd(1)` Security | 本文 glibc 语境与工具使用边界；未读 glibc 源码 |
| AMD64 psABI | Draft 0.99.6，2012-07-02，第 5.2 节 PLT 与图 5.2 | 传统 small/medium PLT 示意，未声称所有现代构建同样发射 |
| Microsoft Learn | 2026-10-06 公共页快照：PE General Concepts/IAT/Base relocation、Decorated names 的 C 修饰名、DLL 导出方式、Binary compatibility 的承诺及限制 | Windows 文档静态对照；页面的源 commit 元数据不等于读取其源仓，未运行 Windows |
| libstdc++ | 2026-10-06 Dual ABI 公开页快照，GCC 5.1 引入历史、`_GLIBCXX_USE_CXX11_ABI` 与 `-std` 的区别 | 供应商兼容配置案例，未读完整标准库实现 |

旧参考入口继续保留：cppreference translation 是第三方参考，本轮标准语义依固定 N4861；旧 ELF PDF 实际是 TIS ELF Specification v1.2（1995 年 5 月），前言面向 32-bit Intel 环境，不能当 ELF64 的完整规格。其封面与前言已核对。下方保留旧链接和措辞供历史追溯，当前解释以上文为准。

### 7.6 历史贡献与原前言保留

2026-09-03 已有整篇实质改写贡献（`a7159db3`）；后续 PR15/PR17 完成迁移和导航调整。本次是对既有全文的继续修订，不能称为首次整理或首次成文。旧作者的十类教学用途已在现有各节保留并纠错，不复制旧 1–7 节形成第二套正文。

下面连续保留来自已有版本的历史前言。其旧日期、“官方参考”标签及“运行 ODR 违规/记录基准”均是历史文本，其中验证计划未作为本轮实验执行，也不是本轮操作指令或已完成记录；当前真实基线、L2 范围及样例结果以前文为准。

> 验证与基准：按文中命令使用 `readelf`/`nm`/`objdump` 查看符号表与重定位项，运行 ODR 违规最小复现实验，记录基准测试结果。

> 知识成熟度：L2（涵盖预处理/编译/汇编/链接全流程、ELF/PE 结构、GOT/PLT 延迟绑定、ODR 违规排障与 ABI 稳定性）。
> 知识基线：C++20 标准；Linux ELF64 / Windows PE32+ (PE/COFF)；Itanium C++ ABI 与 MSVC ABI。
> 官方参考：[cppreference translation](https://en.cppreference.com/w/cpp/language/translation_phases)、[Itanium C++ ABI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html)、[ELF Specification](https://refspecs.linuxfoundation.org/elf/elf.pdf)。
> 最后更新：2026-08-20。

---

## 8. 关联知识与工程落地

- **前置依赖**：
  - [02-C++对象模型与内存/01-对象布局、虚函数与内存分配](../C%2B%2B语言与对象模型/01-对象布局、虚函数与内存分配.md)：类内存布局与虚表指针。
  - [08-计算机体系结构与性能/03-ISA汇编与调用约定](03-ISA汇编与调用约定.md)：调用约定与栈帧分配。
- **构建系统与排障**：
  - [11-工程调试与性能分析/01-调试与性能分析方法论](../../08-工程实践与质量/调试与性能分析/01-调试与性能分析方法论.md)：符号还原与 ODR 违规分析。
  - [15-软件工程与构建/版本控制依赖与构建工程](../../08-工程实践与质量/工程设计与协作/版本控制依赖与构建工程.md)：构建图与动态库依赖。
- **游戏引擎落地**：
  - [游戏知识/08-工具链与打包发布/README](../../../00_Index/学习路线/工程实践与质量.md)：UE UBT/UAT 模块导出宏机制。
- **分类与领域入口**：
  - [10-编译链接与ABI README](../../../00_Index/学习路线/编程与计算机基础.md)
  - [计算机与工程基础 Domain MOC](../../../00_Index/学习路线/编程与计算机基础.md)
