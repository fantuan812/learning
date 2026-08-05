# 10 FName / FString / FText 底层

> 适用版本：UE 5.8（以本机 `C:\Program Files\Epic Games\UE_5.8\Engine\Source\Runtime\Core` 源码为基准，逐行核对）。FName 池机制在 4.x→5.x 有两次大重构（4.26 引入分片池、5.0 引入 FNameEntryId/块分配器），本文以 5.8 现状为准。

## 一、概述

UE 提供三种"字符串"类型：`FName`、`FString`、`FText`。新手最常见的困惑是"什么时候用哪个、为什么不能互相乱转"。答案是三者服务于完全不同的目的：

- **FName**：全局去重、哈希加速的"名字表下标"。它不关心拼写内容，只关心"是不是同一个名字"。对象/资产/骨骼/GameplayTag 的标识都用它，因为它**比较快、存储省、可序列化稳定**；
- **FString**：可变的、堆分配的**运行时文本**。文件路径、日志、拼接出来的动态字符串用它；它也是三兄弟里唯一"内容可以任意变"的；
- **FText**：**面向玩家的本地化文本**。带命名空间（Namespace）、Key 和"源语言字符串"，运行时按当前文化（Culture）查翻译表显示。UI 上所有玩家看得到的文字都必须是 FText。

本文从 `Runtime\Core\Public\UObject\NameTypes.h`、`Runtime\Core\Private\UObject\UnrealNames.cpp`、`Runtime\Core\Public\Internationalization\Text.h` 出发，逐层拆开三个类型的内存模型与转换陷阱。

### 1.1 本篇回答的问题

- `FName("Hello")` 到底发生了什么？为什么第二次构造"更快"？
- FName 为什么大小写不敏感？"Foo" 和 "foo" 是同一个名字吗？
- FName 的 `_123` 数字后缀是什么？`FName("Bone_2")` 和运行时生成的重复名有什么不同？
- FString 的 TCHAR 是什么？为什么 `ToString()` 频繁调用会卡？
- FText 的 Namespace / Key / SourceString 三者各管什么？翻译是怎么被查到的？
- 什么时候用 FName、FString、FText？互相转换的正确姿势是什么？

## 二、核心概念

| 概念 | 类型/位置 | 说明 |
| --- | --- | --- |
| `FName` | 值类型，8 字节 ID + 4 字节 Number（12 字节） | 全局名字表中某个条目的"下标"，复制即引用 |
| `FNamePool` | `UnrealNames.cpp` 静态单例 | 全局名字池：条目分配器 + 哈希分片表 |
| `FNameEntry` | `NameTypes.h` | 池中一个名字条目：Header（长度/宽窄/探针哈希）+ 字符数据 |
| `FNameEntryId` | `NameTypes.h` | 条目句柄：`块号(13bit) << 16 | 块内偏移(16bit)` 编码进 32 位 |
| `FNameEntryHeader` | `NameTypes.h` | `bIsWide:1` + `Len:15`（保留大小写时）；记录名字长度与字符宽度 |
| ComparisonIndex | FName 成员 | **大小写不敏感**条目标识（比较用） |
| DisplayIndex | FName 成员 | **大小写保留**条目标识（显示用；仅 `WITH_CASE_PRESERVING_NAME` 下独立） |
| `Number` | FName 成员（int32） | 实例编号：`FName("X_3")` 外部编号 3；运行时重复名自动递增 |
| `FLazyName` | `NameTypes.h` | 静态初始化期安全的名字：首次使用时才解析成 FName |
| `EName` | `UnrealNames.inl` | 引擎预注册的硬编码名（`NAME_None`、`NAME_All`…），`ENameToEntry` 直接索引 |
| `FString` | `TString<TCHAR>`（堆分配） | 可变的宽字符动态字符串，行为类似 TArray |
| `TCHAR` | `CoreTypes.h` | 平台宽字符：Windows 上为 `wchar_t`（UTF-16） |
| `FStringView` | `StringView.h` | 不拥有数据的字符串视图（只读引用） |
| `TStringBuilder` | `StringBuilder.h` | 栈上/内联缓冲的字符串拼接器，零分配拼接 |
| `FText` | `Internationalization/Text.h` | 本地化文本：Namespace + Key + SourceString + 历史（FTextHistory） |
| `FTextHistory` | `Text.h` | 记录文本"怎么来的"（静态、格式化、数字、日期…），用于编辑器反射与重算 |
| `LOCTEXT` / `NSLOCTEXT` | `Text.h` 宏 | 声明可翻译文本；`LOCTEXT(Key, Literal)` 使用当前 `LOCTEXT_NAMESPACE` |
| `INVTEXT` | `Text.h` 宏 | 文化不变文本（Culture Invariant），不参与翻译 |
| `FText::AsNumber/AsCurrency/AsDate` | `Text.h` | 按当前文化格式化数字/货币/日期 |

## 三、原理详解

### 3.1 FName：全局名表与 12 字节句柄

#### 3.1.1 内存布局

```cpp
// 节选：NameTypes.h —— FName 本体
class FName
{
	FNameEntryId ComparisonIndex; // 比较索引（大小写不敏感条目）
	FNameEntryId DisplayIndex;    // 显示索引（大小写保留条目，仅 WITH_CASE_PRESERVING_NAME）
	int32         Number;         // 实例编号
};
```

`NAME_SIZE = 1024`（单条名字最大字符数），`FNameEntry` 里是 `union { ANSICHAR AnsiName[NAME_SIZE]; WIDECHAR WideName[NAME_SIZE]; }`——名字**按原字符宽度**存储（纯 ASCII 存 ANSI，含宽字符才存 WIDE），省内存。

#### 3.1.2 全局池：FNamePool

`UnrealNames.cpp` 中的 `FNamePool` 由三部分组成：

1. **`FNameEntryAllocator Entries`**：按"块"分配条目的线程安全分配器。每块大小 = `alignof(FNameEntry) * FNameBlockOffsets`（`FNameBlockOffsets = 1 << 16 = 65536` 个条目槽），最多 `1 << 13 = 8192` 块；`FNameEntryId` 正是 `块号(13bit) | 块内偏移(16bit)` 编码（共 29 位），所以**一个 ID 就是一个内存地址的编码**，`Resolve` 只需 `Blocks[Block] + Stride * Offset`，无锁、无查表。
2. **`ComparisonShards[FNamePoolShards]`**：大小写不敏感哈希分片表（`FNamePoolShards = 1 << 8 = 256`，保留大小写时 `1 << 10 = 1024`）。每片一把读写锁 + 开放寻址槽（`FNameSlot`，槽内同时存条目 ID 与探针哈希，先比哈希、再比字符串，减少缓存访问）。
3. **`ENameToEntry[]`**：`EName::MaxHardcodedNameIndex` 长的数组，启动时把 `UnrealNames.inl` 里所有硬编码名（`NAME_None`、`NAME_All`、`NAME_Game`…）直接 `Store` 进池，`FName(EName)` 只需一次数组索引。

哈希函数是 `CityHash64`（`FNameHash::GenerateHash`），分片号取哈希高 32 位模分片数——多线程并发创建名字时锁竞争被分散到各分片。

#### 3.1.3 创建流程

```mermaid
flowchart TD
    A["FName(TEXT(\"Bone_2\"))"] --> B[解析数字后缀<br/>\"Bone\" + Number=2]
    B --> C[FNameHash::GenerateHash<br/>CityHash64(字符串)]
    C --> D[按哈希高 32 位选分片<br/>ComparisonShards[shard]]
    D --> E{同分片加读锁<br/>开放寻址探测}
    E -- 命中 --> F[返回已有 FNameEntryId<br/>不重复存储]
    E -- 未命中 --> G[加写锁 → 分配新 FNameEntry<br/>写入 Ansi/Wide 名字 + Header]
    G --> H[插入哈希表槽位<br/>FNameSlot = EntryId + ProbeHash]
    H --> I{需要保留大小写?<br/>WITH_CASE_PRESERVING_NAME}
    I -- 是 --> J[大小写不同 → 再建/查 Display 条目<br/>DisplayIndex 指向它]
    I -- 否 --> K[DisplayIndex = ComparisonIndex]
    J --> L[构造 FName<br/>ComparisonIndex + DisplayIndex + Number]
    K --> L
```

要点：

- **大小写不敏感**：比较索引的哈希与相等判断都对小写化后的字符串做（`FNamePoolShard<ENameCase::IgnoreCase>`）。因此 `FName("foo") == FName("Foo")` 为真（`IsEqual` 默认 `IgnoreCase`）；
- **大小写保留**：`WITH_CASE_PRESERVING_NAME`（默认开启于 WITH_EDITORONLY_DATA 平台）下，`ToString()` 用 DisplayIndex 还原你当初输入的大小写（`GetPlainNameString` 返回比较名的小写形式）；
- **Number 后缀**：`FName("Bone_2")` 把 `_2` 解析成 Number=2（外部编号），`GetPlainNameString` 只给 `Bone`；运行时若出现重名（如场景里第二个同名 Actor），引擎自动递增 Number，保证"每个名字唯一"；
- **`NAME_None`**：`FNameEntryId` 值 0 且 `Number == NAME_NO_NUMBER_INTERNAL`，是"空名字"，比较/哈希均为常量级。

#### 3.1.4 FLazyName：静态初始化安全

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

全局静态对象构造期间调用 `FName(字面量)` 有顺序风险（名字池可能还没初始化），`FLazyName` 把"入池"推迟到首次使用，引擎内部大量使用（如 `FRigUnit::GetMethodName()` 里的 `static const FLazyName MethodName = FRigVMStruct::ExecuteName;`）。

#### 3.1.5 比较与哈希

- `FName::operator==`：比较 ComparisonIndex（+Number），常数时间；
- `FName::Compare`：先按比较条目做**词典序**（`CompareLexical`），再比 Number；`FNameLexicalLess` 用于需要稳定排序的场景；
- `GetTypeHash(FName)`：`GetTypeHash(GetComparisonIndexInternal())`——哈希只依赖比较索引，**与大小写无关**，因此 `TMap<FName, ...>` 的键查找天然大小写不敏感；
- `FNameFastLess`：按 ID 数值排序，更快但进程内不稳定（ID 分配顺序依赖创建顺序）。

### 3.2 FString：堆分配的动态字符串

#### 3.2.1 类型与内存

`FString` 实际是宏实例化的 `TString<TCHAR>`（`UnrealString.h` 顶部 `#define UE_STRING_CLASS FString` + `UE_STRING_CHARTYPE TCHAR`）。TCHAR 在 Windows 是 `wchar_t`（UTF-16 编码单元），所以：

- 每个字符 2 字节；`FString` 内部是**堆分配的连续缓冲**（行为类似 TArray），`GetCharArray()` 可取底层 `TArray<TCHAR>`；
- 拷贝 `FString` 是深拷贝（除非 `MoveTemp`），`TMap<FString, ...>` 的键每次比较都要扫字符串内容；
- `TEXT("...")` 宏把 ANSI 字面量转成 TCHAR 字面量；`FString::Printf`、`FString::Format` 是常用构造方式。

#### 3.2.2 转换陷阱

| 写法 | 结果 | 陷阱 |
| --- | --- | --- |
| `FString(TEXT("abc"))` | 正常 | 别漏 `TEXT()`：`FString("abc")` 从 ANSI 转码，非 ASCII 内容可能乱码 |
| `FString::Printf(TEXT("%s"), *Str)` | 正常 | `%s` 必须配 `*Str`（取 TCHAR*），漏 `*` 是编译错误/崩溃 |
| `Name.ToString()` | 每次分配新 FString | 热路径上频繁调用会产生大量小分配；优先 `Name.AppendString(Builder)` |
| `FString(CharPtr)` | 深拷贝 + 计算长度 | 有现成长度时用 `FString(CharPtr, Len)` 或 `FStringView` 避免 `Strlen` |
| `FText::ToString()` | 可本地化文本转显示字符串 | 结果随当前文化变化，**不能**用于存档/比较/网络 |
| `FString == FString` | 内容逐字符比较 | 大小写敏感；需要忽略大小写用 `Equals(Other, ESearchCase::IgnoreCase)` |
| `FString::Contains/Find` | 子串搜索 | 大字符串上 O(n)，循环里注意；`TCHAR` 与 `FStringView` 转换注意生命周期 |

#### 3.2.3 更快的替代

- `FStringView`：不拥有数据的视图，函数入参优先用 `FStringView`/`FAnsiStringView` 避免拷贝；
- `TStringBuilder<256>`：栈上缓冲 + 溢出转堆，`Appendf/Append` 拼接零分配；
- `TSharedString`：共享引用计数字符串（5.x 新增），适合大量共享只读文本。

### 3.3 FText：本地化文本

#### 3.3.1 三个身份证：Namespace / Key / SourceString

```cpp
// 节选：Text.h —— FText 关键接口
class FText
{
	const FString& GetSourceString() const; // 源语言字符串（开发语言）
	// Namespace / Key 通过 FTextHistory 或文本属性获得
};
```

一个可翻译 FText 由三部分组成：

| 部分 | 作用 | 例子 |
| --- | --- | --- |
| **Namespace（命名空间）** | 分组，避免不同模块 Key 冲突 | `"Gameplay/Inventory"`（`LOCTEXT_NAMESPACE`） |
| **Key** | 同命名空间内唯一标识 | `"Item_Count"` |
| **SourceString（源串）** | 开发时写的字符串，翻译表以它为基准 | `"You have {0} items"` |

翻译表（.po/.locres）按 `Namespace + Key` 定位译文；**改 Key 或 Namespace 会丢失既有翻译**，改源串则需要重新翻译。C++ 里：

```cpp
#define LOCTEXT_NAMESPACE "MyModule"
// 声明可翻译文本（编辑器里可被收集进本地化管线）
FText::FromString(LOCTEXT("Hello", "Hello, world!").ToString()); // 反例：FText 转 String 再转回，丢失本地化
FText Label = LOCTEXT("ItemCount", "You have {0} items");       // 正例
#undef LOCTEXT_NAMESPACE
```

`FText::Format(Label, FText::AsNumber(Count))` 用 `{0}` 占位符做运行时替换——占位符替换发生在**显示时**，翻译串里的占位符顺序可以不同（如日语 "物品数：{0}"）。

#### 3.3.2 本地化查询流程

```mermaid
flowchart TD
    A["LOCTEXT(\"Key\", \"Source\")<br/>编译进二进制"] --> B[本地化收集工具<br/>Gather（编辑器/构建时）]
    B --> C["生成本地化数据<br/>.po / .locres"]
    C --> D[打包进项目<br/>Localization 目录]
    D --> E[运行时 FText::ToString]
    E --> F[FTextLocalizationManager<br/>按当前 Culture 查表]
    F --> G{Namespace+Key 命中?}
    G -- 是 --> H[返回译文（可含占位符）]
    G -- 否 --> I[返回 SourceString 原文<br/>（回退源语言）]
    H --> J[FText::Format 替换 {0} {1}]
    I --> J
```

#### 3.3.3 不可翻译与"假 FText"

- `FText::FromString(...)` / `INVTEXT(...)`：**文化不变**（Culture Invariant），没有 Namespace/Key，不参与翻译——适用于日志、调试信息、用户自己输入的内容；
- `FText::AsNumber / AsCurrency / AsDate / AsTime`：按当前文化格式化（千分位、货币符号、日期格式），结果也是"生成文本"，可被翻译工具逆向（`FTextHistory` 记录生成参数，编辑器里可以反编辑）；
- `FText::IsEmpty / IsTransient`：`IsTransient` 为真表示文本是运行时生成的（从字符串操作而来），不应入库。

> 判断口诀：**玩家看得见的 → FText；代码/数据内部标识 → FName；动态拼接的显示文本 → 先 FText::Format；纯内部临时文本 → FString（或 FStringView）。**

## 四、三者选型与转换

| 场景 | 首选 | 原因 |
| --- | --- | --- |
| 对象/资产/骨骼/插槽/GameplayTag 的标识 | `FName` | 全局去重、常数时间比较与哈希、序列化稳定 |
| 需要稳定排序/大小写敏感比较的名字 | `FName` + `Compare`（词典序） | FName 支持词典序比较，仍比 FString 快 |
| 文件路径、网络字符串、日志、配置键 | `FString` | 内容可变、可拼接 |
| 玩家可见 UI 文本 | `FText` | 本地化必需 |
| 数字/货币/日期显示 | `FText::AsNumber` 等 | 文化感知格式化 |
| 频繁拼接 | `TStringBuilder` / `FString::Format` | 避免重复分配 |
| 只读参数传递 | `FStringView` | 零拷贝 |

转换对照表：

| 从 → 到 | 写法 | 注意 |
| --- | --- | --- |
| FName → FString | `Name.ToString()` | 热路径避免；用 `Name.AppendString(Builder)` |
| FString → FName | `FName(Str)` | 入池去重；内容变化频繁时别用 |
| FName → FText | `FText::FromName(Name)` | 结果文化不变，仅用于显示标识 |
| FString → FText | `FText::FromString(Str)` | 文化不变；**不要**用于可翻译文案 |
| FText → FString | `Text.ToString()` | 仅显示；存档/比较用 `Text.GetSourceString()`（仍要谨慎） |
| FText 可翻译声明 | `LOCTEXT` / `NSLOCTEXT` | 必须 `#define LOCTEXT_NAMESPACE` 且成对 `#undef` |
| FText 占位 | `FText::Format(Template, Args...)` | 模板来自 LOCTEXT，参数用 FText |

## 五、示例

### 5.1 FName 热路径缓存（仿引擎内部写法）

```cpp
// 每帧查询骨骼索引：不要每帧 FName 构造 + GetBoneIndex
// 反例（每帧分配与哈希）：
int32 Index = Mesh->GetBoneIndex(TEXT("spine_01"));

// 正例（FLazyName 或静态 FName 缓存）：
namespace MyBoneNames
{
	static const FLazyName Spine01(TEXT("spine_01"));
}

// 组件初始化时缓存索引
int32 CachedSpineIndex = INDEX_NONE;
void Init(USkeletalMeshComponent* Mesh)
{
	CachedSpineIndex = Mesh->GetBoneIndex(FName(MyBoneNames::Spine01));
}
```

### 5.2 FString 零分配拼接

```cpp
// 反例：多次 ToString + += 造成大量堆分配
FString Path = FString::Printf(TEXT("%s/%s"), *BaseDir, *FileName);

// 正例：TStringBuilder 栈缓冲
TStringBuilder<256> Builder;
Builder.Append(BaseDir);
Builder.Append(TEXT("/"));
Builder.Append(FileName);
FString FinalPath = Builder.ToString(); // 一次分配
```

### 5.3 FText 完整示例

```cpp
#define LOCTEXT_NAMESPACE "InventoryUI"

// 模板带占位符，显示时按文化格式化数量
const FText ItemCountTemplate = LOCTEXT("ItemCount", "You have {0} items");
FText FinalText = FText::Format(ItemCountTemplate, FText::AsNumber(ItemCount));

// 数字按当前文化显示（千分位、小数点随语言）
FText Price = FText::AsCurrency(Value, TEXT("USD"));

#undef LOCTEXT_NAMESPACE
```

## 六、最佳实践

1. **标识一律 FName**：UObject 名称、资产路径段、骨骼/插槽/GameplayTag、`TMap` 键。键数量有限且重复率高，池化收益最大。
2. **不要在热路径构造 FName**：`FName(Str)` 要算 CityHash64 并可能加锁；能缓存就缓存（`FLazyName`、static、成员变量）。
3. **ToString() 谨慎**：`FName::ToString`/`FString::Printf` 都是堆分配。日志高频处用 `TStringBuilder` 或直接 `AppendString`。
4. **玩家可见文本只用 FText**：UI 控件（UMG）属性已是 FText 类型；C++ 传参也要用 FText，否则本地化工具收集不到。
5. **FText 不可存档/比较**：用 FText 做字典键、存档字段都是反模式；要稳定的本地化 ID 用 `FTextKey`/Namespace+Key 或自定义 ID。
6. **`FText::FromString` ≠ 本地化**：它产生文化不变文本。若你从配置文件读 UI 文案并希望可翻译，请走本地化数据表（如 `FDataTable` + `FText` 列，或 .po）。
7. **大小写行为要明确**：`TMap<FName>` 查找大小写不敏感（哈希基于比较索引）；`TMap<FString>` 大小写敏感。混用前想清楚。
8. **Number 后缀是特性不是 bug**：编辑器里同名对象自动 `_2`、`_3`，序列化/加载依赖 Number 区分；自己拼 `FName(名字_序号)` 时用 `FName::NumberToName` 风格的工具或直接构造 `FName(Str, Number)`。

## 七、FAQ

**Q1：FName 到底多大？**
12 字节（两个 `FNameEntryId` 各 4 字节 + `Number` 4 字节）。`TIsPODType<FName>` 为真，可 memcpy。注意：在不保留大小写的平台（非编辑器）`DisplayIndex` 被优化掉，仍是 8 字节布局（`UE_FNAME_OUTLINE_NUMBER` 等宏控制）。

**Q2：FName 池会无限增长吗？**
池只增不减（名字一旦创建不会回收）。运行时动态拼大量唯一字符串（如每帧 `FName(时间戳)`）会造成内存泄漏式增长——这正是"标识用 FName、动态数据用 FString"的原因。

**Q3："Foo" 和 "foo" 是同一个 FName 吗？**
比较索引相同（`==` 为真、哈希相同），但 `WITH_CASE_PRESERVING_NAME` 下 DisplayIndex 不同，`ToString()` 还原各自大小写。`IsEqual(Other, ENameCase::CaseSensitive)` 用 DisplayIndex 比较，可区分。

**Q4：`FName("Bone_2")` 的 `_2` 和场景里自动生成的 `Bone_2` 有区别吗？**
机制相同：都是 Number=2 的编号名。区别在于 `_2` 是字面量显式声明的；运行时重复名是引擎在发现重名时自动递增分配的。`GetPlainNameString` 都只返回 `Bone`。

**Q5：FText 翻译在运行时如何切换语言？**
`FInternationalization`/`FTextLocalizationManager` 维护文化栈，切换 `CurrentCulture` 后，所有"活"的 FText 在下次 `ToString()` 时按新文化重查表（历史机制保证重新格式化数字/日期）。旧文本对象本身不变。

**Q6：为什么不能用 FString 做本地化？**
因为翻译是"按 ID 查表"，而 FString 内容是值本身——改了源串就无法定位译文，也没有 Namespace/Key 语义，收集工具无法提取。FText 把"内容"与"身份（NS/Key）"分离，翻译管线才成立。

**Q7：`FText::Format` 的占位符和 `FString::Printf` 的 `%s` 有什么区别？**
`{0}` 是 FText 占位符，参数必须是 FText（或可转 FText 的类型），翻译串可重排占位符顺序；`%s` 是 C 风格格式串，参数是 TCHAR*，不参与本地化。UI 文案用前者，日志用后者。

**Q8：为什么 `FString` 在 `TMap` 里当键慢？**
哈希要扫整个字符串内容（O(len)），比较也要逐字符；而 FName 键的哈希与比较都是整数运算（O(1)）。同内容字符串每次拷贝/入容器都是一份新堆内存。

## 八、关联阅读

- [01-引擎基础/01 UObject 与反射系统](./01-UObject与反射系统.md)（FName 是 UPROPERTY 名与对象名的基石）
- [01-引擎基础/05 场景组件与变换体系](./05-场景组件与变换体系.md)（骨骼/插槽名的 FName 标识实践）
- [12-引擎源码分析/07 容器与内存管理源码](../12-引擎源码分析/07-容器与内存管理源码.md)（FString 堆分配与 TArray 内存模型）
- [12-引擎源码分析/01 UPROPERTY 与反射系统源码](../12-引擎源码分析/01-UPROPERTY与反射系统源码.md)（FName 在反射元数据中的角色）
- 官方文档：FName / FText（Localization）指南、String Handling 最佳实践
