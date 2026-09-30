// Gameplay performance evidence: profiling overhead of common instrumentation styles.
// Answers "why does logging everything in a hot gameplay path destroy the frame budget".
// Build: g++ -std=c++17 -O2 -o profiling_overhead.exe profiling_overhead.cpp
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <algorithm>

namespace {

// --------------------------- measurement harness ---------------------------
struct Measurement { double nsPerCall; double totalNs; };

template <typename Fn>
Measurement Measure(const char* label, int batchCalls, int rounds, Fn&& fn) {
    std::vector<double> perCall;
    perCall.reserve(rounds);
    for (int r = 0; r < rounds; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        fn(batchCalls);
        const auto t1 = std::chrono::steady_clock::now();
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        perCall.push_back(ns / batchCalls);
    }
    std::sort(perCall.begin(), perCall.end());
    const double p50 = perCall[perCall.size() / 2];
    std::printf("  %-34s p50=%8.2f ns/call\n", label, p50);
    return Measurement{p50, p50 * batchCalls * rounds};
}

int gPass = 0;
int gFail = 0;
void Check(bool ok, const char* name, const std::string& detail) {
    if (ok) { ++gPass; std::printf("PASS  %-58s %s\n", name, detail.c_str()); }
    else    { ++gFail; std::printf("FAIL  %-58s %s\n", name, detail.c_str()); }
}

std::string Fmt(const char* fmt, ...) {
    char buf[320];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return std::string(buf);
}

// --------------------------- instrumentation styles ------------------------
volatile uint64_t gSink = 0;      // defeats the optimiser without costing a barrier
std::atomic<uint64_t> gAtomic{0};

// Pre-registered counter id lookup (the cheap, realistic way).
enum { kCounterCount = 8 };
const char* kCounterNames[kCounterCount] = {"tick", "ai", "physics", "network", "gameplay", "gc", "audio", "ui"};
uint64_t gCounters[kCounterCount] = {0};

int FindCounterByName(const char* name) {
    for (int i = 0; i < kCounterCount; ++i) {
        if (std::strcmp(kCounterNames[i], name) == 0) return i;
    }
    return -1;
}

}  // namespace

int main() {
    constexpr int kBatch = 20000;
    constexpr int kRounds = 200;
    constexpr int kCallsPerFrame = 1000;   // a realistic "log every invocation" call count
    constexpr double kFrameBudgetMs = 16.6;  // 60 fps

    std::printf("gameplay-perf | instrumentation overhead benchmark\n");
    std::printf("compiler=%s c++17 O2\n", __VERSION__);
    std::printf("harness: batch=%d calls, rounds=%d, reported as p50 ns/call\n", kBatch, kRounds);
    std::printf("--------------------------------------------------------------------------------\n");

    // 1) bare counter increment: what a well-written counter should cost
    const Measurement base = Measure("1 bare counter increment", kBatch, kRounds, [](int n) {
        for (int i = 0; i < n; ++i) { gCounters[i & (kCounterCount - 1)]++; }
    });

    // 2) atomic relaxed increment
    const Measurement atomicC = Measure("2 atomic relaxed increment", kBatch, kRounds, [](int n) {
        for (int i = 0; i < n; ++i) { gAtomic.fetch_add(1, std::memory_order_relaxed); }
    });

    // 3) scoped RAII timer (two clock reads per call)
    const Measurement scoped = Measure("3 scoped steady_clock timer", kBatch, kRounds, [](int n) {
        uint64_t acc = 0;
        for (int i = 0; i < n; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            acc += static_cast<uint64_t>(i & 7);
            const auto t1 = std::chrono::steady_clock::now();
            gSink += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() + acc;
        }
    });

    // 4) string-keyed counter lookup per call (naive but very common)
    const Measurement lookup = Measure("4 strcmp lookup per call", kBatch, kRounds, [](int n) {
        for (int i = 0; i < n; ++i) {
            const int id = FindCounterByName("gameplay");
            if (id >= 0) gCounters[id]++;
        }
    });

    // 5) format a log line per call (no I/O yet)
    const Measurement formatOnly = Measure("5 snprintf label per call", kBatch, kRounds, [](int n) {
        char buf[160];
        for (int i = 0; i < n; ++i) {
            std::snprintf(buf, sizeof(buf), "tick=%d system=%s value=%.3f", i, "gameplay", i * 0.5);
            gSink += static_cast<unsigned char>(buf[0]);
        }
    });

    // 6) format + buffered file write per call: the "log everything" anti-pattern
    const char* logPath = "build/profiling_overhead_tmp.log";
    std::FILE* logFile = std::fopen(logPath, "wb");
    double perCallFile = 0.0;
    if (logFile) {
        std::setvbuf(logFile, nullptr, _IOFBF, 1 << 20);
        const Measurement fileLog = Measure("6 snprintf + fwrite per call", kBatch, kRounds, [&](int n) {
            char buf[160];
            for (int i = 0; i < n; ++i) {
                const int len = std::snprintf(buf, sizeof(buf), "tick=%d system=%s value=%.3f\n", i, "gameplay", i * 0.5);
                std::fwrite(buf, 1, static_cast<size_t>(len), logFile);
            }
        });
        perCallFile = fileLog.nsPerCall;
        std::fclose(logFile);
        std::remove(logPath);
    } else {
        std::printf("  %-34s SKIPPED (cannot open %s)\n", "6 snprintf + fwrite per call", logPath);
    }

    // 7) sampled scoped timer: 1 in 100 calls
    const Measurement sampled = Measure("7 sampled timer (1/100 calls)", kBatch, kRounds, [](int n) {
        uint64_t acc = 0;
        for (int i = 0; i < n; ++i) {
            if ((i % 100) == 0) {
                const auto t0 = std::chrono::steady_clock::now();
                acc += static_cast<uint64_t>(i & 7);
                const auto t1 = std::chrono::steady_clock::now();
                gSink += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            } else {
                acc += static_cast<uint64_t>(i & 7);
            }
        }
        gSink += acc;
    });

    std::printf("--------------------------------------------------------------------------------\n");

    // --------------------------- assertions --------------------------------
    Check(atomicC.nsPerCall >= base.nsPerCall * 0.5,
          "P1 atomic increment is not cheaper than a plain counter",
          Fmt("bare=%.2fns atomic=%.2fns", base.nsPerCall, atomicC.nsPerCall));

    Check(scoped.nsPerCall > base.nsPerCall,
          "P2 scoped timer costs more than a counter (two clock reads)",
          Fmt("counter=%.2fns timer=%.2fns ratio=%.1fx", base.nsPerCall, scoped.nsPerCall,
              scoped.nsPerCall / base.nsPerCall));

    Check(formatOnly.nsPerCall > scoped.nsPerCall * 5.0,
          "P3 formatting a log line costs far more than timing it",
          Fmt("timer=%.2fns format=%.2fns ratio=%.1fx", scoped.nsPerCall, formatOnly.nsPerCall,
              formatOnly.nsPerCall / scoped.nsPerCall));

    if (perCallFile > 0.0) {
        Check(perCallFile > formatOnly.nsPerCall,
              "P4 buffered file write adds cost on top of formatting",
              Fmt("format=%.2fns format+write=%.2fns", formatOnly.nsPerCall, perCallFile));
    }

    Check(sampled.nsPerCall < scoped.nsPerCall / 10.0,
          "P5 1/100 sampling is an order of magnitude cheaper than every-call timing",
          Fmt("every-call=%.2fns sampled=%.2fns ratio=%.1fx", scoped.nsPerCall, sampled.nsPerCall,
              scoped.nsPerCall / sampled.nsPerCall));

    // --------------------------- frame budget translation -------------------
    constexpr int kHotPathCallsPerFrame = 100000;  // per-entity logging in a large world
    std::printf("\n[budget] frame_budget_ms=%.1f\n", kFrameBudgetMs);
    struct Row { const char* label; double ns; };
    const Row rows[] = {
        {"bare counter", base.nsPerCall},
        {"atomic counter", atomicC.nsPerCall},
        {"scoped timer", scoped.nsPerCall},
        {"snprintf label", formatOnly.nsPerCall},
        {"snprintf + fwrite", perCallFile},
        {"sampled timer (1/100)", sampled.nsPerCall},
    };
    std::printf("[budget] per-system view, calls_per_frame=%d\n", kCallsPerFrame);
    for (const Row& r : rows) {
        const double ms = r.ns * kCallsPerFrame / 1e6;
        std::printf("[budget] %-22s %8.3f ms/frame  %6.2f%% of budget\n", r.label, ms,
                    100.0 * ms / kFrameBudgetMs);
    }
    std::printf("[budget] hot path view, calls_per_frame=%d\n", kHotPathCallsPerFrame);
    for (const Row& r : rows) {
        const double ms = r.ns * kHotPathCallsPerFrame / 1e6;
        std::printf("[budget] %-22s %8.3f ms/frame  %6.2f%% of budget\n", r.label, ms,
                    100.0 * ms / kFrameBudgetMs);
    }

    const double logMs = perCallFile * kHotPathCallsPerFrame / 1e6;
    Check(logMs > kFrameBudgetMs,
          "P6 per-call logging blows the frame budget on a hot path",
          Fmt("%.3f ms/frame at %d calls vs budget %.1f ms", logMs, kHotPathCallsPerFrame,
              kFrameBudgetMs));

    const double timerMs = scoped.nsPerCall * kHotPathCallsPerFrame / 1e6;
    Check(timerMs < kFrameBudgetMs,
          "P7 scoped timing on the same hot path still fits in the budget",
          Fmt("%.3f ms/frame vs budget %.1f ms", timerMs, kFrameBudgetMs));

    std::printf("--------------------------------------------------------------------------------\n");
    std::printf("RESULT pass=%d fail=%d\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
