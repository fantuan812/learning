// atomic_memory_order.cpp — Atomic 与 C++ 内存模型实验（证据：evidence/labs/atomic-memory-order）
//
// 实验问题：
//  1) std::atomic 的 RMW（fetch_add）在多线程下是否正确累计（无锁正确性）？
//  2) 普通 int 在多线程 ++ 会发生什么（数据竞争）？
//  3) release/acquire 能否正确传递"数据 + 就绪标志"（message passing）？
//  4) CAS（compare_exchange_weak）无锁计数是否正确？
//  5) relaxed / acq_rel / seq_cst 在 x86 上的开销差异有多大？
//
// 构建：cl /nologo /utf-8 /O2 /std:c++17 /EHsc atomic_memory_order.cpp /Fe:atomic_memory_order.exe

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

static const int kThreads = 8;
static const int kPerThread = 1000000;
static const long long kExpect = (long long)kThreads * kPerThread;

// A) atomic fetch_add：无锁计数正确性
static void test_atomic_counter() {
    std::atomic<long long> counter{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&] {
            for (int j = 0; j < kPerThread; ++j) {
                counter.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : ts) t.join();
    const long long got = counter.load();
    printf("A) atomic fetch_add x%lld: got=%lld expect=%lld -> %s\n",
           kExpect, got, kExpect, got == kExpect ? "PASS" : "FAIL");
}

// B) 普通 int ++：数据竞争（UB），结果不确定
static void test_plain_race() {
    int lost = 0;
    const long long perThread = 5000000;
    const long long expect = (long long)kThreads * perThread;
    for (int round = 1; round <= 5; ++round) {
        long long counter = 0;
        std::vector<std::thread> ts;
        for (int i = 0; i < kThreads; ++i) {
            ts.emplace_back([&] {
                for (long long j = 0; j < perThread; ++j) {
                    ++counter;   // 数据竞争
                }
            });
        }
        for (auto& t : ts) t.join();
        const bool ok = (counter == expect);
        if (!ok) ++lost;
        printf("B) plain ++ x%lld round %d: got=%lld expect=%lld -> %s\n",
               expect, round, counter, expect, ok ? "相等（本轮未丢更新）" : "丢更新");
    }
    printf("B) 5 轮中 %d 轮出现丢更新（数据竞争 = UB，结果不确定，可能因机器/优化而不同）\n", lost);
}

// C) message passing：release 发布数据，acquire 消费数据
static void test_message_passing() {
    const int kRounds = 5000000;
    std::atomic<bool> ready{false};
    std::atomic<bool> ack{false};
    int data = 0;
    bool ok = true;

    std::thread producer([&] {
        for (int i = 1; i <= kRounds; ++i) {
            data = i;                                  // 普通写
            ready.store(true, std::memory_order_release);   // 发布
            while (!ack.load(std::memory_order_acquire)) {} // 等待消费完成
            ack.store(false, std::memory_order_release);
        }
    });
    std::thread consumer([&] {
        for (int i = 1; i <= kRounds; ++i) {
            while (!ready.load(std::memory_order_acquire)) {} // 获取
            if (data != i) { ok = false; break; }
            ready.store(false, std::memory_order_release);
            ack.store(true, std::memory_order_release);
        }
    });
    producer.join();
    consumer.join();
    printf("C) message passing (release/acquire) x%d: %s\n", kRounds, ok ? "PASS" : "FAIL");
}

// D) CAS 无锁计数器
static void test_cas_counter() {
    std::atomic<long long> counter{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&] {
            for (int j = 0; j < kPerThread; ++j) {
                long long cur = counter.load(std::memory_order_relaxed);
                while (!counter.compare_exchange_weak(cur, cur + 1, std::memory_order_relaxed)) {
                }
            }
        });
    }
    for (auto& t : ts) t.join();
    const long long got = counter.load();
    printf("D) CAS loop x%lld: got=%lld expect=%lld -> %s\n",
           kExpect, got, kExpect, got == kExpect ? "PASS" : "FAIL");
}

// E) 内存序开销（x86：RMW 都是 lock 前缀，三种 order 差异极小；多线程竞争是主成本）
static double bench(int threads, std::memory_order mo, int iters) {
    std::atomic<long long> c{0};
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int i = 0; i < threads; ++i) {
        ts.emplace_back([&] {
            for (int j = 0; j < iters; ++j) c.fetch_add(1, mo);
        });
    }
    for (auto& t : ts) t.join();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

static void bench_all() {
    const int iters = 20000000;
    printf("\nE) fetch_add 耗时（ms，iters=%d/线程）\n", iters);
    printf("   threads | relaxed | acq_rel | seq_cst\n");
    for (int threads : {1, 8}) {
        double r = bench(threads, std::memory_order_relaxed, iters);
        double a = bench(threads, std::memory_order_acq_rel, iters);
        double s = bench(threads, std::memory_order_seq_cst, iters);
        printf("   %7d | %7.2f | %7.2f | %7.2f\n", threads, r, a, s);
    }
    printf("   （x86 上三种 order 的 RMW 均编译为 lock 前缀指令，差异主要来自缓存行竞争）\n");
}

int main() {
    printf("== Atomic 与内存模型实验（MSVC /O2 /std:c++17，%d 线程 × %d 次）==\n\n", kThreads, kPerThread);
    test_atomic_counter();
    test_plain_race();
    test_message_passing();
    test_cas_counter();
    bench_all();
    return 0;
}
