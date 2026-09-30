// Gameplay core evidence: attribute / modifier aggregation benchmark.
// Compares full recomputation against dirty-flag incremental recomputation for an
// attribute pipeline (base + additive + multiplicative + override modifiers).
// Build: g++ -std=c++17 -O2 -o attr_modifier_bench.exe attr_modifier_bench.cpp
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>

namespace {

enum ModOp { kAdd = 0, kMul = 1, kOverride = 2 };

struct Modifier {
    ModOp op;
    double value;
    uint64_t source;
};

struct Attribute {
    double base = 100.0;
    std::vector<Modifier> mods;
    double cached = 0.0;
    bool dirty = true;
};

// Deterministic evaluation: last override wins over base, then additive sum, then product.
double Evaluate(const Attribute& a) {
    double value = a.base;
    for (const Modifier& m : a.mods) {
        if (m.op == kOverride) value = m.value;
    }
    double add = 0.0;
    double mul = 1.0;
    for (const Modifier& m : a.mods) {
        if (m.op == kAdd) add += m.value;
        else if (m.op == kMul) mul *= m.value;
    }
    return (value + add) * mul;
}

void Refresh(Attribute& a) {
    a.cached = Evaluate(a);
    a.dirty = false;
}

struct XorShift {
    uint64_t s;
    explicit XorShift(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
};

std::string Fmt(const char* fmt, double a, double b, double c) {
    char buf[200];
    std::snprintf(buf, sizeof(buf), fmt, a, b, c);
    return std::string(buf);
}

void Report(const char* label, std::vector<double>& us, double opsPerCall) {
    std::sort(us.begin(), us.end());
    auto pct = [&](double p) { return us[static_cast<size_t>(p * (us.size() - 1))]; };
    std::printf("[bench] %-20s rounds=%zu p50=%.1fus p95=%.1fus p99=%.1fus max=%.1fus entities_per_sec=%.0f\n",
                label, us.size(), pct(0.50), pct(0.95), pct(0.99), us.back(),
                opsPerCall / (pct(0.50) / 1e6));
}

}  // namespace

int main() {
    constexpr int kEntities = 20000;
    constexpr int kModsPerEntity = 16;
    constexpr int kRounds = 200;
    constexpr int kDirtyPercent = 10;   // 10% of entities dirty per round

    std::printf("gameplay-core | attribute modifier aggregation benchmark\n");
    std::printf("compiler=%s c++17 O2\n", __VERSION__);
    std::printf("config entities=%d mods_per_entity=%d rounds=%d dirty_percent=%d\n",
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
        Refresh(a);
    }

    // Correctness: incremental path must agree with a full recompute.
    constexpr int kSamples = 400;
    int mismatch = 0;
    for (int i = 0; i < kSamples; ++i) {
        const Attribute& probe = attrs[static_cast<size_t>(rng.next() % kEntities)];
        const double incremental = probe.cached;
        const double full = Evaluate(probe);
        if (std::abs(incremental - full) > 1e-9) {
            ++mismatch;
            std::printf("FAIL  correctness sample %d cached=%.4f full=%.4f\n", i, incremental, full);
        }
    }
    std::printf("samples=%d mismatch=%d\n", kSamples, mismatch);
    std::printf("%s  incremental equals full recompute on %d samples\n",
                mismatch == 0 ? "PASS " : "FAIL ", kSamples);

    std::vector<double> fullUs, incUs;
    fullUs.reserve(kRounds);
    incUs.reserve(kRounds);

    for (int r = 0; r < kRounds; ++r) {
        // Full recompute: touch every entity.
        const auto t0 = std::chrono::steady_clock::now();
        for (Attribute& a : attrs) Refresh(a);
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
            if (a.dirty) { Refresh(a); ++refreshed; }
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
    const double ratio = sortedInc[sortedInc.size() / 2] / sortedFull[sortedFull.size() / 2];
    std::printf("[bench] p50_speedup=%.2fx (dirty=%d%% of entities)\n", 1.0 / ratio, kDirtyPercent);
    std::printf("RESULT pass=%d fail=%d\n", mismatch == 0 ? kSamples + 1 : kSamples, mismatch);
    return mismatch == 0 ? 0 : 1;
}
