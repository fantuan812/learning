// Gameplay core evidence: ordered attribute aggregation and explicit cache publication.
// Default main is bounded semantics; --benchmark explicitly opts into historical timing.
// Single-threaded teaching model, not a Buff lifecycle or dependency propagation service.
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <limits>

#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__ > 0)
#error Attribute contracts require finite checks and ordered floating-point evaluation
#endif
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53,
              "This teaching contract requires binary64 double");

namespace {
// A fixed underlying type makes unknown int operations representable and rejectable.
enum ModOp : int { kAdd = 0, kMul = 1, kOverride = 2 };
enum class EvalError { kNone, kNonfiniteInput, kInvalidOp, kRange };

struct Modifier {
    ModOp op;
    double value;
    uint64_t source; // Identity metadata only; no dedup/revoke/order policy.
};

struct Attribute {
    double base = 100.0;
    std::vector<Modifier> mods;
    double cached = 0.0; // Old bytes may survive failure: not a current-value accessor.
    bool dirty = true;
    EvalError error = EvalError::kNone;
};

struct Evaluation {
    EvalError error;
    double value; // Meaningful only on kNone, never an old cache fallback.
};

// Newly selected teaching policy: validate ALL inputs, even ones masked by Override.
// Preserve the original ordered formula and double rounding/underflow behavior.
Evaluation TryEvaluate(const Attribute& a) {
    if (!std::isfinite(a.base)) return {EvalError::kNonfiniteInput, 0.0};
    for (const Modifier& m : a.mods) {
        if (!std::isfinite(m.value)) return {EvalError::kNonfiniteInput, 0.0};
        if (m.op != kAdd && m.op != kMul && m.op != kOverride)
            return {EvalError::kInvalidOp, 0.0};
    }
    double value = a.base;
    for (const Modifier& m : a.mods) {
        if (m.op == kOverride) value = m.value;
    }
    double add = 0.0;
    double mul = 1.0;
    for (const Modifier& m : a.mods) {
        if (m.op == kAdd) {
            add += m.value;
            if (!std::isfinite(add)) return {EvalError::kRange, 0.0};
        } else if (m.op == kMul) {
            mul *= m.value;
            if (!std::isfinite(mul)) return {EvalError::kRange, 0.0};
        }
    }
    const double sum = value + add;
    if (!std::isfinite(sum)) return {EvalError::kRange, 0.0};
    const double result = sum * mul;
    if (!std::isfinite(result)) return {EvalError::kRange, 0.0};
    return {EvalError::kNone, result};
}

// Does not roll back caller-owned base/mods and is not a whole-batch transaction.
EvalError TryRefresh(Attribute& a) {
    const Evaluation candidate = TryEvaluate(a);
    a.error = candidate.error;
    if (candidate.error != EvalError::kNone) {
        a.dirty = true;
        return candidate.error;
    }
    a.cached = candidate.value;
    a.dirty = false;
    return EvalError::kNone;
}

// Caller MUST mark every raw source mutation dirty. This seam cannot discover
// unmarked edits, synchronize threads, or implement dependency/event ordering.
bool TryReadCurrent(const Attribute& a, double& out) {
    if (a.dirty || a.error != EvalError::kNone || !std::isfinite(a.cached)) return false;
    out = a.cached;
    return true;
}

#ifndef ATTRIBUTE_MODEL_ONLY
struct XorShift {
    uint64_t s;
    explicit XorShift(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
};

void Report(const char* label, std::vector<double>& us, double opsPerCall) {
    std::sort(us.begin(), us.end());
    auto pct = [&](double p) { return us[static_cast<size_t>(p * (us.size() - 1))]; };
    std::printf("[bench] %-20s rounds=%zu p50=%.1fus p95=%.1fus p99=%.1fus max=%.1fus entities_per_sec=%.0f\n",
                label, us.size(), pct(0.50), pct(0.95), pct(0.99), us.back(),
                opsPerCall / (pct(0.50) / 1e6));
}

int HistoricalBenchmark() {
    constexpr int kEntities = 20000;
    constexpr int kModsPerEntity = 16;
    constexpr int kRounds = 200;
    constexpr int kDirtyPercent = 10;   // Attempts with replacement; NOT 10% distinct entities

    std::printf("gameplay-core | attribute modifier aggregation benchmark\n");
    std::printf("compiler=%s (actual build flags belong in the run manifest)\n", __VERSION__);
    std::printf("config entities=%d mods_per_entity=%d rounds=%d mutation_attempt_percent=%d\n",
                kEntities, kModsPerEntity, kRounds, kDirtyPercent);

    XorShift rng(20260911ULL);
    std::vector<Attribute> attrs(static_cast<size_t>(kEntities));
    for (Attribute& a : attrs) {
        a.base = 100.0 + rng.unit() * 50.0;
        a.mods.reserve(kModsPerEntity);
        for (int m = 0; m < kModsPerEntity; ++m) {
            const int roll = static_cast<int>(rng.next() % 10);
            if (roll == 0)      a.mods.push_back(Modifier{kOverride, 150.0 + rng.unit() * 100.0, rng.next()});
            else if (roll < 4)  a.mods.push_back(Modifier{kMul, 1.0 + rng.unit() * 0.5, rng.next()});
            else                a.mods.push_back(Modifier{kAdd, rng.unit() * 20.0, rng.next()});
        }
        if (TryRefresh(a) != EvalError::kNone) return 1;
    }

    // This retained timing path is not independent post-mutation validation.
    std::puts("HISTORICAL_TIMING_ONLY: no correctness or current speed claim; full-table dirty scan O(N+kM)");

    std::vector<double> fullUs, incUs;
    fullUs.reserve(kRounds);
    incUs.reserve(kRounds);

    for (int r = 0; r < kRounds; ++r) {
        // Full recompute: touch every entity.
        const auto t0 = std::chrono::steady_clock::now();
        for (Attribute& a : attrs) if (TryRefresh(a) != EvalError::kNone) return 1;
        const auto t1 = std::chrono::steady_clock::now();
        fullUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());

        // Incremental: mutate a slice of modifiers, then refresh only dirty entities.
        const int dirtyCount = kEntities * kDirtyPercent / 100;
        for (int i = 0; i < dirtyCount; ++i) {
            Attribute& a = attrs[static_cast<size_t>(rng.next() % kEntities)];
            Modifier& m = a.mods[static_cast<size_t>(rng.next() % a.mods.size())];
            m.value += (rng.unit() - 0.5) * 0.1;
            a.dirty = true;
        }
        const auto t2 = std::chrono::steady_clock::now();
        int refreshed = 0;
        for (Attribute& a : attrs) {
            if (a.dirty) { if (TryRefresh(a) != EvalError::kNone) return 1; ++refreshed; }
        }
        const auto t3 = std::chrono::steady_clock::now();
        incUs.push_back(std::chrono::duration<double, std::micro>(t3 - t2).count());
        if (refreshed == 0) std::printf("WARN  round %d refreshed no entity\n", r);
    }

    Report("full_recompute", fullUs, kEntities);
    Report("dirty_incremental", incUs, kEntities);

    std::vector<double> sortedFull = fullUs, sortedInc = incUs;
    std::sort(sortedFull.begin(), sortedFull.end());
    std::sort(sortedInc.begin(), sortedInc.end());
    const size_t p50 = static_cast<size_t>(0.50 * (sortedFull.size() - 1));
    const double ratio = sortedInc[p50] / sortedFull[p50];
    std::printf("[bench] p50_speedup=%.2fx (mutation attempts=%d%% of N; replacement sampling)\n", 1.0 / ratio, kDirtyPercent);
    std::puts("BENCHMARK_COMPLETED correctness=not_established");
    return 0;
}

int Examples() {
    Attribute a;
    int checks = 0, failures = 0;
    auto check = [&](bool good) { ++checks; if (!good) { ++failures; std::printf("FAIL example=%d\n", checks); } };
    double out = -1.0;
    check(!TryReadCurrent(a, out) && out == -1.0);
    check(TryRefresh(a) == EvalError::kNone);
    check(TryReadCurrent(a, out) && out == 100.0);
    a.mods.push_back({kAdd, 10.0, 1}); a.dirty = true;
    check(!TryReadCurrent(a, out) && out == 100.0);
    check(TryRefresh(a) == EvalError::kNone);
    check(TryReadCurrent(a, out) && out == 110.0);
    a.base = std::numeric_limits<double>::infinity(); a.dirty = true;
    check(TryRefresh(a) == EvalError::kNonfiniteInput && a.cached == 110.0 && a.dirty);
    check(!TryReadCurrent(a, out) && out == 110.0);
    a.base = 120.0; a.dirty = true;
    check(TryRefresh(a) == EvalError::kNone);
    check(TryReadCurrent(a, out) && out == 130.0);
    std::printf("EXAMPLE_RESULT version=1 checks=%d fail=%d benchmark=off\n", checks, failures);
    return failures ? 1 : 0;
}
#endif
} // namespace

#ifndef ATTRIBUTE_MODEL_ONLY
int main(int argc, char** argv) {
    if (argc == 1) return Examples();
    if (argc == 2 && std::string(argv[1]) == "--benchmark") return HistoricalBenchmark();
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::puts("usage: attr_modifier_bench [--benchmark|--help]; default runs bounded examples");
        return 0;
    }
    std::fputs("usage: attr_modifier_bench [--benchmark|--help]\n", stderr);
    return 2;
}
#endif
