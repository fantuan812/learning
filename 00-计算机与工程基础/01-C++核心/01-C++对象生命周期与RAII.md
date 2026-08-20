# 01-C++对象生命周期与RAII
> 验证与基准：按文中命令执行最小实验，记录结果与边界。

> 知识基线：C++11~C++20 标准语义（storage duration、构造/析构、异常安全、所有权）；编译器基准 MSVC 2022（v14.44）/ x64；UE 对照以本机 UE 5.8 源码为准。
> 版本基准：C++11 引入移动语义与 `unique_ptr/shared_ptr/weak_ptr`；C++17 起析构函数默认 `noexcept` 语义沿用 C++11 规则（析构默认不抛）。
> 适用范围：所有 C++ 开发（客户端 UE / 服务端 / 工具链）；UE 部分仅适用于 UE5.8 当前版本，4.27 及早期 UE5 仅作差异说明。
> 官方参考：[cppreference - Storage duration](https://en.cppreference.com/w/cpp/language/storage_duration)、[cppreference - RAII](https://en.cppreference.com/w/cpp/language/raii)、[cppreference - Exceptions](https://en.cppreference.com/w/cpp/language/exceptions)。
> 最后更新：2026-08-12（首版）。
> 知识成熟度：L2（标准/官方资料验证；示例可编译运行，未做独立 Benchmark）。

## 1. 概述

对象生命周期是 C++ 一切正确性的根：**构造决定对象何时可用，析构决定资源何时归还**。不理解生命周期，`TArray` 扩容、`TSharedPtr` 引用计数、`UObject` 垃圾回收、服务端连接句柄、锁与文件描述符的释放都会变成"凭经验猜"。

本文回答四个问题：

1. C++ 对象有哪几种存储期（storage duration），各自何时构造/析构？
2. RAII 为什么是 C++ 资源管理的唯一正解？
3. 异常安全的三档保证是什么，和 RAII 什么关系？
4. UE 的 `UObject` 生命周期为什么和普通 C++ 对象不一样，边界在哪里？

读完本文应能：说出任意对象从构造到析构的完整顺序；解释为什么析构函数不能抛异常；说明 `unique_ptr` 与 `shared_ptr` 的适用边界；以及为什么 UE 里 `UObject` 不能 `delete`。

## 2. 核心概念

| 术语 | 中文 | 一句话定义 |
| --- | --- | --- |
| Storage duration | 存储期 | 对象生命周期的四种来源：自动（栈）、静态、线程、动态（堆） |
| Lifetime | 生命周期 | 从构造完成到析构开始的区间；引用/指针只在生命周期内有效 |
| RAII | 资源获取即初始化 | 资源在构造函数获取、在析构函数释放，与对象作用域绑定 |
| Unwinding | 栈展开 | 异常抛出时按逆序析构已构造的局部对象 |
| Exception safety | 异常安全 | 函数在抛异常时对状态的三档保证：基本/强/不抛 |
| Ownership | 所有权 | 谁负责释放资源；单一所有权避免双重释放与泄漏 |
| `unique_ptr` | 独占智能指针 | 独占所有权，移动语义转移，析构即释放 |
| `shared_ptr` | 共享智能指针 | 引用计数共享所有权，`weak_ptr` 观察不拥有 |
| Dangling | 悬垂 | 引用/指针指向已结束生命周期的对象，解引用为 UB |

## 3. 原理详解

### 3.1 四种存储期

```cpp
int g = 0;              // 静态存储期：程序启动前构造，进程结束析构
thread_local int t = 0; // 线程存储期：线程创建时构造，线程退出析构

void f() {
    int a = 0;          // 自动存储期：进入作用域构造，离开作用域逆序析构
    static int s = 0;   // 静态存储期（局部静态）：首次执行到声明时构造
    int* p = new int(0);// 动态存储期：new 构造，必须 delete 析构
    delete p;
}
```

关键规则：

- **自动对象**按声明逆序析构；成员按声明顺序构造、逆序析构；基类先于派生类构造、逆序析构。
- **静态/线程对象**的析构顺序与构造顺序相反；跨翻译单元的初始化顺序未定义（用局部静态或单例规避）。
- **动态对象**的生命周期完全由代码控制——这正是泄漏与悬垂的来源，因此必须用 RAII 包装。
- 引用和指针本身不拥有对象；对象生命周期结束时，指向它的任何引用/指针立即失效（悬垂）。

### 3.2 构造与析构的完整顺序

```cpp
struct Base { Base() {} ~Base() {} };
struct Member { Member() {} ~Member() {} };
struct Derived : Base {
    Member m;              // 成员声明顺序决定构造顺序
    Derived() : Base(), m() {}   // 先基类，后成员，再本类函数体
    ~Derived() {}          // 先本类函数体，后成员逆序，再基类
};
```

构造顺序：`Base` → 成员（按声明序）→ `Derived` 函数体。
析构顺序：`Derived` 函数体 → 成员（逆声明序）→ `Base`。

推论（工程上最常踩的坑）：

- 构造函数里**成员初始化列表顺序必须与声明顺序一致**，否则有未初始化读风险（`-Wreorder` 类警告的由来）。
- 析构函数里**不能调用虚函数期望派生态**：析构时派生部分已销毁，虚调用落到当前类的实现。
- 基类析构不是 `virtual` 时，通过基类指针 `delete` 派生对象是未定义行为（UE `UObject` 的析构被 GC 统一管理，与此不同，见 3.6）。

### 3.3 RAII：资源获取即初始化

RAII 把"资源"（内存、锁、文件、套接字、数据库连接、渲染命令列表）绑定到对象生命周期：

```cpp
class FileGuard {
    FILE* f_;
public:
    explicit FileGuard(const char* path) : f_(fopen(path, "r")) {
        if (!f_) throw std::runtime_error("open failed");
    }
    ~FileGuard() { if (f_) fclose(f_); }
    FileGuard(const FileGuard&) = delete;
    FileGuard& operator=(const FileGuard&) = delete;
};

void read_config() {
    FileGuard f("config.ini");   // 获取资源
    parse(f);                    // 正常路径或抛异常，离开作用域都会 fclose
}                                // 栈展开/正常返回都会执行析构
```

为什么这是唯一正解：

- **所有退出路径统一释放**：`return`、异常、`break`/`continue`、提前 `goto`，析构都会执行；手写 `close()` 在每个提前返回点都要补一遍，漏一处就是泄漏。
- **释放顺序确定**：逆序析构保证"后获取的先释放"，天然满足锁、嵌套缓冲区的释放顺序要求。
- **异常安全的基础**：栈展开会完整析构已构造的局部对象，资源不会因异常泄漏。

### 3.4 异常安全三档保证

| 保证 | 含义 | 工程应用 |
| --- | --- | --- |
| 基本保证 | 抛异常后对象处于有效但未指定的状态，不泄漏资源 | 最低要求，绝大多数业务代码 |
| 强保证 | 操作要么完全成功，要么状态不变（commit-or-rollback） | 数据库事务、背包变更、状态机回滚 |
| 不抛保证 | 函数绝不抛异常（`noexcept`） | 析构、移动构造（为容器扩容服务）、热路径 |

RAII 与三档保证的关系：

- 用 RAII 持有临时资源（新容器、新句柄），操作失败时只需"提交或整体丢弃"，天然得到强保证的雏形。
- 析构函数**默认不抛**（C++11 起析构隐式 `noexcept`）：析构抛异常且正处于栈展开时直接 `std::terminate`。规则：**析构函数永远不要抛出异常**，释放失败也要吞掉并记录。
- 移动构造标记 `noexcept` 是容器扩容的前提（见 02-Copy-Move与值语义）；移动抛异常时 `vector` 只能回退到复制或放弃强保证。

### 3.5 所有权：谁负责释放

| 智能指针 | 所有权 | 典型场景 |
| --- | --- | --- |
| `std::unique_ptr<T>` | 独占 | 工厂返回、容器元素、pimpl |
| `std::shared_ptr<T>` | 共享（引用计数） | 多个系统共享同一对象且生命周期不确定 |
| `std::weak_ptr<T>` | 观察（不计数） | 缓存、观察者，打破 `shared_ptr` 环 |
| 裸指针/引用 | 无所有权 | 参数传递、非拥有观察（须保证生命周期） |

规则：

- 默认用 `unique_ptr`；确需共享才用 `shared_ptr`；`shared_ptr` 循环引用（A↔B 互持）会导致双方永不释放，用 `weak_ptr` 破环。
- 不要把同一个裸指针同时交给两个独立所有者，否则双重释放。
- `shared_ptr` 的控制块本身是堆分配：每次拷贝原子递增计数，热路径上比 `unique_ptr` 贵（详见 W1-05 allocator 与容器篇的扩展）。

### 3.6 UE 对照：UObject 生命周期与 C++ 生命周期不同

UE 的 `UObject` 生命周期由**垃圾回收（GC）**管理，不是普通的 C++ 生命周期：

```cpp
UObject* obj = NewObject<UMyActor>(this);   // 由 GC 管理，禁止 delete
// obj 由 GC 在可达性分析后销毁；被引用（AddToRoot / 强引用 / 被根对象持有）则存活
```

差异与边界：

| 维度 | 普通 C++ 对象 | UE `UObject` |
| --- | --- | --- |
| 创建 | 栈/`new` | `NewObject`/`SpawnActor`，统一分配 |
| 释放 | 作用域/`delete`/智能指针 | GC 可达性分析（`MarkAsGarbage` 等） |
| 悬垂防护 | 智能指针/所有权约定 | `TWeakObjectPtr`（GC 后自动失效） |
| 值语义 | `TArray`/`TMap` 元素可复制 | `USTRUCT` 支持复制；`UObject` 只能引用 |
| 生命周期事件 | 构造/析构 | `BeginDestroy`/`FinishDestroy`、`IsValid` |

具体规则（UE5.8）：

- `UObject` **禁止栈上构造、禁止 `delete`**：引擎统一内存管理，`delete` 会导致双重释放或 GC 后悬垂。
- 跨帧持有 `UObject*` 必须考虑 GC：用 `TWeakObjectPtr` 观察、`UPROPERTY()` 强引用或 `AddToRoot`（谨慎，防泄漏）。
- `TSharedPtr`/`TUniquePtr` 用于非 UObject（如普通 C++ 对象、插件内部类）；`TWeakObjectPtr` 才是 UObject 的"弱引用"。
- 值类型（`USTRUCT`、`int32`、`FVector`）走普通 C++ 生命周期，与 GC 无关。

> 关联：UObject 与 GC 的完整源码分析见 [游戏知识/01-引擎基础](../../游戏知识/01-引擎基础/README.md) 与 [12-引擎源码分析](../../游戏知识/12-引擎源码分析/README.md)（UObject/GC 专题）；本层只讲与 C++ 生命周期的对接边界。

### 3.7 服务端场景：连接、会话与事务的生命周期

游戏服务端的连接、会话、事务天然是"生命周期对象"，用 RAII 建模可以消灭一类系统性泄漏：

```cpp
class SessionGuard {
    Session* s_;
public:
    SessionGuard(Server& server, uint64 id)
        : s_(server.AcquireSession(id)) {          // 获取：登记会话、启动心跳
    }
    ~SessionGuard() {
        s_->server.ReleaseSession(s_->id);          // 释放：停止心跳、落库、广播下线
    }
    Session& operator*() const { return *s_; }
    SessionGuard(const SessionGuard&) = delete;
};
```

工程要点：

- **连接对象**：套接字 fd、加密上下文、读缓冲、写缓冲全部随会话对象构造/析构，断线、超时、玩家登出走同一条析构路径。
- **心跳/定时器**：随会话创建注册、随会话析构注销；否则登出后定时器仍回调悬垂会话（服务端典型崩溃源）。
- **数据库事务**：`Begin → 操作 → Commit/Rollback` 用 guard 表达"析构时未 Commit 则 Rollback"，天然覆盖所有提前返回路径（见 4.4）。
- **登出顺序**：先停输入（网络层），再停逻辑（会话对象），最后释放存储（落库）——顺序本身就是一组 RAII 对象的析构顺序。

## 4. 示例

### 4.1 最小 ScopeGuard（RAII 通用工具，可编译运行）

```cpp
#include <cstdio>
template <typename F>
class ScopeGuard {
    F fn_;
    bool active_ = true;
public:
    explicit ScopeGuard(F fn) : fn_(std::move(fn)) {}
    ~ScopeGuard() { if (active_) fn_(); }
    void dismiss() { active_ = false; }   // 提交成功时取消回滚
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
};

void commit_or_rollback(bool ok) {
    ScopeGuard rollback([] { std::puts("rollback: release resource"); });
    if (ok) {
        rollback.dismiss();
        std::puts("commit");
    }
}

int main() {
    commit_or_rollback(true);   // 输出 commit
    commit_or_rollback(false);  // 输出 rollback: release resource
}
```

运行：`cl /nologo /utf-8 /std:c++17 /EHsc scope_guard.cpp`（节选示意，可直接编译）。

### 4.2 锁的 RAII

```cpp
{
    std::lock_guard<std::mutex> lock(mu_);   // 获取锁
    state_ += 1;                              // 抛异常也自动解锁
}                                             // 离开作用域解锁
```

### 4.3 `unique_ptr` 自定义删除器（服务端句柄/套接字）

```cpp
auto closer = [](int* fd) { if (fd && *fd >= 0) close(*fd); delete fd; };
std::unique_ptr<int, decltype(closer)> sock(new int(socket(...)), closer);
```

### 4.4 数据库事务 RAII（commit-or-rollback）

```cpp
class TransactionGuard {
    Db& db_;
    bool committed_ = false;
public:
    explicit TransactionGuard(Db& db) : db_(db) { db_.Begin(); }
    ~TransactionGuard() { if (!committed_) db_.Rollback(); }   // 任何未提交路径自动回滚
    void commit() { db_.Commit(); committed_ = true; }
    TransactionGuard(const TransactionGuard&) = delete;
};

void grant_item(Db& db, uint64 player, uint64 item) {
    TransactionGuard tx(db);
    db_.Execute("INSERT ...");      // 中途抛异常 → 析构回滚
    db_.Execute("UPDATE ...");
    tx.commit();                    // 全部成功 → 提交
}
```

这就是"强异常保证"的工程形态：要么全部生效，要么像没发生过。

## 5. 最佳实践

1. **资源一律 RAII**：锁、文件、套接字、句柄、GPU 资源、数据库连接；禁止裸 `new`/`delete` 配对出现在业务代码。
2. **析构函数 `noexcept`（默认即可），永不抛异常**；释放失败记录日志而非抛出。
3. **成员按声明顺序初始化**；构造函数里不要调用可被重写的虚函数。
4. **默认 `unique_ptr`**，确需共享才 `shared_ptr`，用 `weak_ptr` 打破环。
5. **裸指针只做非拥有观察**，并保证被观察对象生命周期更长（参数、短期局部）。
6. UE 侧：`UObject` 不 `delete`、不栈上构造；跨帧引用用 `TWeakObjectPtr`/`UPROPERTY`。
7. 强保证实现套路：先在局部构造新状态，成功后再一次性替换（commit-or-rollback），配合 ScopeGuard 回滚。
8. 容器存值还是存指针：值类型（可移动、尺寸合理）直接存值；多态/大对象用 `unique_ptr`；`TArray` 同理。

落地检查清单：

| 场景 | 检查项 |
| --- | --- |
| 自动存储期 | 离开作用域前是否有未释放资源？是否依赖了析构顺序？ |
| 动态存储期 | 谁拥有？释放路径唯一吗？会不会双重释放或泄漏？ |
| 异常路径 | 每个提前 return / 抛异常路径是否都走 RAII 释放？ |
| 析构函数 | 是否可能抛出？是否访问了已销毁的派生部分？ |
| UE 对象 | 是 UObject 吗？是否被 GC 管理？跨帧引用是否用 TWeakObjectPtr/UPROPERTY？ |

## 6. FAQ

**Q1：析构函数能抛异常吗？**
不能安全地抛。析构默认 `noexcept`，抛出会导致 `std::terminate`（栈展开期间抛出直接终止）。释放失败请记录并吞掉。

**Q2：为什么 `shared_ptr` 循环引用会泄漏？**
两个 `shared_ptr` 互持时引用计数永远 ≥1，控制块永不归零。用 `weak_ptr` 观察一侧即可破环。

**Q3：`unique_ptr` 能拷贝吗？**
不能（拷贝构造被删除），只能移动；这保证单一所有权。需要多个持有者时换 `shared_ptr`。

**Q4：UE 的 `UObject` 为什么不能 `delete`？**
因为引擎的 GC 与反射系统统一管理 UObject 分配与回收，`delete` 会造成双重释放或 GC 悬垂。销毁请用 `MarkAsGarbage`/`ConditionalBeginDestroy` 语义或让引用归零。

**Q5：`TWeakObjectPtr` 和 `std::weak_ptr` 一样吗？**
语义类似（观察不拥有），机制不同：前者靠 GC 的 `IsValid()` 判断对象是否已被回收，后者靠控制块强/弱计数。

**Q6：局部静态对象的析构顺序和全局对象有关吗？**
局部静态在首次执行时构造、按构造逆序析构，且与全局静态析构顺序交错；跨编译单元初始化顺序本身未定义，避免依赖。

**Q7：为什么基类析构要 `virtual`？**
通过基类指针 `delete` 派生对象时，若基类析构非虚，派生部分不会被析构（UB/资源泄漏）。有派生意图的类都应声明虚析构。

**Q8：局部静态对象初始化线程安全吗？**
C++11 起"magic statics"保证局部静态的首次初始化是线程安全的（编译器生成 guard）。跨编译单元初始化顺序仍未定义，所以不要把依赖初始化顺序的全局对象写成普通全局。

**Q9：UE 的 `TSharedPtr` 与 `std::shared_ptr` 线程安全一样吗？**
引用计数的增减都是原子操作，这点相同；但"同一个智能指针对象被多个线程同时读/写"两者都不安全。区别在生命周期管理：UObject 用 `TWeakObjectPtr` 观察 GC 对象，`std::weak_ptr` 观察引用计数对象，机制不同。

**Q10：为什么析构函数里不能调用虚函数期待派生态？**
析构从派生类向基类逐层执行，进入基类析构时派生部分已销毁；此时虚调用解析到当前类实现，通常无法访问派生成员。需要清理逻辑应在派生类析构里先做。

## 7. 验证与基准

- 标准语义：以上存储期/构造顺序/异常安全规则以 [cppreference - Storage duration](https://en.cppreference.com/w/cpp/language/storage_duration) 与 [cppreference - RAII](https://en.cppreference.com/w/cpp/language/raii) 为验证基准；构造/析构顺序可编译 4.1 示例用打印观察。
- 异常安全保证的工程定义见 [cppreference - Exceptions](https://en.cppreference.com/w/cpp/language/exceptions)。
- UE 部分以本机 UE 5.8 源码与 [UE 5.8 官方文档](https://dev.epicgames.com/documentation/en-us/unreal-engine)（UObject/GC 页面）为准；本层仅建立 C++ 侧边界，深度分析见 12-引擎源码分析。
- 后续升级 L3/L4 计划：为 ScopeGuard 增加异常注入测试（强保证验证），为锁 RAII 增加锁竞争 Benchmark（关联 W1-06）。

验证命令（Windows / MSVC）：

```powershell
cl /nologo /utf-8 /std:c++17 /EHsc scope_guard.cpp /Fe:scope_guard.exe
.\scope_guard.exe   # 预期输出依次为 commit 与 rollback: release resource
```

## 8. 关联阅读

- [02-Copy-Move与值语义](02-Copy-Move与值语义.md)：特殊成员函数、移动语义与容器扩容（含本机实验）。
- [00-计算机与工程基础](../README.md)：本层规划与门禁。
- [游戏知识/01-引擎基础](../../游戏知识/01-引擎基础/README.md)：UObject/反射/World 生命周期。
- [游戏知识/12-引擎源码分析](../../游戏知识/12-引擎源码分析/README.md)：UObject/GC 源码深度。
- [游戏服务端/01-架构与网络](../../游戏服务端/01-架构与网络/README.md)：服务端连接/会话生命周期的 RAII 实践。
