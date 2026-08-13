// tick_scheduler.cpp — Server Main Loop / Tick Scheduler 模拟（证据：evidence/server/tick-scheduler）
//
// 模拟问题：
//  1) 固定步长主循环（accumulator 模式）在负载正常/过载时的 Tick 成本分布（P50/P95/P99）；
//  2) 过载时 catch-up（补帧，有上限）与 drop（丢帧）两种策略的效果；
//  3) 实体数量对单 Tick 成本的影响（100 / 1000 / 10000）。
//
// 这是模型实验：工作负载 = 实体数 × 单位成本（带抖动），不绑定具体引擎实现。
//
// 构建：cl /nologo /utf-8 /O2 /std:c++17 /EHsc tick_scheduler.cpp /Fe:tick_scheduler.exe

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

struct Stats {
    double p50 = 0, p95 = 0, p99 = 0;
    long long ticks = 0, frames = 0, drops = 0, catchUps = 0;
};

// 固定步长 accumulator 模拟
// tickUs: 每 Tick 预算（微秒，模拟 60Hz → 约 16667us）
// entities: 实体数；workPerEntityUs: 每实体每 Tick 模拟成本；jitter: 成本抖动
// frameLoad: 帧时间负载系数（1.0 = 正好 60fps 节奏；1.3 = 过载）
// catchUp: true 用补帧策略（最多 maxCatchUp 帧），false 用丢帧策略
static Stats simulate(int entities, double workPerEntityUs, double jitter,
                      double frameLoad, bool catchUp, int maxCatchUp,
                      int frames) {
    const double tickUs = 16666.7;                 // 60Hz tick
    const double frameUs = 16666.7 * frameLoad;    // 每帧实际流逝
    std::mt19937_64 rng(20260812);
    std::uniform_real_distribution<double> jit(1.0 - jitter, 1.0 + jitter);

    Stats st;
    std::vector<double> costs;
    costs.reserve(frames * 2);
    double accumulator = 0.0;

    for (int f = 0; f < frames; ++f) {
        st.frames++;
        accumulator += frameUs;
        int executed = 0;
        while (accumulator >= tickUs && (catchUp || executed == 0)) {
            const double cost = entities * workPerEntityUs * jit(rng);   // 本 Tick 工作量
            costs.push_back(cost);
            st.ticks++;
            accumulator -= tickUs;
            if (++executed > maxCatchUp) {
                // 达到补帧上限：剩余时间直接丢弃（慢帧标记）
                accumulator = 0.0;
                st.drops++;
                break;
            }
        }
        if (!catchUp && accumulator >= tickUs) {   // 丢帧策略：不补，直接清零
            st.drops++;
            accumulator = 0.0;
        }
        if (executed > 1) st.catchUps++;
    }

    std::sort(costs.begin(), costs.end());
    const size_t n = costs.size();
    st.p50 = costs[n / 2];
    st.p95 = costs[(size_t)(n * 0.95)];
    st.p99 = costs[(size_t)(n * 0.99)];
    return st;
}

static void print_row(const char* label, int entities, double work, double jitter,
                      double load, bool catchUp, int maxCatchUp, int frames) {
    Stats s = simulate(entities, work, jitter, load, catchUp, maxCatchUp, frames);
    printf("%-34s | %6lld | %8.1f | %8.1f | %8.1f | %6lld | %6lld | %6lld\n",
           label, s.ticks, s.p50, s.p95, s.p99, s.drops, s.catchUps, s.frames);
}

int main() {
    printf("== Server Main Loop / Tick Scheduler 模拟（60Hz，%d 帧）==\n", 6000);
    printf("说明：Tick 成本 = 实体数 × 单位成本(带抖动) 的模型量；单位 us。\n\n");
    printf("%-34s | %6s | %8s | %8s | %8s | %6s | %6s | %6s\n",
           "场景", "ticks", "P50", "P95", "P99", "drops", "catch", "frames");
    printf("------------------------------------------------------------------------------------------------\n");

    // 1) 实体规模：单位成本 0.5us，负载 1.0，catch-up 上限 3
    print_row("100 entities x0.5us load=1.0", 100, 0.5, 0.3, 1.0, true, 3, 6000);
    print_row("1000 entities x0.5us load=1.0", 1000, 0.5, 0.3, 1.0, true, 3, 6000);
    print_row("10000 entities x0.5us load=1.0", 10000, 0.5, 0.3, 1.0, true, 3, 6000);

    // 2) 过载：10000 实体在 1.0 负载下已接近预算（5000us），1.3 负载导致积压
    print_row("10000 x0.5us load=1.3 catch-up", 10000, 0.5, 0.3, 1.3, true, 3, 6000);
    print_row("10000 x0.5us load=1.3 drop", 10000, 0.5, 0.3, 1.3, false, 3, 6000);

    // 3) 严重过载：20000 实体（预算 10000us，接近 60% 帧预算）
    print_row("20000 x0.5us load=1.5 catch-up", 20000, 0.5, 0.3, 1.5, true, 3, 6000);
    print_row("20000 x0.5us load=1.5 drop", 20000, 0.5, 0.3, 1.5, false, 3, 6000);

    return 0;
}
