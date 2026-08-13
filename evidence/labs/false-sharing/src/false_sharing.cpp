// false_sharing.cpp — False Sharing Benchmark（证据：evidence/labs/false-sharing）
//
// 实验问题：
//  1) 多个线程各自累加"自己的槽位"，相邻排列（同缓存行）vs 64 字节对齐（独立缓存行），耗时差多少？
//  2) 真实共享计数（单原子计数器多线程 fetch_add）与 false sharing 的成本对比。
//
// 构建：cl /nologo /utf-8 /O2 /std:c++17 /EHsc false_sharing.cpp /Fe:false_sharing.exe
// 用法：false_sharing.exe [每线程累加次数，默认 20000000] [线程数，默认 4]

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

// volatile 强制每次迭代都执行内存 RMW，防止编译器把循环提升为寄存器累加
// （普通成员会被 /O2 优化掉，从而观察不到缓存行竞争——数据竞争 UB 允许编译器这么做）
struct Aligned { alignas(64) volatile long long v = 0; };   // 每槽独占缓存行
struct Packed  { volatile long long v = 0; };               // 槽位相邻排列（false sharing）

// 原子槽位变体：用 RMW 强制走缓存一致性协议（非原子 volatile 写在受限调度主机上可能观察不到竞争）
struct PackedAtomic  { std::atomic<long long> v{0}; };                 // 4 个相邻原子槽（共享缓存行）
struct AlignedAtomic { alignas(64) std::atomic<long long> v{0}; };     // 每槽独立缓存行

template <typename T>
static double run_padded(int threads, long long iters) {
    std::vector<T> slots(threads);
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int i = 0; i < threads; ++i) {
        ts.emplace_back([&, i] {
            long long v = slots[i].v;
            for (long long j = 0; j < iters; ++j) ++v;   // 局部累加后写回一次？不：直接操作槽位
            slots[i].v = v;
        });
    }
    for (auto& t : ts) t.join();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

template <typename T>
static double run_padded_direct(int threads, long long iters) {
    std::vector<T> slots(threads);
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int i = 0; i < threads; ++i) {
        ts.emplace_back([&, i] {
            for (long long j = 0; j < iters; ++j) ++slots[i].v;   // 直接命中共享缓存行（false sharing 场景）
        });
    }
    for (auto& t : ts) t.join();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static double run_shared_atomic(int threads, long long iters) {
    std::atomic<long long> counter{0};
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int i = 0; i < threads; ++i) {
        ts.emplace_back([&] {
            for (long long j = 0; j < iters; ++j) counter.fetch_add(1, std::memory_order_relaxed);
        });
    }
    for (auto& t : ts) t.join();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

template <typename T>
static double run_atomic_slots(int threads, long long iters) {
    std::vector<T> slots(threads);
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int i = 0; i < threads; ++i) {
        ts.emplace_back([&, i] {
            for (long long j = 0; j < iters; ++j) slots[i].v.fetch_add(1, std::memory_order_relaxed);
        });
    }
    for (auto& t : ts) t.join();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

int main(int argc, char** argv) {
    const int threads = (argc > 2) ? std::atoi(argv[2]) : 4;
    const long long iters = (argc > 1) ? std::atoll(argv[1]) : 20000000;
    const int runs = 3;

    printf("== False Sharing Benchmark（%d 线程 × %lld 次/线程，%d 次取平均）==\n\n", threads, iters, runs);

    for (int r = 1; r <= runs; ++r) {
        double a = run_padded_direct<Aligned>(threads, iters);   // 独立缓存行
        double p = run_padded_direct<Packed>(threads, iters);    // 同缓存行（false sharing）
        double s = run_shared_atomic(threads, iters);            // 真实共享（对照）
        double pa = run_atomic_slots<PackedAtomic>(threads, iters);     // 原子槽：同缓存行
        double aa = run_atomic_slots<AlignedAtomic>(threads, iters);    // 原子槽：独立缓存行
        printf("run %d: aligned=%.2f ms  packed=%.2f ms  shared_atomic=%.2f ms  atomic_packed=%.2f ms  atomic_aligned=%.2f ms\n",
               r, a, p, s, pa, aa);
        printf("        packed/aligned=%.1fx  atomic_packed/atomic_aligned=%.1fx\n", p / a, pa / aa);
    }

    // 局部累加 vs 直接累加：说明"写缓存行"才是成本来源（编译器优化说明）
    printf("\n对照：每线程先局部累加再写回（不产生缓存行竞争）\n");
    for (int r = 1; r <= 2; ++r) {
        double a = run_padded<Aligned>(threads, iters);
        double p = run_padded<Packed>(threads, iters);
        printf("run %d: aligned=%.2f ms  packed=%.2f ms\n", r, a, p);
    }
    return 0;
}
