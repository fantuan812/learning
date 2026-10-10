---
type: Concept
title: "10 FName / FString / FText 底层"
description: "区分名字身份、编号与显示拼写，以及可变字符串的长度、编码、分配和借用寿命；保留旧源码转录的可见语义。"
status: stable
verified: []
maturity: L2
updated: 2026-10-10
sources:
  - id: fname-guide-56
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/fname-in-unreal-engine?application_version=5.6
  - id: fname-api-56
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/FName?application_version=5.6
  - id: fname-constructors-56
    resource: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FName/__ctor?application_version=5.6
  - id: fname-entry-id-56
    resource: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FNameEntryId?application_version=5.6
  - id: fstring-guide-56
    resource: https://dev.epicgames.com/documentation/unreal-engine/fstring-in-unreal-engine?application_version=5.6
  - id: string-view-55
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/Containers/TStringView?application_version=5.5
  - id: string-builder-55
    resource: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/Misc/TStringBuilderBase?application_version=5.5
  - id: ftext-guide-56
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/ftext-in-unreal-engine?application_version=5.6
  - id: text-localization-56
    resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/text-localization-in-unreal-engine?application_version=5.6
---
# 10 FName / FString / FText 底层

> 知识成熟度：L2。本文是静态概念、公开 API 合同与现有转录的分层阅读；未运行 UE、编译、分配统计或性能实验。示例与表格中的 PAPER_EXPECTED 都是有前提的纸面预期，运行状态均为 NOT_RUN。
> 版本基准：2026-10-10 实际读到的 Epic 5.6 指南与 API；少数细项用明确标出的 5.5/5.3 API。版本参数固定的是文档入口，不是引擎提交或本机构建。本轮没有读取原稿所称的 UE 5.8.0、CL 55116800、`++UE5+Release-5.8` 安装；该历史身份不再承担现行实现认证。
> 最后更新：2026-10-10。

## 一、先把三种责任分开

`FName` 表达代码和数据中的名字身份；`FString` 拥有可修改的字符序列；`FText` 保留面向玩家的文本及本地化语义。选型首先看操作和寿命，然后才看一次比较或一次分配的成本。

| 需要保留什么 | 合适的起点 | 调用方仍要负责什么 |
| --- | --- | --- |
| 反复比较的有限名字，如骨骼名、参数名 | `FName` | 该名字是否存在于目标系统、是否允许忽略大小写、是否需要编号 |
| 日志、路径片段、外部输入、需要编辑的字符串值 | `FString` | 编码转换、长度上限、所有权与异步移交 |
| 一次同步调用期间借用一段字符 | `FStringView` | 底层所有者活着、范围有效、被调用方不留存 |
| 局部拼接后马上交给消费者 | `TStringBuilder` | 容量、溢出分配、输出借用何时失效 |
| 可翻译的 UI 文案、数字或日期显示 | `FText` | 文本身份、文化与显示更新；发布流程另有主责入口 |

这不是“所有 ID 一律 FName”：业务可能要求大小写敏感的账号键、跨机器永久身份，或严格保留输入字节。这些需求必须另建合适的表示。GameplayTag 还有注册和层级语义，不能用一个任意 `FName` 代替完整 `FGameplayTag` 合同。

本篇保留原来的六个问题：构造名字发生什么；比较和显示大小写有何不同；数字后缀与对象重名如何区分；TCHAR 与字符串转换如何影响成本；FText 身份与显示如何分层；三者怎样转换。顺着“输入 → 表示 → 比较 → 输出 → 持有”阅读，能看到每一步保存或丢失了什么。

## 二、FName：名字身份不是字符串指针，也不是稳定网络 ID

### 2.1 从文本到名字，再到显示文本

公开指南将 `FName` 描述为共享名字表中的字符串关联与实例编号，名字比较忽略大小写；名字文本本身不能像 `FString` 一样逐字符改写。给一个 `FName` 变量重新赋值，仍然可以让它表示另一个名字。“不可变”不等于 C++ 变量不能赋值。[FName 指南，5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/fname-in-unreal-engine?application_version=5.6)

可以用下面的概念流程理解成本；它不是 `UnrealNames.cpp` 函数体或线程时序转录：

1. 调用方提供有效文本，并选择允许添加还是只查找
2. 构造路径处理名字文本、编号及所选构造参数，查询名字系统
3. 查到已有表示可以复用；添加路径可能需要建立新表示
4. 持有 `FName` 值用于名字比较；需要人读时再转换为字符串或写入输出缓冲

所以“第二次一定更快”“每次构造一定分配”都不成立。指南的已存在骨骼名字例子明确可以避免新条目分配；查询文本、查表和业务查找本身仍有成本。缓存固定名字能减少重复构造，但收益需要在真实热路径测量。

持有一个 `FName` 不持有与它同名的 UObject、资产或骨骼，也不保证它们已加载、未销毁或查找成功。名字系统存在该串和目标 Mesh 存在这根骨骼是两个问题。[对象与反射](01-UObject与反射系统.md)负责对象身份和对象寿命；本篇只负责名字值。

### 2.2 比较身份与显示拼写

固定 5.6 API 明确：名字不区分大小写；`WITH_CASE_PRESERVING_NAME` 为 1 时可保留大小写。`ComparisonIndex` 服务比较，`DisplayIndex` 服务显示。这是语义分工，不能据此画出所有构建共同的 12 字节 ABI。[FName API，5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/FName?application_version=5.6)

作者示例，PAPER_EXPECTED / NOT_RUN：

```cpp
const FName Upper(TEXT("Socket"));
const FName Lower(TEXT("socket"));
const bool bSameName = (Upper == Lower);
```

在普通名字相等语义下预期 `bSameName` 为真；这不承诺两次 `ToString()` 必须逐字输出原输入。尤其不能把关闭大小写保留的构建当作一个无损、逐字的字符串容器。需要保留用户原输入或做大小写敏感键时，单独拥有原字符串并显式定义比较规则。

`IsEqual` 的比较方法和是否比较编号是额外参数；不能拿一次忽略编号的比较结果当完整名字相等。大小写保留也不是 Unicode 规范化、语言学排序或任意脚本的大小写折叠承诺。

`GetPlainNameString()` 的公开描述是“去掉编号部分”，并没有“把比较名字变成小写”这个合同。不要先无条件 `ToLower()` 再把该结果称为原始显示名。

### 2.3 Number：外部后缀、内部表示和对象命名责任

三个层次要分别记录：

| 层次 | 例子 | 不能混同的含义 |
| --- | --- | --- |
| 外部显示后缀 | `Bone_2` 的可见 `2` | 不是直接可 memcpy 的内部 Number 字段 |
| 名字的编号表示 | 字符串部分加编号部分 | 5.6 API 描述内部编号比外部值多 1，零用于无编号；不要直接照抄外部数字传入所有构造重载 |
| UObject 唯一命名 | 指定 Outer、Class 和 BaseName 的命名操作 | 普通 `FName(TEXT("Bone"))` 重复构造不会替你创建唯一对象名 |

5.6 构造 API 的 `bSplitName` 参数控制在特定 Number 条件下是否拆末尾编号，某些加载构造明确不拆分。将 `Bone_2` 解释为基本名 `Bone` 和外部编号 2，必须先说明使用正常拆分路径；在该前提下，按公开的内部加一描述应理解为内部编号 3。本文没有读取完整解析函数，因此不规定前导零、最大位数、溢出、异常后缀的接受规则。外部协议不要依赖这些未核对的解析角落。[构造 API，5.6](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FName/__ctor?application_version=5.6)

例如两次构造同一 `Bone_2` 都是在表示同一名字；不能期待第二次自动变成 `Bone_3`。需要对象系统生成可用名字时，`MakeUniqueObjectName` 是带 Outer/Class/Options 的独立操作。它的保证属于该命名上下文，不是跨进程、跨存档或跨服务器的全局永久身份。[MakeUniqueObjectName，5.5](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/CoreUObject/UObject/MakeUniqueObjectName?application_version=5.5)

`NAME_None` 是名字系统的特殊值；默认构造和 `IsNone()` 应通过公开接口理解，不把“某个内部字段是零”当作所有布局的业务协议。`FNAME_Find` 的失败可以返回 None；5.6 API 还区分 outline-number 配置下检查字符串部分或完整编号名，因此“名字池查找”也不能代替业务注册表验证。

### 2.4 比较、哈希和排序是三种操作

| 操作 | 适合回答的问题 | 边界 |
| --- | --- | --- |
| `==` / 按所需参数调用 `IsEqual` | 两个名字是否满足该相等关系 | 默认名字比较不是逐字拼写比较；编号不能无意丢掉 |
| `GetTypeHash` 配合容器键相等 | 去哪个桶继续查 | 相同键必须得到相同哈希；相同哈希不证明键相等 |
| `Compare` / `LexicalLess` | 需要词汇顺序还是确定性顺序 | 5.6 API 把 Compare 描述为字母升序，LexicalLess 为跨进程确定性词汇序；不要承诺所有语言的自然排序 |
| `CompareIndexes` / `FastLess` | 当前进程中需要快速非字母排序 | 只在进程寿命内稳定，不能成为跨运行排序协议 |

公开 `FNameEntryId` 是 opaque ID；其 `ToUnstableInt()` 明确属于当前进程。不要写入“名字表下标 = 数据库 ID = 网络 ID”，也不要通过观察某次输出数字相同就证明可移植。[FNameEntryId，5.6](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/FNameEntryId?application_version=5.6)

原稿的 `GetTypeHash(GetComparisonIndexInternal())` 只有一段表达式，没有本轮可核对的函数体或版本绑定，不能作为现行哈希算法说明。业务层只依赖哈希与相等的一致性，不自己复制字段级哈希，更不忽略 Number 后断言“哈希唯一”。

### 2.5 存档、网络与名字池占用

序列化一个类型与裸写该类型的内存是不同合同。存档或复制管线可以为名字建立可恢复的表示，但它们必须定义自己的名字映射、编码、版本与编号处理。一个进程的池索引或 `ToUnstableInt()` 没有承担这些事情。

作者设计建议：自定义协议若需要稳定业务键，先定义允许字符、大小写策略、长度和版本，再选择可移植字符串或显式分配的业务 ID；接收端按同一合同解析。不能把本地 `FName` 地址、句柄、哈希值或原始对象字节直接发给另一进程并指望重建同一名字。

同样，名字池的共享存储不等于“任意字符串入池都便宜”。输入集合持续产生新名称时，会持续给名字系统施加存储压力。本文不从未读实现断言池的逐项回收方式、最大块数或永不释放；保守设计是将时间戳、完整用户消息、无界随机值保留为动态数据，不把它们批量转成名字。`FName` 析构也不能被当作逐条回收池内存的接口。

## 三、FString：拥有字符，长度不等于字节数或屏幕字数

### 3.1 所有权、长度和缓冲区

`FString` 是动态可变字符串，拥有自己的字符数组；`*Str` 提供用于读取的字符串指针，不转移所有权。拷贝字符串值与拷贝借用指针的结果不同：前者得到自己的值，后者仍依赖同一所有者。移动可以转移资源，但不要预设移动后原变量的内容，或继续拿旧借用跨越移动操作。[FString 指南，5.6](https://dev.epicgames.com/documentation/unreal-engine/fstring-in-unreal-engine?application_version=5.6)

`Len()` 不含结束字符。阅读与外部接口交互时，至少分别标出下列量：

| 量 | 用途 | 常见错误 |
| --- | --- | --- |
| `Len()` 对应的字符存储单元数 | 在同一字符串表示内确定范围 | 把它称为玩家看到的字数 |
| 结束符 | 给要求 NUL 结束的 API 确定结尾 | 向网络正文长度里多算一个终止零，或漏掉输出缓冲所需结尾空间 |
| 容量 / 分配字节数 | 判断可容纳多少数据、实际内存成本 | 把容量当成现有字符串长度 |
| 转码后的字节数 | UTF-8 等外部数据接口 | 使用 `Str.Len()` 直接当 UTF-8 字节长度 |
| Unicode 码点 / 字素簇 | 文本编辑、截断和 UI 字数 | 用一个下标单元代表完整可见字符 |

已读 5.3 `Len` 文档支持“不含结束字符”；它不承诺字素计数。[Len，5.3](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/Containers/FString/Len?application_version=5.3) 旧编码文档虽挂在 5.6 入口，但仍含 UE4、2009/2010 和旧平台叙述；这里只借其说明编码单位与转码寿命，不能把其中旧文件加载、平台宽度或 UCS-2 概述重新签认为所有现代 UE 模块的实现。[Character Encoding，5.6 入口](https://dev.epicgames.com/documentation/en-us/unreal-engine/character-encoding-in-unreal-engine?application_version=5.6)

例如在 UTF-16 表示下，BMP 外码点需要代理对；组合附加符还可能让多个码点显示成一个字素。对这种输入按任意单元 `Left/Right/Mid` 截断，可能破坏文本边界。纯 ASCII 的长度例子不能外推为“每个用户字符总是 2 字节”。`TCHAR`、`wchar_t`、字节序与外部 UTF-8 也不能只靠强制转换指针来互换。

**走通一个非 ASCII 输入（PAPER_EXPECTED；目标管线 NOT_RUN）。** 固定内容按顺序为 `U+0041 U+1F600 U+0065 U+0301`，视觉示意是 `A😀é`；最后两项是 `e` 加组合重音，计数前不做规范化，也不替换为预组字符。以下明确假定一个字节为 8 位、`TCHAR` 为 16 位且每个元素保存一个 UTF-16 代码单元，并假定 `FString` 正好保存这段序列。这是本例的表示前提，不能据此认证未读目标平台或转换器支持。

| 内容码点 | UTF-16 代码单元（十六进制） | 单元数 | UTF-8 数据字节（十六进制） | 字节数 |
| --- | --- | --- | --- | --- |
| `U+0041`（A） | `0041` | 1 | `41` | 1 |
| `U+1F600`（😀） | `D83D DE00` | 2 | `F0 9F 98 80` | 4 |
| `U+0065`（e） | `0065` | 1 | `65` | 1 |
| `U+0301`（组合重音） | `0301` | 1 | `CC 81` | 2 |
| 合计，均不含终止 NUL | `0041 D83D DE00 0065 0301` | **5** | `41 F0 9F 98 80 65 CC 81` | **8** |

这张表按 Unicode 16.0 的编码规则手工推导：例如 `0x1F600 - 0x10000 = 0xF600 = 0x3D × 0x400 + 0x200`，代理对因此是 `0xD800 + 0x3D = 0xD83D`、`0xDC00 + 0x200 = 0xDE00`；UTF-8 按对应码点的位分配得到表中各字节。[Unicode 16.0，第 3 章 §3.9.2–3.9.3、表 3-5/3-6](https://www.unicode.org/versions/Unicode16.0.0/core-spec/chapter-3/)

内容有 **4 个码点**。按 Unicode 16.0、UAX #29 修订 45 的**默认扩展字素簇**规则（无定制），分段是 `[A] [😀] [e＋U+0301]`，共 **3 簇**：`U+0301` 的属性是 `Extend`，GB9 使它与前面的 `e` 不分开，其余两个内部边界按 GB999 分开。这里的簇数不是任意字体的 glyph 数或屏幕宽度，也没有声明 UE 当前 UI 已使用此版分段器。[UAX #29，修订 45，§3.1.1](https://www.unicode.org/reports/tr29/tr29-45.html)、[Unicode 16.0 GraphemeBreakProperty](https://www.unicode.org/Public/16.0.0/ucd/auxiliary/GraphemeBreakProperty.txt)

在上述 UTF-16 前提下，`Len()` 对应 **5 个存储单元**，内容占 **10 字节**；若要求终止 NUL，还需一个 `0000` 单元，即合计 6 单元、12 字节的内容加结尾空间，这不是分配容量结论。正确 UTF-8 内容是 **8 字节**；需要 NUL 结束的输出缓冲还要另留一个 `00`，共 9 字节。

具体误用：转为上表 UTF-8 后，若仍把 `Len() = 5` 当成发送长度，只发送前 5 字节 `41 F0 9F 98 80`，接收内容成为 `A😀`，末尾 `65 CC 81`（`e` 和组合重音）整个丢失。**这个 5 字节前缀本身是有效 UTF-8**，所以错误可能表现为安静丢字，不能笼统说一定产生非法 UTF-8。长度参数应来自转码结果的实际数据字节数；该例为 8。以上是固定输入的纸面预期，未执行编码程序、分段程序或 UE 测试。

原稿把 `FString` 写成固定 `TString<TCHAR>` ABI，并从宏名直接推定对象布局。本轮没有相应目标头文件；这里使用拥有动态字符序列的合同。`GetCharArray()` 是低层入口，直接改底层数组时调用方必须维护有效字符串约束，不把它当作任意二进制包容器。

### 3.2 带长度构造：参数顺序会改变含义

固定 5.3 构造列表分别存在 `(int32 Len, const WIDECHAR* Str)` 和 `(const WIDECHAR* Str, int32 ExtraSlack)`。因此旧建议 `FString(CharPtr, Len)` 可能把长度当额外容量，继续按 NUL 找结尾；这不是可忽略的风格问题。[构造列表，5.3](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/Containers/FString/__ctor?application_version=5.3)

作者示例，PAPER_EXPECTED / NOT_RUN；前提为目标重载与已读合同一致、指针指向有效字符数组、范围非负且不越界：

```cpp
const TCHAR Raw[] = TEXT("ABCDE");
const int32 SliceLength = 3;
const FString OwnedPrefix(SliceLength, Raw);
const FStringView BorrowedPrefix(Raw, SliceLength);
```

预期两个范围都表示 `ABC`，但 `OwnedPrefix` 拥有结果，`BorrowedPrefix` 只借用。借用的范围没有在 `Raw[3]` 自动写零；不能把 `BorrowedPrefix.GetData()` 直接交给只认识 NUL 结束的接口并期待它只读三个单元。这里使用有结束符的 ASCII 原数组，未证明含嵌入 NUL、非法编码、负长度或超长输入的行为。

### 3.3 大小写规则必须写在调用点

原稿声称 `FString == FString` 以及默认 `TMap<FString, ...>` 天然大小写敏感，不能沿用。已读固定 5.3 `operator==` 页面明写 case insensitive；5.6 指南明确提供 `Equals` 的显式比较选项。本文不把旧版本说明冒充未读构建的函数体，示例直接写出期望的关系。[operator==，5.3](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/Containers/FString/op_cmp_eq?application_version=5.3)

作者示例，PAPER_EXPECTED / NOT_RUN：

```cpp
const FString Saved(TEXT("Config"));
const FString Entered(TEXT("config"));
const bool bExact = Saved.Equals(Entered, ESearchCase::CaseSensitive);
const bool bFolded = Saved.Equals(Entered, ESearchCase::IgnoreCase);
```

对这个 ASCII 输入，纸面预期分别为 false 和 true。`Contains/Find` 同样显式选择大小写与搜索方向，不因为 `Equals` 使用了一个选项就推定所有搜索接口采用同一默认。

若容器业务确实要求大小写敏感，必须核对容器的 KeyFuncs、相等和哈希是否实现同一关系；只在查询前加一次敏感 `Equals`，不会改变已存键的容器合同。也不要先用 FName 去重，再期待转回 FString 能恢复被合并的大小写区别。

### 3.4 借用指针、视图和异步调用

`FStringView` 是不拥有字符的范围，通常按值传递；视图不保证 NUL 结束。底层字符串或 builder 被销毁、修改或重分配后，原借用可能失效；复制 view 并没有复制字符。[TStringView，5.5](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/Containers/TStringView?application_version=5.5)

作者示意，接口名为教学约定，未宣称仓内存在这些函数：

```cpp
void ConsumeNow(FStringView Text); // 约定：返回前消费完，不保存 view
void EnqueueOwned(FString Text);  // 约定：队列保存拥有型值到消费完成

void SubmitLabel(const FName LabelName)
{
    const FString Label = LabelName.ToString();
    ConsumeNow(Label);
    EnqueueOwned(Label);
}
```

这里 `Label` 的块作用域覆盖同步消费；传给约定接收拥有型值的队列，可以复制成队列自己的字符串。仅把实参类型改成 view，或在队列中保存 `*Label`，就破坏了这条寿命链。接口的真实实现必须履行保存值的约定，示例中的函数声明本身不证明它。

反例只用于阅读，NOT_RUN：`const TCHAR* P = *LabelName.ToString();` 在分号后临时 FString 已结束寿命；同理，从临时 FString 保存 view 不能延长底层字符寿命。若指针只在同一个完整表达式内同步使用，临时寿命可能覆盖调用，但被调用方仍不得把指针留到返回之后。[operator*，5.3](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/Containers/FString/op_deref?application_version=5.3)

转码宏还有第二个所有者：转换对象。`TCHAR_TO_UTF8` 的结果不是原 FString 的长期 UTF-8 别名；宏创建的临时转换对象结束后，输出指针会失效。需要跨调用或异步保存时，显式拥有转换后的结果，并使用转换结果自己的长度；输入 FString 一直活着也不能挽救已销毁转换对象的指针。

### 3.5 分配与拼接：减少中间对象，不许诺固定次数

作者示例，PAPER_EXPECTED / NOT_RUN，要求项目可用 `Misc/StringBuilder.h`：

```cpp
FString BuildDebugLabel(const FString& Group, const FName Name)
{
    TStringBuilder<128> Builder;
    Builder.Append(Group);
    Builder.Append(TEXT(":"));
    Name.AppendString(Builder);
    return FString(Builder.ToString());
}
```

这里 builder 承担短期拼接，返回的 FString 承担长期持有；`ToString()` 给出的指针只在 builder 有效且未被修改期间可借用，不是返回一个 FString。构造返回值时仍需得到拥有型结果。固定容量覆盖常见输入时可减少中间分配；超过初始缓冲会动态分配，因此不能称“无条件零分配”，也不能承诺整个函数恰好一次分配。[TStringBuilderBase，5.5](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/Misc/TStringBuilderBase?application_version=5.5)

`FString::Printf(TEXT("%s"), *Str)` 的 `%s` 要求匹配的字符指针；`TEXT` 处理字面量类型，不负责把任意运行时 UTF-8 字节解码成 TCHAR。`FString::Format` 是普通字符串格式化，不能替代 FText 的本地化历史。频繁日志也要先控制产生频率与是否真的需要构造，再考虑 builder；本篇不报告节省了多少微秒或多少次分配。

原稿的 `TSharedString` 只给出一个名字和版本断言，没有本轮已读公开合同；不把它列作保证可用的通用替代。若需要共享只读文本，先设计拥有者、不可变内容与借用范围，再核对具体目标类型。

原来的文件路径拼接用途也可以沿用这个“局部构造 → 拥有型结果”模式。在约定 BaseDir 不以分隔符结束、FileName 是一个已验证文件名片段时，依次 Append BaseDir、`TEXT("/")` 和 FileName，最后复制为 FString；纸面输入 `Saved` 与 `trace.txt` 得到 `Saved/trace.txt`。这只教字符串拼接，没有提供路径规范化、防目录穿越、文件存在或资产加载保证。真实路径工具还应处理空段、已有分隔符和平台规则；不能用一个“零分配”标题把这些合同省略。

## 四、名字缓存与业务缓存分别失效

原骨骼例子想减少每帧名字构造和骨骼查询，这个用途保留；修复重点是缓存的身份和重建时机。

1. 固定名字可以存在宿主初始化时建立的成员值或合适的静态值中；静态方式仍须遵循模块生命周期
2. `FLazyName` 的公开作用是减少静态初始化期间构造 FName 的工作，提供 `Resolve`/转换接口；它不负责延长 UObject 生命周期，也不自动缓存 `GetBoneIndex` 的结果
3. 骨骼索引属于具体资源及其骨骼映射；更换 Mesh、重建映射或组件退场时，应清空旧索引，重新查询并处理 `INDEX_NONE`
4. 查询失败不能只保留上一次有效索引，否则新模型可能在相同整数下代表别的骨骼

[FLazyName，5.5](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/UObject/FLazyName?application_version=5.5) 只支持惰性名字构造合同；资源缓存策略是作者设计建议，不是假称该类实现了资源观察。

作者流程示意，PAPER_EXPECTED / NOT_RUN：

| 事件 | 名字值 | 业务索引与拥有者 |
| --- | --- | --- |
| 宿主初始化 | 建立目标骨骼 FName | 先设无效；在当前 Mesh 查找成功才写入 |
| 每帧使用 | 重用同一名字 | 确认当前 Mesh 仍是索引对应资源，再用已验证索引 |
| Mesh 从 A 变为 B | 名字可以不变 | 先撤销 A 的索引，再查询 B；失败保持无效 |
| 组件销毁 / 模块退出 | 名字不是保活引用 | 停止异步消费者，丢弃组件与资源缓存 |

## 五、FText：转换边界与主责入口

本节保留原稿的选型、Namespace/Key/SourceString、格式化和文化切换问题；收集、翻译、Import、Compile、Cook 的完整发布责任交给[本地化发布工作流](../../08-工程实践与质量/持续交付与发布治理/13-本地化发布工作流.md)。该链接是主责导航，不代表本轮重新验证其所有历史源码断言。

### 5.1 身份、来源与当前显示值

可本地化文本的 Namespace 和 Key 形成身份，SourceString 是翻译来源并参与陈旧译文判断；不是每个 FText 都以一个可翻译字面量创建。文本历史可以支持文化变化后重建，也支持文本的专用传输表示。因此原稿“FText 不可存档/比较/网络”是过度禁止：应选择相应文本合同，不能丢掉历史后把当前显示字符串冒充原文本。[Text Localization，5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/text-localization-in-unreal-engine?application_version=5.6)

修改 Namespace/Key 会改变身份，可能影响已有翻译关联；修改来源串需要重新检查译文。本文不承诺“必然永久丢失全部翻译”，也不把每次 `ToString()` 画成必定从磁盘重查一次 LocRes。已经缓存的 FString 只是某次显示结果，文化变更后不会凭空长出丢失的文本历史。

### 5.2 三个转换不能画成可逆箭头

| 转换 | 保存的东西 | 丢失或没有建立的东西 |
| --- | --- | --- |
| `FName` → `FString` | 当前可读名字表示 | 没有获得跨进程内部索引合同；不承诺原输入逐字复原 |
| `FString` → `FName`，如 `FName(*Value)` | 名字比较所需身份 | 原输入大小写区分等字符串语义不能继续当作无损字段 |
| `FName/FString` → `FText` | 可显示的非本地化内容 | 不会自动生成完整可翻译文案身份 |
| `FText::ToString()` | 当前字符串内容 | 本地化历史和身份不能只靠该 FString 还原 |

`FromString` 在非 Editor 构建与文化不变构造相同，但 Editor 中不会直接标成文化不变，保存到 FText 资产属性后可能获得可本地化身份；所以它不等于在所有宿主中调用 `AsCultureInvariant/INVTEXT`。`FromName` 也不是自动本地化入口。需要明确不可翻译的玩家输入，使用对应的文化不变合同；需要本地化文案，使用有文本身份的资源或字面量。[FText 指南，5.6](https://dev.epicgames.com/documentation/en-us/unreal-engine/ftext-in-unreal-engine?application_version=5.6)

比较 FText 可以使用 `EqualTo/CompareTo` 等定义比较级别的接口；业务稳定 ID 则单独存放。不要把不确定是否存在的 `Text.GetSourceString()` 当作通用存档 API，更不要用来源串当文本永久 ID。也不能从 `IsTransient` 一个名称推断所有运行时生成文本都禁止持久化。

### 5.3 保留本地化格式化示例的用途

作者示例，PAPER_EXPECTED / NOT_RUN：

```cpp
#define LOCTEXT_NAMESPACE "InventoryUI"
FText MakeItemCountText(int32 ItemCount)
{
    return FText::Format(
        LOCTEXT("ItemCount", "You have {0} items"),
        FText::AsNumber(ItemCount));
}
#undef LOCTEXT_NAMESPACE
```

模板维持完整句子，参数按文本/数字格式化合同提供；翻译可以调整占位顺序。模板中简单的英文 `items` 并不自动解决所有语言的复数与语法，真实文案还需要本地化流程处理。`FText::Format` 接受支持的参数值，不是“所有实参必须已是 FText”。`AsNumber/AsDate/AsCurrency` 类操作负责相应文化格式，不能据此推出生成文本都可被翻译工具逆向成可编辑源文案。

`NSLOCTEXT` 显式给出命名空间；`LOCTEXT` 使用该文件中的 `LOCTEXT_NAMESPACE` 并应及时 `#undef`。不应强迫两者都依赖同一宏定义。避免 `LOCTEXT(...).ToString()` 再 `FromString(...)` 的往返：外观偶尔相同，文本历史已经不是原来的合同。

## 六、现有“源码节选”的逐项阅读与身份

以下三个代码块逐字保留固定基线中的现有转录，连同原注释一起保留。它们没有引擎 commit、真实头文件原字或完整条件编译上下文；“节选”是原稿自称。本轮没有下载新的受限引擎源码。这里只解释可见的声明和表达式，并指出它们不能证明的实现。

### 6.1 H1：FName 字段草图

```cpp
// 节选：NameTypes.h —— FName 本体
class FName
{
	FNameEntryId ComparisonIndex; // 比较索引（大小写不敏感条目）
	FNameEntryId DisplayIndex;    // 显示索引（大小写保留条目，仅 WITH_CASE_PRESERVING_NAME）
	int32         Number;         // 实例编号
};
```

可见声明把比较、显示和编号分成三个成员，能教会读者区分三种责任；它没有构造、查表、哈希或比较的函数体。注释说 DisplayIndex 条件存在，但代码块根本没有显示 `#if`；类也没有可核对的完整基类、对齐和配置。因此不能从三个字段计算目标构建固定大小，更不能把“可 memcpy”变成跨构建序列化许可。已读 5.6 API 的 Number 文档类型是 `uint32`，与这里的 `int32` 不同，进一步说明这份转录不能直接代替头文件。

原文另外写了 `Blocks[Block] + Stride * Offset`。作为普通可见表达式，它表示先选一个块，再按步长偏移；它依然在索引 `Blocks`，不能称“无查表”。它没显示边界检查、块发布、锁或原子操作，因而不能证明无锁或线程安全。原稿给的 13/16 位拆分、256/1024 分片、CityHash64、Header 位域和 NAME_SIZE 上限都缺少本轮绑定实现；阅读者可以把它们列为日后核对某一引擎提交的项目，不能把这些数字写入业务协议。

继续按原稿真正写出的表达式读：`块号 << 16 | 块内偏移` 是把高位块号和低位偏移拼接，必须先满足位宽与不重叠前提；它不是一个可直接解引用的机器地址。`1 << 16` 等于 65536，仅是该草图的偏移空间大小，不能据此证明每个名字条目占一个固定槽。`union { ANSICHAR AnsiName[NAME_SIZE]; WIDECHAR WideName[NAME_SIZE]; }` 表达两个表示共享存储位置，不是同时分配两份名字；仅有 union 声明仍不能推出每个实际条目分配整个 NAME_SIZE 数组，或判定何种非 ASCII 输入走哪条分支。这样保留地址计算、位打包和宽窄存储的学习用途，同时不把草图变成已读分配器。

`FNamePool`、`FNameEntryAllocator`、`FNameEntry`、`FNameSlot`、`ENameToEntry` 这些历史名称分别想表达池、分配、条目、查找槽和硬编码名关联；公开 API 已支持 opaque 名字 ID 和硬编码 EName 构造，但没有在本篇提供该整条私有实现链。保留这种职责分解有助于读源码；按名字补写未见函数体、重构年份或锁升级路径则会制造证据。

### 6.2 H2：FLazyName 声明草图

```cpp
// 节选：NameTypes.h
/** Lazily constructed FName that helps avoid allocating FNames during static initialization */
class FLazyName
{
	// constexpr 构造只保存字符串字面量指针 + 解析出的 Number
	template <int N> constexpr FLazyName(const ANSICHAR (&Literal)[N]);
	// 首次隐式转换到 FName 时才真正进池
	operator FName() const;
};
```

这里的 `template <int N>` 加数组引用表达“接收定长字符数组”：字面量可绑定，但声明本身未限制实参必须是字面量。`constexpr` 声明表达该构造可参与相应常量求值；`operator FName() const` 声明转换接口。代码没有构造或转换函数体，注释中的“保存指针与解析 Number”“首次才入池”不能当作可见实现逐行证明。省略 `public:` 的 class 草图也不是可以直接粘进工程的完整类定义。

公开 FLazyName 合同足以支持“延后名字构造以减少静态初始化工作”；不能从中推出原稿声称的某个 FRigVM 调用例在未读版本中一定存在，也不能推出所有全局静态 FName 都必然因初始化顺序崩溃。

### 6.3 H3：FText 接口草图

```cpp
// 节选：Text.h —— FText 关键接口
class FText
{
	const FString& GetSourceString() const; // 源语言字符串（开发语言）
	// Namespace / Key 通过 FTextHistory 或文本属性获得
};
```

可见 `const FString&` 是引用返回声明，`const` 修饰成员函数，不是所有权或永久寿命的承诺；无函数体就看不到返回的是谁的数据。这个转录同样缺 `public:`，本轮也没有公开证据证明 `GetSourceString()` 是可这样调用的 FText 公共方法。它只能提醒“源串与当前显示串不同”，不能继续充当存档或相等判断示例。上节以公开文本历史、文本比较和专用传输说明补足原来的实际用途。

## 七、有限纸面案例：把错用变成可检查的预期

每行状态均为 `PAPER_EXPECTED / NOT_RUN`。表中给的是固定资料支持的语义与作者设计前提，不是执行日志、引擎断言或性能实测。失败条件应由未来真实工程测试记录输入、目标版本、配置、输出与证据，不能在本表先填 PASS。

| 案例与输入 | 前提 | 纸面预期 | 反例 / 未覆盖 |
| --- | --- | --- | --- |
| P1：`Socket` / `socket` 名字比较 | 普通 FName 相等，编号一致 | 名字相等 | 不承诺关闭大小写保留时逐字输出原串；不扩展到任意 Unicode 折叠 |
| P2：重复构造 `Bone_2` | 正常拆分后缀路径 | 两次表示同一名字；外部 2 与内部表示分开理解 | 不自动变 `Bone_3`；前导零、溢出和特殊构造 NOT_RUN |
| P3：只查名字池 | 使用 `FNAME_Find` | 未命中可得 None | 命中不代表目标 UObject/骨骼存在；编号检查受已述配置影响 |
| P4：两个进程不同插入顺序 | 自定义持久协议 | 以显式键合同重建名字 | 内部索引、FastLess 顺序或裸字节不作为线协议证据 |
| P5：`ABCDE` 取前三单元 | 有效 ASCII 数组、长度 3 | 拥有型结果与 view 均表示 ABC | view 后面仍接 D；无 NUL 消费者必须拿长度 |
| P6：Config/config 比较 | 显式 CaseSensitive / IgnoreCase | 前者不等，后者相等 | 不靠字符串类型名称推定容器 KeyFuncs |
| P7：临时 ToString 的指针保存到下一句 | 临时在分号处结束 | 指针不可继续使用 | 同步完整表达式内消费不等于允许异步留存 |
| P8：builder 在初始容量内 / 超出容量 | 字符范围有效 | 输出文本应相同；超出时允许动态扩展 | 未测分配次数；GetData 不自动等同 NUL 结束结果 |
| P9：同一骨骼名从 Mesh A 切到 B | 宿主按资源变化撤销缓存 | 先使旧索引失效，再查 B | 名字值不变不能证明 A 的索引仍有效 |
| P10：FText 先 ToString 再 FromString | 文本具有本地化历史 | 往返不能保证恢复原身份和历史 | 当前外观相同不证明切文化后仍正确；完整发布 NOT_RUN |
| P11：TCHAR 转 UTF-8 后排队 | 队列消费晚于当前完整表达式 | 队列必须拥有转换后字节及其长度 | 保存宏的临时指针，即使输入 FString 还活着也不够 |
| P12：`U+0041 U+1F600 U+0065 U+0301`，详见 §3.1 | 16 位 `TCHAR` 存 UTF-16；无规范化；Unicode 16.0 默认扩展字素簇 | 内容为 5 单元、4 码点、3 簇、8 个 UTF-8 字节；NUL 另计 | 把 `Len() = 5` 当 UTF-8 字节数只发送有效前缀 `A😀`，丢掉 `e＋U+0301`；PAPER_EXPECTED / NOT_RUN |

## 八、选型实践与 FAQ

1. 有限、重复的内部名字可以用 FName；先确认忽略大小写符合业务，资产、对象和 GameplayTag 各自还有更高层合同
2. 缓存固定名字可以少做重复构造；缓存资源索引还要保存资源归属和失效策略，二者分别处理
3. 把转换放在需要文本的边界；比较名字不必先 ToString，输出 builder 也不能不计溢出和结果拥有成本
4. 玩家文案保留 FText；玩家输入需要明确文化不变的显示策略，不能指望 FString 往返自动本地化
5. FText 比较和持久化使用适合文本的合同；业务主键独立保存，来源串与当前显示串都不是通用永久 ID
6. FString/view/builder/转码对象分别标所有者与消费截止点；异步边界移交拥有型数据
7. 比较、查找、容器键和外部协议显式定义大小写与规范化策略；哈希一致性与相等要成对审查
8. 编号、对象唯一命名和网络身份分别设计；不手工套用历史私有字段或借未核对工具名生成永久 ID

**Q1：FName 究竟多大，能不能 memcpy？** 取决于实际头文件、编译配置和 ABI，本篇没有目标 `sizeof` 证据。即便某构建允许特定内存复制，也不产生跨运行、跨机器或跨版本的序列化合同。

**Q2：名字池会增长吗？** 新的唯一输入会增加名字系统的存储工作；不要把临时数据无限入池。具体回收、上限和增长曲线需要固定引擎实现与测量，本篇没有给出运行观测。

**Q3：Foo 与 foo 是同一个名字吗？** 对普通忽略大小写的 FName 相等语义，是；显示拼写是另一维度，受配置影响。需要精确保留输入请另持字符串。

**Q4：字面量 Bone_2 与自动对象名有什么不同？** 前者经所选构造路径解释为名字；后者先由对象命名过程挑选可用名，再交给名字系统表示。相同最终名字不表示生成过程相同，更不意味着跨会话身份相同。

**Q5：文化切换后显示会怎样？** FText 的历史支持重建，实际 UI 与缓存须正确响应；已经取出的 FString 是当时结果，不能被当作持续关联的文本对象。本篇不认证某引擎构建的切换调用链。

**Q6：为什么仅保存 FString 不够本地化？** 它能保存字符值，但没有因此保留文本身份、来源和历史；业务必须选择 FText 或明确的文本资源定位协议。

**Q7：FText::Format 与 FString::Printf 有何区别？** 前者用于保留文本格式化语义，模板可本地化、参数可按文化表达；后者产生普通字符结果且需正确匹配格式符与参数类型。不要把所有 Format 参数都限制成已构造的 FText。

**Q8：FString 做键一定慢吗？** 字符串哈希/比较通常与内容有关，但总成本还取决于长度、转换频率、分配、容器和命中分布。为一次查找新建 FName 不保证净收益；选择正确相等关系后再测真实负载。

## 九、关联阅读与来源边界

- [UObject 与反射系统](01-UObject与反射系统.md)：名字值与对象身份、对象寿命的区别
- [场景组件与变换体系](../../04-图形动画与物理仿真/空间层级与变换/05-场景组件与变换体系.md)：骨骼/插槽名称所在的使用上下文
- [容器与内存管理源码](07-容器与内存管理源码.md)：容器、内存和借用失效；其历史转录不能替本篇提供未读 FString 实现
- [UPROPERTY 与反射系统源码](01-UPROPERTY与反射系统源码.md)：名字在反射中的职责，不把字段名字当成对象保活
- [本地化发布工作流](../../08-工程实践与质量/持续交付与发布治理/13-本地化发布工作流.md)：本篇完成类型与转换边界后，继续阅读发布责任

本轮读到的是公开指南、API 声明/说明，以及仓内已经存在的三份源码草图。部分固定 5.6/5.5 FString 细项页返回空壳或不可访问，细项改用明确标出的 5.3 页面；没有把失败页面记作已读实现。未读取完整 `NameTypes.h`、`UnrealNames.cpp`、`UnrealString.h` 或 `Text.h` 源文件，没有再次认证旧稿的版本、重构年份、锁、哈希、内存布局或本地化查表时机。现有转录原字保留，用公开合同与逐表达式解释修复使用方式；不补造引擎私有函数体。
