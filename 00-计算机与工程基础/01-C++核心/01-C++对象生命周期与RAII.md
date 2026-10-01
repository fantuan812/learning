---
type: Mechanism
title: "01-C++对象生命周期与RAII"
status: stable
verified: []
maturity: L2
updated: 2026-10-01
sources:
  - resource: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
    title: C++ Core Guidelines C.35 and resource management
  - resource: https://eel.is/c%2B%2Bdraft/class.dtor
    title: C++ destructor rules
  - resource: https://eel.is/c%2B%2Bdraft/thread.thread.destr
    title: std::thread destructor contract
  - resource: https://dev.epicgames.com/documentation/en-us/unreal-engine/smart-pointers-in-unreal-engine
    title: Unreal Smart Pointer Library
---
# 01-C++对象生命周期与RAII
> 验证与基准：按文中命令执行最小实验，记录结果与边界。

> 知识基线：C++11~C++20 标准语义（storage duration、构造/析构、异常安全、所有权）；编译器基准 MSVC 2022（v14.44）/ x64；UE对照保留5.8阅读背景；2026-10-01增量核对官方文档，未访问原本机引擎安装。
> 版本基准：C++11 引入移动语义与 `unique_ptr/shared_ptr/weak_ptr`；示例采用C++11；析构未显式写异常规格时依据子对象析构等规则推导，不能无条件概括为所有析构都不抛。
> 适用范围：所有 C++ 开发（客户端 UE / 服务端 / 工具链）；UE 部分仅适用于 UE5.8 当前版本，4.27 及早期 UE5 仅作差异说明。
> 官方参考：[cppreference - Storage duration](https://en.cppreference.com/w/cpp/language/storage_duration)、[cppreference - RAII](https://en.cppreference.com/w/cpp/language/raii)、[cppreference - Exceptions](https://en.cppreference.com/w/cpp/language/exceptions)。
> 最后更新：2026-10-01（释放责任、智能指针线程安全与取消/完成边界）。
> 知识成熟度：L2（标准/官方资料验证；示例可编译运行，未做独立 Benchmark）。

## 1. 概述

对象生命周期是 C++ 一切正确性的根：**构造决定对象何时可用，析构决定资源何时归还**。不理解生命周期，`TArray` 扩容、`TSharedPtr` 引用计数、`UObject` 垃圾回收、服务端连接句柄、锁与文件描述符的释放都会变成"凭经验猜"。

本文回答四个问题：

1. C++ 对象有哪几种存储期（storage duration），各自何时构造/析构？
2. RAII 如何把资源释放责任绑定到对象，哪些业务成功条件仍须显式表达？
3. 异常安全的三档保证是什么，和 RAII 什么关系？
4. UE 的 `UObject` 生命周期为什么和普通 C++ 对象不一样，边界在哪里？

读完本文应能：说出任意对象从构造到析构的完整顺序；解释为什么析构函数不能抛异常；说明 `unique_ptr` 与 `shared_ptr` 的适用边界；以及为什么 UE 里 `UObject` 不能 `delete`。

## 2. 核心概念

| 术语 | 中文 | 一句话定义 |
| --- | --- | --- |
| Storage duration | 存储期 | 存储占用的持续时间；自动/静态/线程/动态，不等于对象生命周期 |
| Lifetime | 生命周期 | 对象可按其类型正常使用的阶段；与存储复用、构造/析构中的特殊规则分开 |
| RAII | 资源获取即初始化 | 资源在构造函数获取、在析构函数释放，与对象作用域绑定 |
| Unwinding | 栈展开 | 异常抛出时按逆序析构已构造的局部对象 |
| Exception safety | 异常安全 | 函数在抛异常时对状态的三档保证：基本/强/不抛 |
| Ownership | 所有权 | 谁负责释放资源；单一所有权避免双重释放与泄漏 |
| `unique_ptr` | 独占智能指针 | 独占所有权，移动语义转移，析构即释放 |
| `shared_ptr` | 共享智能指针 | 引用计数共享所有权，`weak_ptr` 观察而不增加强引用计数 |
| Dangling | 悬垂 | 引用/指针指向已结束生命周期的对象，解引用为 UB |

## 3. 原理详解

### 3.1 四种存储期

```cpp
int g = 0;              // 静态存储期：程序启动前构造，进程结束析构
thread_local int t = 0; // 线程存储期；动态初始化时机有延迟规则，不保证创建线程即构造所有对象

void f() {
    int a = 0;          // 自动存储期：进入作用域构造，离开作用域逆序析构
    static int s = 0;   // 静态存储期（局部静态）：首次执行到声明时构造
    int* p = new int(0);// 动态存储期：new 构造，必须 delete 析构
    delete p;
}
```

关键规则：

- **自动对象**按声明逆序析构；成员按声明顺序构造、逆序析构；基类先于派生类构造、逆序析构。
- **静态/线程对象**遵循各自初始化/终止规则；不要把跨翻译单元动态初始化顺序当业务保证。局部静态可延迟建立依赖，但不是消灭所有析构顺序、递归初始化或线程访问问题的万能单例。
- **动态对象**的生命周期完全由代码控制——这正是泄漏与悬垂的来源，因此必须用 RAII 包装。
- 裸指针/引用本身不表达释放责任；生命周期结束后不能继续按活对象解引用。存储仍存在、指针数值仍非空，都不能证明对象仍可用。

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

- 构造函数里**成员初始化列表应与声明顺序一致**；实际顺序始终由声明决定，误以为列表能重排依赖会有未初始化读风险（`-Wreorder` 类警告的由来）。
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

RAII的主要收益与前提：

- **正常作用域退出与栈展开统一释放**：`return`、异常展开、`break`/`continue`、离开作用域的 `goto` 会析构已构造对象；手写 `close()` 在每个提前返回点都要补一遍，漏一处就是泄漏。
- **释放顺序确定**：逆序析构保证"后获取的先释放"，天然满足锁、嵌套缓冲区的释放顺序要求。
- **异常安全的基础**：栈展开会完整析构已构造的局部对象，由已完成构造的RAII成员持有的资源可被回收；若构造函数取得裸资源后再抛异常，该对象自身析构不会运行，必须让成员守卫先接管。

RAII依赖正常作用域退出/栈展开；进程崩溃、强制终止、abort/_Exit等路径不会因此自动释放所有局部资源。`noexcept`违规终止也不能作为可恢复异常路径测试。

### 3.4 异常安全三档保证

| 保证 | 含义 | 工程应用 |
| --- | --- | --- |
| 基本保证 | 抛异常后对象处于有效但未指定的状态，不泄漏资源 | 最低要求，绝大多数业务代码 |
| 强保证 | 操作要么完全成功，要么状态不变（commit-or-rollback） | 数据库事务、背包变更、状态机回滚 |
| 不抛保证 | 承诺异常不逃逸；`noexcept`违规会终止，不是自动恢复 | 析构、移动构造（为容器扩容服务）、热路径 |

RAII 与三档保证的关系：

- 用 RAII 持有临时资源（新容器、新句柄），操作失败时只需"提交或整体丢弃"，天然得到强保证的雏形。
- 析构未显式写异常规格时按子对象等规则推导，可能是potentially-throwing。设计释放接口时应让异常不逃逸；`noexcept`函数中逃逸异常、或异常展开期间析构又让异常逃逸，都可能触发终止。需要调用方处理的flush/commit/close失败应提供显式操作，析构仅作不抛的兜底，不能把吞错当业务成功。
- `noexcept`移动有利于vector重分配维持异常保证，但不是能扩容的必要条件；可能使用复制，或在仅可抛移动类型上有更弱保证。只在实际满足合同时标注，不能为了“走移动”强行加noexcept。

### 3.5 所有权：谁负责释放

| 智能指针 | 所有权 | 典型场景 |
| --- | --- | --- |
| `std::unique_ptr<T>` | 独占 | 工厂返回、容器元素、pimpl |
| `std::shared_ptr<T>` | 共享（引用计数） | 多个系统共享同一对象且生命周期不确定 |
| `std::weak_ptr<T>` | 观察（不增加强计数） | 缓存、观察者，打破 `shared_ptr` 环 |
| 裸指针/引用 | 无所有权 | 参数传递、非拥有观察（须保证生命周期） |

规则：

- 默认用 `unique_ptr`；确需共享才用 `shared_ptr`；`shared_ptr` 循环引用（A↔B 互持）会导致双方永不释放，用 `weak_ptr` 破环。
- 不要把同一个裸指针同时交给两个独立所有者，否则双重释放。
- `shared_ptr`共享控制块；`make_shared`可合并对象与控制块分配。共享计数维护有成本，但标准的线程安全合同不等于规定每次都用某条原子指令，更不保证业务对象安全；热路径成本应实测。

### 3.6 UE 对照：UObject 生命周期与 C++ 生命周期不同

UE的UObject仍是C++对象，但创建/销毁时机由引擎对象系统与GC编排，不能把普通delete所有权习惯直接套用：

```cpp
UObject* obj = NewObject<UMyData>(this); // UMyData为UObject派生数据类；Actor用SpawnActor
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
- 值类型本身走C++生命周期；USTRUCT内若含引擎对象引用，其可达性追踪仍取决于反射/持有路径，不能概括为所有USTRUCT都与GC无关。

> 关联：UObject 与 GC 的完整源码分析见 [游戏知识/01-引擎基础](../../游戏知识/01-引擎基础/README.md) 与 [12-引擎源码分析](../../游戏知识/12-引擎源码分析/README.md)（UObject/GC 专题）；本层只讲与 C++ 生命周期的对接边界。

### 3.7 服务端场景：连接、会话与事务的生命周期

游戏服务端的连接、会话、事务天然是"生命周期对象"，用 RAII 建模可以消灭一类系统性泄漏：

```cpp
class SessionGuard {
    Session* s_;
public:
    SessionGuard(Server& server, uint64 id)
        : s_(server.AcquireSession(id)) {          // 获取：登记会话、启动心跳
        if (!s_) throw std::runtime_error("session acquisition failed");
    }
    ~SessionGuard() noexcept {
        if (s_) s_->server.ReleaseSessionNoThrow(s_->id); // 仅不抛的本地释放合同
    }
    Session& operator*() const { return *s_; }
    SessionGuard(const SessionGuard&) = delete;
    SessionGuard& operator=(const SessionGuard&) = delete;
    SessionGuard(SessionGuard&&) = delete;
    SessionGuard& operator=(SessionGuard&&) = delete;
};
```

工程要点：

- **连接对象**：套接字 fd、加密上下文、读缓冲、写缓冲全部随会话对象构造/析构，断线、超时、玩家登出走同一条析构路径。
- **心跳/定时器**：注销只能按具体API合同阻止未来派发，已排队/执行中的回调可能仍存在。先撤销逻辑写回资格，再取消并等待安全完成或让共享任务状态独立存活，不能注销后立刻假定裸this安全。
- **数据库事务**：`Begin → 操作 → Commit/Rollback` 用 guard 表达"析构时未 Commit 则 Rollback"，天然覆盖所有提前返回路径（见 4.4）。
- **登出顺序**：停止接收新操作、撤销写回资格、完成业务保存/重试协议、收敛后台任务，再释放资源；落库与广播应由显式可报告失败的流程负责，析构顺序本身不证明这些操作成功。上述Session/Server是接口示意，Acquire失败与ReleaseSessionNoThrow合同须由真实实现定义。

### 3.8 活着、线程安全、结果仍适用，是三份合同

| 问题 | 能提供的机制 | 不能据此推出 |
| --- | --- | --- |
| 对象是否仍活着 | RAII拥有者、shared_ptr强引用、weak_ptr::lock成功 | 对象字段可无锁并发读写 |
| 读写是否同步 | mutex、正确原子协议、线程归属 | 本次异步结果还对应当前目标/会话 |
| 结果是否仍适用 | RequestId/Generation/Revision与关闭状态检查 | 工作线程已退出或资源已可回收 |

不要先`if (!weak.expired())`再使用另一路裸指针：检查和使用之间对象可能结束。应将一次[weak_ptr::lock](https://eel.is/c%2B%2Bdraft/util.smartptr.weak.obs)的结果保存在局部strong handle并判空；它保证持有期间的共享对象生命期，不自动保护成员，也不禁止会话在逻辑上关闭。

异步捕获的选择要显式：

- 捕获裸`this`/引用：任务可能比调用栈或对象活得久，必须由外部完成屏障证明安全。
- 捕获独立数据快照：工作只读值；拥有者销毁不会使该快照悬垂，但快照可能过期。
- 捕获shared_ptr：延长被捕获状态寿命，可能把最终析构推到工作线程。带线程亲和资源的对象不能因此随便跨线程销毁，还要防对象→任务→对象的强引用环。
- 捕获weak_ptr：执行时尝试获得临时所有权；失败则放弃。成功后仍检查业务代次和关闭状态。UE的TWeakObjectPtr不能直接等同这一套引用计数合同。

共享引用计数的同步也不保证“最后一个strong handle在哪个线程释放”。析构若访问世界、UI或线程绑定资源，应显式把收尾安排到正确线程，同时避免该线程join一个还在等待它执行收尾的任务。

### 3.9 取消请求不等于完成确认

`Cancel/StopRequested`表示不再需要结果或请求停止；`Stopped/Joined/Completed`才可能建立安全回收所需的事实，具体以API合同为准。一个取消标志不会中断所有阻塞IO，不会自动移除已排队回调，也不会让捕获的引用突然安全。

```text
停止接收新任务 → 撤销旧结果接纳资格 → 请求协作停止/唤醒等待
→ 等待已定义的完成屏障，或转移到独立任务状态继续存活
→ 在正确线程释放资源
```

`std::thread`对象析构时若仍joinable，会按[标准合同](https://eel.is/c%2B%2Bdraft/thread.thread.destr)终止程序；线程函数自然返回也不会自动把句柄变为非joinable。RAII join守卫可以防忘记汇合，却不保证等待时间上界。不能从工作线程join自身，不能持有工作线程退出所需的锁去join，也不能在GameThread等一个依赖GameThread continuation的任务。

C++20的jthread/stop_token可简化部分样板，仍是协作停止，不强杀工作；本页可运行例子只用C++11。队列的结果接纳字段已在[AI预算主文](../../游戏服务端/06-世界模拟与运行时/11-AI与寻路时间预算.md)定义，这里只负责所有权与停止确认，避免复制第二份协议。实际线程池drain/cancel语义继续看[线程同步与锁](../04-C++并发与内存模型/02-线程同步与锁.md)。

## 4. 示例

### 4.1 最小 ScopeGuard（C++11，可编译运行）

这个教学守卫要求清理函数与其对象析构不抛，显式禁用复制/赋值/移动；没有实现工厂或复杂转移语义。先由安全资源包装接管获取失败路径，不能假设构造本守卫之前发生的失败也已被保护。

```cpp
#include <cassert>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <iostream>

template <typename F>
class ScopeGuard {
    F fn_;
    bool active_;
public:
    explicit ScopeGuard(F fn) noexcept(std::is_nothrow_move_constructible<F>::value)
        : fn_(std::move(fn)), active_(true) {
        static_assert(noexcept(std::declval<F&>()()), "cleanup must be noexcept");
        static_assert(std::is_nothrow_destructible<F>::value,
                      "cleanup object destruction must not throw");
    }
    ~ScopeGuard() noexcept { if (active_) fn_(); }
    void dismiss() noexcept { active_=false; }
    ScopeGuard(const ScopeGuard&)=delete;
    ScopeGuard& operator=(const ScopeGuard&)=delete;
    ScopeGuard(ScopeGuard&&)=delete;
    ScopeGuard& operator=(ScopeGuard&&)=delete;
};
int main() {
    int releases=0;
    auto cleanup=[&releases]() noexcept { ++releases; };
    typedef ScopeGuard<decltype(cleanup)> Guard; // C++11，无CTAD
    static_assert(!std::is_copy_constructible<Guard>::value, "no copy");
    static_assert(!std::is_copy_assignable<Guard>::value, "no assignment");
    { Guard guard(cleanup); }
    assert(releases==1);
    { Guard guard(cleanup); guard.dismiss(); }
    assert(releases==1);
    try { Guard guard(cleanup); throw std::runtime_error("injected"); }
    catch (const std::runtime_error&) {}
    assert(releases==2);
    std::cout << "PASS: 3 scope-guard assertions\n";
}
```

运行：`g++ -std=c++11 -Wall -Wextra -Werror -pedantic scope_guard.cpp -o scope_guard && ./scope_guard`。C++17才有的类模板实参推导（CTAD）不用于本例；C++11写明`ScopeGuard<decltype(cleanup)>`。捕获引用必须比guard活得久；noexcept合同违规会终止，不是错误恢复机制。

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
    ~TransactionGuard() noexcept {
        if (!committed_) db_.RollbackNoThrow(); // 真实数据库适配层必须提供不抛兜底
    }
    void commit() { db_.Commit(); committed_ = true; }
    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;
};

void grant_item(Db& db, uint64 player, uint64 item) {
    TransactionGuard tx(db);
    db.Execute("INSERT ...");      // 中途抛异常 → 析构回滚
    db.Execute("UPDATE ...");
    tx.commit();                    // 全部成功 → 提交
}
```

这是调用结构示意，不是分布式事务或数据库提交成功的证明。若Commit已在服务端生效但响应丢失，客户端异常并不等于未提交；RollbackNoThrow也不能撤销已提交业务。需查询/幂等与数据库合同，不能仅凭RAII宣称所有外部副作用获得强异常保证。

### 4.5 停止确认与独立状态（C++11，可编译运行）

下面是**生命周期同步实验**，不是线程池或生产任务系统。拥有者在创建它的控制线程使用/销毁，工作线程只捕获独立state。测试用条件变量确认启动，不靠sleep猜时序。

```cpp
#include <cassert>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <iostream>

struct TaskState {
    std::mutex mutex;
    std::condition_variable changed;
    bool started=false, stop=false, exited=false;
};
class JoiningTask {
    std::shared_ptr<TaskState> state_;
    std::thread worker_;
public:
    JoiningTask() : state_(std::make_shared<TaskState>()) {
        const std::shared_ptr<TaskState> state=state_;
        worker_=std::thread([state] { // C++11按值捕获；没有裸this
            std::unique_lock<std::mutex> lock(state->mutex);
            state->started=true;
            state->changed.notify_all();
            state->changed.wait(lock,[state] { return state->stop; });
            state->exited=true; // 实际工作还要处理异常/IO/资源释放合同
        });
    }
    void waitStarted() {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->changed.wait(lock,[this] { return state_->started; });
        // 此谓词只在同步成员调用期间使用，不排队到异步任务。
    }
    void requestStop() {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->stop=true;
        }
        state_->changed.notify_all();
    }
    std::shared_ptr<TaskState> observe() const { return state_; }
    ~JoiningTask() noexcept {
        requestStop();
        if (worker_.joinable()) worker_.join(); // 不持有state mutex，禁止自join
    }
    JoiningTask(const JoiningTask&)=delete;
    JoiningTask& operator=(const JoiningTask&)=delete;
    JoiningTask(JoiningTask&&)=delete;
    JoiningTask& operator=(JoiningTask&&)=delete;
};
int main() {
    std::shared_ptr<TaskState> observer;
    std::weak_ptr<TaskState> weak;
    {
        JoiningTask task;
        observer=task.observe(); weak=observer;
        task.waitStarted();
        {
            std::lock_guard<std::mutex> lock(observer->mutex);
            assert(observer->started);
            assert(!observer->exited); // 还没请求stop，工作线程正在等待
        }
        task.requestStop(); // 只提出请求；此处不声称已经退出
    } // 析构请求停止并join；以下读取发生在完成屏障之后
    assert(observer->exited);
    assert(!weak.expired()); // 独立状态仍由测试观察者持有
    observer.reset();
    assert(weak.expired()); // 无隐藏强引用环或遗留线程状态
    std::cout << "PASS: 5 joining-ownership assertions\n";
}
```

```bash
g++ -std=c++11 -pthread -Wall -Wextra -Werror -pedantic joining_ownership.cpp -o joining_ownership
./joining_ownership
g++ -std=c++11 -pthread -Wall -Wextra -Werror -pedantic -fsanitize=undefined -fno-sanitize-recover=all joining_ownership.cpp -o joining_ownership_ubsan
./joining_ownership_ubsan
```

前提：拥有者成员函数/析构不被多线程并发调用，也不会在工作线程销毁；测试中的mutex/cv/join正常工作。代码没有捕获并传播工作异常，没有一般IO取消，没有超时join。系统同步调用若抛出且逃出线程函数或noexcept析构会终止，生产实现应另定故障政策。`exited`在join后读取安全，不代表任意线程可随时无锁读取该字段；UBSan通过也不是数据竞争不存在的证明，ThreadSanitizer/目标调度压力验证另做。

## 5. 最佳实践

1. **资源一律 RAII**：锁、文件、套接字、句柄、GPU 资源、数据库连接；禁止裸 `new`/`delete` 配对出现在业务代码。
2. **释放兜底不让异常逃逸**；不要假设默认规格永远noexcept。重要失败用显式close/commit返回给调用方，析构记录失败不能冒充提交成功。
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
语言允许特定析构为noexcept(false)，但资源守卫应避免让异常逃逸；默认规格依赖子对象等规则。noexcept中逃逸或异常展开期间又逃逸异常会终止。需要报告的释放/提交失败用显式操作处理，不能统一吞错后视为成功。

**Q2：为什么 `shared_ptr` 循环引用会泄漏？**
两个 `shared_ptr` 互持时引用计数永远 ≥1，控制块永不归零。用 `weak_ptr` 观察一侧即可破环。

**Q3：`unique_ptr` 能拷贝吗？**
不能（拷贝构造被删除），只能移动；这保证单一所有权。需要多个持有者时换 `shared_ptr`。

**Q4：UE 的 `UObject` 为什么不能 `delete`？**
因为引擎的 GC 与反射系统统一管理 UObject 分配与回收，`delete` 会造成双重释放或 GC 悬垂。销毁依对象类别采用引擎公开生命周期接口（如Actor的Destroy），并管理GC可达引用；不要把MarkAsGarbage/ConditionalBeginDestroy当所有玩法对象的通用销毁入口。

**Q5：`TWeakObjectPtr` 和 `std::weak_ptr` 一样吗？**
语义类似（观察不拥有），机制不同：前者靠 GC 的 `IsValid()` 判断对象是否已被回收，后者靠控制块强/弱计数。

**Q6：局部静态对象的析构顺序和全局对象有关吗？**
局部静态在首次执行时构造、按构造逆序析构，且与全局静态析构顺序交错；跨翻译单元动态初始化顺序有复杂的排序/延迟规则，不应依赖未经证明的相对先后；这不等于所有初始化都触发未定义行为。

**Q7：为什么基类析构要 `virtual`？**
在本篇C++11普通delete场景，通过基类指针删除派生对象而基类析构非虚是未定义行为，不只是“少调用派生析构”。按[C.35](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rc-dtor-virtual)，允许经基类销毁时使用public virtual析构；禁止这种销毁时可用protected non-virtual析构。不是所有允许派生的类都必须虚析构。

**Q8：局部静态对象初始化线程安全吗？**
C++11 起"magic statics"保证局部静态的首次初始化是线程安全的（编译器生成 guard）。这只保护首次初始化，不保护初始化后的成员访问；跨翻译单元动态初始化依赖仍需单独设计。

**Q9：UE 的 `TSharedPtr` 与 `std::shared_ptr` 线程安全一样吗？**
不应一概而论。UE [ESPMode](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/ESPMode?lang=en-US)区分NotThreadSafe与ThreadSafe，跨线程共享须选合适模式；不要假定所有TSharedPtr的计数都是线程安全的。标准shared_ptr不同句柄可并发管理同一控制块，但同一句柄的冲突读写仍需同步/对应标准版本的原子接口。两者都不自动保护所指对象字段，也不授予业务写回资格。UObject的弱引用/GC另有合同。

**Q10：为什么析构函数里不能调用虚函数期待派生态？**
析构从派生类向基类逐层执行，进入基类析构时派生部分已销毁；此时虚调用解析到当前类实现，通常无法访问派生成员。需要清理逻辑应在派生类析构里先做。

## 7. 验证与基准

- 标准语义以[C++工作草案析构规则](https://eel.is/c%2B%2Bdraft/class.dtor)与[异常规格](https://eel.is/c%2B%2Bdraft/except.spec)核对；4.1测试正常退出、dismiss、异常展开三条清理路径。现代草案会继续演进，本例只使用C++11已有机制。
- 异常安全保证的工程定义见 [cppreference - Exceptions](https://en.cppreference.com/w/cpp/language/exceptions)。
- UE 部分以本机 UE 5.8 源码与 [UE 5.8 官方文档](https://dev.epicgames.com/documentation/en-us/unreal-engine)（UObject/GC 页面）为准；本层仅建立 C++ 侧边界，深度分析见 12-引擎源码分析。
- 后续升级 L3/L4 计划：为 ScopeGuard 增加异常注入测试（强保证验证），为锁 RAII 增加锁竞争 Benchmark（关联 W1-06）。

验证命令（Windows / MSVC）：

```powershell
cl /nologo /utf-8 /std:c++17 /EHsc scope_guard.cpp /Fe:scope_guard.exe
.\scope_guard.exe   # 预期输出 PASS: 3 scope-guard assertions
```

### 7.1 本次实验与下一层验证

- 2026-10-01：原ScopeGuard节选在GCC14.2 C++17编译报缺少utility；替换例以C++11通过正常释放/dismiss/异常展开3条断言。
- 4.5以真实std::thread/condition_variable验证启动、停止后join、状态所有权与最终释放5条断言；没有依赖sleep或执行线程不受控的detach。
- 两例严格警告编译与UBSan通过；仅支持列出的路径。没有完成UE编译、生产数据库提交失败注入、通用线程池取消或TSan验证。

| 后续故障场景 | 要证明的合同 |
| --- | --- |
| 构造中途失败 | 只有已构造成员自动清理；裸资源先有守卫 |
| 任务排队后会话关闭 | 存活状态与业务写回资格分开，旧回调不得提交 |
| 工作线程正在等待UI/GameThread | 关闭流程不在同一线程同步等待这个依赖 |
| 最后强引用在后台释放 | 线程亲和资源不会在错误线程析构 |
| 数据库提交响应丢失 | 显式unknown结果与幂等查询，不由析构假定回滚成功 |

保持maturity L2和verified空列表；这些实验不把整个生命周期/并发知识域提升为已验证。

## 8. 关联阅读

- [02-Copy-Move与值语义](02-Copy-Move与值语义.md)：特殊成员函数、移动语义与容器扩容（含本机实验）。
- [00-计算机与工程基础](../README.md)：本层规划与门禁。
- [游戏知识/01-引擎基础](../../游戏知识/01-引擎基础/README.md)：UObject/反射/World 生命周期。
- [游戏知识/12-引擎源码分析](../../游戏知识/12-引擎源码分析/README.md)：UObject/GC 源码深度。
- [游戏服务端/01-架构与网络](../../游戏服务端/01-架构与网络/README.md)：服务端连接/会话生命周期的 RAII 实践。

- [Smart Pointers in Unreal Engine](https://dev.epicgames.com/documentation/en-us/unreal-engine/smart-pointers-in-unreal-engine)：可选线程安全模式与普通对象所有权
- [C++异常规格](https://eel.is/c%2B%2Bdraft/except.spec)：析构异常规格的推导与非抛出承诺
- [AI与寻路时间预算](../../游戏服务端/06-世界模拟与运行时/11-AI与寻路时间预算.md)：取消之后的代次、deadline与结果接纳
- [UE多线程回写边界](../../游戏知识/01-引擎基础/11-多线程与任务系统.md)：引擎线程亲和与回调生命周期
