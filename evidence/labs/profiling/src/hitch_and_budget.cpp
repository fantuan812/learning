// Gameplay performance evidence: hitch detection and per-system tick budget accounting.
// Build: g++ -std=c++17 -O2 -o hitch_and_budget.exe hitch_and_budget.cpp
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

namespace {

struct XorShift {
    uint64_t s;
    explicit XorShift(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
};

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

double Percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * (v.size() - 1))];
}

// ---------------------------------------------------------------------------
// Frame series: baseline jitter plus injected hitches (some consecutive).
// ---------------------------------------------------------------------------
struct FrameSeries {
    std::vector<double> frames;
    std::vector<int> hitchEventStarts;   // index of the first frame of each injected hitch
    std::vector<int> hitchFrames;        // every frame that belongs to an injected hitch
};

FrameSeries MakeSeries(uint64_t seed, bool injectHitches) {
    FrameSeries s;
    XorShift rng(seed);
    constexpr int kFrames = 1800;          // 30 s at 60 fps
    s.frames.resize(kFrames);
    for (int i = 0; i < kFrames; ++i) s.frames[i] = 8.0 + rng.unit() * 2.0 - 1.0;   // 7..9 ms

    if (!injectHitches) return s;

    // 12 hitch events; 3 of them are cascades (2-3 consecutive bad frames).
    const int starts[12] = {120, 260, 400, 540, 700, 860, 1000, 1150, 1300, 1450, 1600, 1720};
    const int lengths[12] = {1, 1, 3, 1, 1, 2, 1, 1, 3, 1, 1, 2};
    for (int e = 0; e < 12; ++e) {
        const int start = starts[e];
        if (start >= kFrames) continue;
        s.hitchEventStarts.push_back(start);
        for (int f = 0; f < lengths[e] && start + f < kFrames; ++f) {
            s.frames[start + f] = 25.0 + rng.unit() * 55.0;   // 25..80 ms
            s.hitchFrames.push_back(start + f);
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// Hitch detector: adaptive threshold + hysteresis so cascades merge into one event.
// ---------------------------------------------------------------------------
struct HitchReport {
    std::vector<int> events;       // frame index where each hitch event starts
    int badFrames = 0;
    double thresholdMs = 0.0;
};

HitchReport DetectHitches(const std::vector<double>& frames, int recoverFrames = 2) {
    HitchReport r;
    // Use the median (robust to outliers) instead of a percentile: the p99 of a series
    // that already contains hitches is itself contaminated by them, which silently
    // pushes the threshold up until real hitches stop being detected.
    std::vector<double> sorted(frames);
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted[sorted.size() / 2];
    r.thresholdMs = std::max(24.0, median * 2.5);   // >= 1.5 frames at 60 fps
    const double recoverThreshold = r.thresholdMs * 0.6;

    bool inHitch = false;
    int goodRun = 0;
    for (size_t i = 0; i < frames.size(); ++i) {
        const double f = frames[i];
        if (f > r.thresholdMs) {
            ++r.badFrames;
            if (!inHitch) { inHitch = true; r.events.push_back(static_cast<int>(i)); }
            goodRun = 0;
        } else if (inHitch) {
            if (f <= recoverThreshold) ++goodRun; else goodRun = 0;
            if (goodRun >= recoverFrames) { inHitch = false; goodRun = 0; }
        }
    }
    return r;
}

void TestHitchDetection() {
    const FrameSeries dirty = MakeSeries(2026, true);
    const FrameSeries clean = MakeSeries(2026, false);

    const HitchReport dirtyR = DetectHitches(dirty.frames);
    const HitchReport cleanR = DetectHitches(clean.frames);

    Check(static_cast<int>(dirtyR.events.size()) == static_cast<int>(dirty.hitchEventStarts.size()),
          "H1 12 injected hitches detected as exactly 12 events (cascades merged)",
          Fmt("events=%d injected=%d badFrames=%d", static_cast<int>(dirtyR.events.size()),
              static_cast<int>(dirty.hitchEventStarts.size()), dirtyR.badFrames));

    Check(dirtyR.badFrames == static_cast<int>(dirty.hitchFrames.size()),
          "H2 hitching frames detected with 100% recall",
          Fmt("detected=%d injected=%d", dirtyR.badFrames, static_cast<int>(dirty.hitchFrames.size())));

    Check(cleanR.events.empty(),
          "H3 jitter-only series yields zero false positives",
          Fmt("events=%d threshold=%.1fms", static_cast<int>(cleanR.events.size()), cleanR.thresholdMs));

    Check(cleanR.thresholdMs >= 20.0 && cleanR.thresholdMs <= 40.0,
          "H4 adaptive threshold stays in a sane band on a clean 60 fps series",
          Fmt("threshold=%.1fms (2.5x median vs 24ms floor)", cleanR.thresholdMs));

    // Event start indices should match the injected starts.
    bool startsMatch = dirtyR.events.size() == dirty.hitchEventStarts.size();
    if (startsMatch) {
        for (size_t i = 0; i < dirtyR.events.size(); ++i) {
            if (dirtyR.events[i] != dirty.hitchEventStarts[i]) { startsMatch = false; break; }
        }
    }
    Check(startsMatch, "H5 each detected event starts on the first bad frame",
          Fmt("firstEvent=%d injected=%d", dirtyR.events.empty() ? -1 : dirtyR.events[0],
              dirty.hitchEventStarts.empty() ? -1 : dirty.hitchEventStarts[0]));
}

// ---------------------------------------------------------------------------
// Tick budget accounting + degradation ladder with hysteresis
// ---------------------------------------------------------------------------
enum { kSystems = 4 };
const char* kSystemNames[kSystems] = {"ai", "physics", "network", "gameplay"};
constexpr double kBudgetsMs[kSystems] = {2.0, 3.0, 2.0, 4.0};

struct TickSample {
    double ms[kSystems];
};

struct BudgetEngine {
    int level = 0;                 // 0 = normal, 3 = most aggressive degradation
    int overRun = 0;               // consecutive over-budget ticks
    int goodRun = 0;               // consecutive within-budget ticks
    int escalations = 0;
    int recoveries = 0;
    int levelChanges = 0;
    static constexpr int kEscalateAfter = 3;   // 3 consecutive bad ticks -> +1 level
    static constexpr int kRecoverAfter = 30;   // 30 consecutive good ticks -> -1 level

    // Returns the index of the offending system, or -1 when every system is inside budget.
    int Step(const TickSample& s) {
        int offender = -1;
        for (int i = 0; i < kSystems; ++i) {
            if (s.ms[i] > kBudgetsMs[i]) { offender = i; break; }   // first offender is deterministic
        }
        if (offender >= 0) {
            ++overRun;
            goodRun = 0;
            if (overRun >= kEscalateAfter && level < 3) {
                ++level;
                ++escalations;
                ++levelChanges;
                overRun = 0;
            }
        } else {
            overRun = 0;
            ++goodRun;
            if (goodRun >= kRecoverAfter && level > 0) {
                --level;
                ++recoveries;
                ++levelChanges;
                goodRun = 0;
            }
        }
        return offender;
    }
};

TickSample MakeTick(double ai, double physics, double net, double gameplay) {
    TickSample s{};
    s.ms[0] = ai; s.ms[1] = physics; s.ms[2] = net; s.ms[3] = gameplay;
    return s;
}

void TestBudgetAttribution() {
    BudgetEngine e;
    const TickSample s = MakeTick(1.0, 5.5, 1.0, 2.0);   // physics over its 3 ms budget
    const int offender = e.Step(s);
    Check(offender == 1, "B1 over-budget system attributed correctly",
          Fmt("offender=%s (expected physics)", offender >= 0 ? kSystemNames[offender] : "none"));
}

void TestEscalation() {
    BudgetEngine e;
    for (int i = 0; i < 9; ++i) e.Step(MakeTick(1.0, 1.0, 1.0, 9.0));   // gameplay over budget
    Check(e.level == 3 && e.escalations == 3,
          "B2 degradation escalates one level per 3 consecutive bad ticks, capped at 3",
          Fmt("level=%d escalations=%d", e.level, e.escalations));
}

void TestRecoveryHysteresis() {
    BudgetEngine e;
    for (int i = 0; i < 9; ++i) e.Step(MakeTick(1.0, 1.0, 1.0, 9.0));   // -> level 3
    const int levelAfterBad = e.level;
    for (int i = 0; i < 29; ++i) e.Step(MakeTick(1.0, 1.0, 1.0, 1.0)); // 29 good ticks
    const int levelBeforeRecover = e.level;
    for (int i = 0; i < 1; ++i) e.Step(MakeTick(1.0, 1.0, 1.0, 1.0));  // 30th good tick
    Check(levelAfterBad == 3 && levelBeforeRecover == 3 && e.level == 2,
          "B3 recovery needs 30 consecutive good ticks (no premature un-degrade)",
          Fmt("afterBad=%d after29Good=%d after30Good=%d", levelAfterBad, levelBeforeRecover, e.level));
}

void TestNoFlapping() {
    BudgetEngine e;
    // Alternating bad/good ticks: without hysteresis this would flap every tick.
    for (int i = 0; i < 400; ++i) {
        if ((i % 2) == 0) e.Step(MakeTick(1.0, 1.0, 1.0, 9.0));
        else              e.Step(MakeTick(1.0, 1.0, 1.0, 1.0));
    }
    Check(e.levelChanges == 0 && e.level == 0,
          "B4 alternating bad/good ticks cause zero level changes (no flapping)",
          Fmt("levelChanges=%d level=%d", e.levelChanges, e.level));
}

void TestSustainedRecovery() {
    BudgetEngine e;
    for (int i = 0; i < 9; ++i) e.Step(MakeTick(1.0, 1.0, 1.0, 9.0));   // level 3
    for (int i = 0; i < 200; ++i) e.Step(MakeTick(1.0, 1.0, 1.0, 1.0)); // sustained good
    Check(e.level == 0 && e.recoveries == 3,
          "B5 sustained good ticks walk the level back to normal one step at a time",
          Fmt("level=%d recoveries=%d", e.level, e.recoveries));
}

// ---------------------------------------------------------------------------
// Accounting overhead benchmark
// ---------------------------------------------------------------------------
void BenchAccounting() {
    constexpr int kTicks = 200000;
    std::vector<double> perTickNs;
    perTickNs.reserve(100);
    BudgetEngine e;
    double sink = 0.0;

    for (int r = 0; r < 100; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int t = 0; t < kTicks / 100; ++t) {
            TickSample s = MakeTick(1.0 + (t & 3) * 0.1, 1.0, 1.0, 1.0);
            e.Step(s);
            for (int i = 0; i < kSystems; ++i) sink += s.ms[i];
        }
        const auto t1 = std::chrono::steady_clock::now();
        perTickNs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / (kTicks / 100));
    }

    std::sort(perTickNs.begin(), perTickNs.end());
    const double p50 = perTickNs[perTickNs.size() / 2];
    std::printf("\n[bench] budget_accounting ticks=%d systems=%d\n", kTicks, kSystems);
    std::printf("[bench] per_tick_ns p50=%.1f p95=%.1f p99=%.1f\n", p50,
                Percentile(perTickNs, 0.95), Percentile(perTickNs, 0.99));
    std::printf("[bench] per_system_ns=%.1f sink=%.0f\n", p50 / kSystems, sink);
}

}  // namespace

int main() {
    std::printf("gameplay-perf | hitch detection and tick budget accounting\n");
    std::printf("compiler=%s c++17 O2\n", __VERSION__);
    std::printf("--------------------------------------------------------------------------------\n");
    TestHitchDetection();
    TestBudgetAttribution();
    TestEscalation();
    TestRecoveryHysteresis();
    TestNoFlapping();
    TestSustainedRecovery();
    BenchAccounting();
    std::printf("--------------------------------------------------------------------------------\n");
    std::printf("RESULT pass=%d fail=%d\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
