// Gameplay performance evidence: machine-local instrumentation observations.
// Build: g++ -std=c++17 -O2 -o profiling_overhead profiling_overhead.cpp
// Timing hypotheses never determine the exit code; functional failures do.
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <limits>
#include <filesystem>
#include <system_error>

namespace {

// --------------------------- measurement harness ---------------------------
constexpr double kUnavailable = std::numeric_limits<double>::quiet_NaN();
struct Measurement { double nsPerCall = kUnavailable; bool valid = false; };

bool IsPositiveFinite(double value) { return std::isfinite(value) && value > 0.0; }

template <typename Fn>
Measurement Measure(const char* label, int batchCalls, int rounds, Fn&& fn) {
    if (batchCalls <= 0 || rounds <= 0) return {};
    std::vector<double> perCall;
    perCall.reserve(rounds);
    bool valid = true;
    for (int r = 0; r < rounds; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        fn(batchCalls);
        const auto t1 = std::chrono::steady_clock::now();
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        valid = valid && IsPositiveFinite(ns) && IsPositiveFinite(ns / batchCalls);
        perCall.push_back(ns / batchCalls);
    }
    if (!valid) {
        std::printf("  %-34s INVALID (nonpositive or nonfinite sample)\n", label);
        return {};
    }
    std::sort(perCall.begin(), perCall.end());
    const double p50 = perCall[perCall.size() / 2];
    std::printf("  %-34s p50=%8.2f ns/call\n", label, p50);
    // A median times the number of calls is not the measured total elapsed time.
    return Measurement{p50, true};
}

int gPass = 0;
int gFail = 0;
void Check(bool ok, const char* name, const std::string& detail) {
    if (ok) { ++gPass; std::printf("FUNCTIONAL PASS %-34s %s\n", name, detail.c_str()); }
    else    { ++gFail; std::printf("FUNCTIONAL FAIL %-34s %s\n", name, detail.c_str()); }
}
int FunctionalExitCode() { return gFail == 0 ? 0 : 1; }

std::string Fmt(const char* fmt, ...) {
    char buf[320];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return std::string(buf);
}

// The legacy P1-P7 thresholds are descriptive hypotheses, not portable tests.
enum class Comparison { AtLeast, Greater, Less };
enum class Observation { Supported, Unsupported, Unavailable };
int gSupported = 0;
int gUnsupported = 0;
int gUnavailable = 0;

Observation ClassifyRatio(double numerator, double denominator, double threshold,
                          Comparison comparison) {
    if (!IsPositiveFinite(numerator) || !IsPositiveFinite(denominator) ||
        !IsPositiveFinite(threshold)) return Observation::Unavailable;
    const double ratio = numerator / denominator;
    if (!IsPositiveFinite(ratio)) return Observation::Unavailable;
    const bool supported = comparison == Comparison::AtLeast ? ratio >= threshold :
                           comparison == Comparison::Greater ? ratio > threshold : ratio < threshold;
    return supported ? Observation::Supported : Observation::Unsupported;
}

void ObserveRatio(const char* id, const char* description, double numerator,
                  double denominator, double threshold, Comparison comparison) {
    const Observation observation = ClassifyRatio(numerator, denominator, threshold, comparison);
    const char* status = "UNAVAILABLE";
    if (observation == Observation::Supported) { ++gSupported; status = "SUPPORTED"; }
    else if (observation == Observation::Unsupported) { ++gUnsupported; status = "UNSUPPORTED"; }
    else { ++gUnavailable; }
    const char* op = comparison == Comparison::AtLeast ? ">=" :
                     comparison == Comparison::Greater ? ">" : "<";
    std::printf("OBSERVATION %s %-11s %s; numerator=%.6f denominator=%.6f ratio=%.6f threshold=%s%.6f\n",
                id, status, description, numerator, denominator,
                observation == Observation::Unavailable ? kUnavailable : numerator / denominator,
                op, threshold);
}

// --------------------------- instrumentation styles ------------------------
volatile uint64_t gSink = 0;
std::atomic<uint64_t> gAtomic{0};

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
    constexpr uint64_t kTotalCalls = static_cast<uint64_t>(kBatch) * kRounds;
    constexpr int kCallsPerFrame = 1000;
    constexpr int kHotPathCallsPerFrame = 100000;
    constexpr double kFrameBudgetMs = 16.6;

    std::printf("gameplay-perf | instrumentation overhead benchmark\n");
    std::printf("compiler=%s c++17 (optimization flags are recorded by the runner)\n", __VERSION__);
    std::printf("harness: batch=%d calls, rounds=%d, reported as p50 ns/call\n", kBatch, kRounds);
    std::printf("contract: exit code covers functionality; P1-P7 are machine/build/sample-local observations\n");
    std::printf("--------------------------------------------------------------------------------\n");

    const Measurement base = Measure("1 bare counter increment", kBatch, kRounds, [](int n) {
        for (int i = 0; i < n; ++i) { gCounters[i & (kCounterCount - 1)]++; }
    });
    bool bareCountsValid = true;
    for (int i = 0; i < kCounterCount; ++i) {
        bareCountsValid = bareCountsValid && gCounters[i] == kTotalCalls / kCounterCount;
    }
    Check(bareCountsValid, "F01 bare counter counts", Fmt("expected_per_counter=%llu counters=%d",
          static_cast<unsigned long long>(kTotalCalls / kCounterCount), kCounterCount));

    const Measurement atomicC = Measure("2 atomic relaxed increment", kBatch, kRounds, [](int n) {
        for (int i = 0; i < n; ++i) { gAtomic.fetch_add(1, std::memory_order_relaxed); }
    });
    Check(gAtomic.load(std::memory_order_relaxed) == kTotalCalls, "F02 atomic count",
          Fmt("actual=%llu expected=%llu", static_cast<unsigned long long>(gAtomic.load()),
              static_cast<unsigned long long>(kTotalCalls)));

    const Measurement scoped = Measure("3 scoped steady_clock timer", kBatch, kRounds, [](int n) {
        uint64_t acc = 0;
        for (int i = 0; i < n; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            acc += static_cast<uint64_t>(i & 7);
            const auto t1 = std::chrono::steady_clock::now();
            gSink += std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count() + acc;
        }
    });

    const Measurement lookup = Measure("4 strcmp lookup per call", kBatch, kRounds, [](int n) {
        for (int i = 0; i < n; ++i) {
            const int id = FindCounterByName("gameplay");
            if (id >= 0) gCounters[id]++;
        }
    });
    bool lookupValid = FindCounterByName("missing") == -1;
    for (int i = 0; i < kCounterCount; ++i) {
        lookupValid = lookupValid && FindCounterByName(kCounterNames[i]) == i &&
            gCounters[i] == kTotalCalls / kCounterCount + (i == 4 ? kTotalCalls : 0);
    }
    Check(lookupValid, "F03 lookup mapping and counts", "all names, missing name, and eight counter totals");

    uint64_t formatErrors = 0;
    const Measurement formatOnly = Measure("5 snprintf label per call", kBatch, kRounds, [&](int n) {
        char buf[160];
        for (int i = 0; i < n; ++i) {
            const int len = std::snprintf(buf, sizeof(buf), "tick=%d system=%s value=%.3f", i, "gameplay", i * 0.5);
            if (len < 0 || static_cast<size_t>(len) >= sizeof(buf)) ++formatErrors;
            else gSink += static_cast<unsigned char>(buf[0]);
        }
    });
    Check(formatErrors == 0, "F04 formatting succeeds", Fmt("errors=%llu calls=%llu",
          static_cast<unsigned long long>(formatErrors), static_cast<unsigned long long>(kTotalCalls)));

    // Derive the expected byte count outside the measured file-writing loop.
    uint64_t expectedBytesPerRound = 0;
    bool expectedLengthsValid = true;
    for (int i = 0; i < kBatch; ++i) {
        char buf[160];
        const int len = std::snprintf(buf, sizeof(buf), "tick=%d system=%s value=%.3f\n", i, "gameplay", i * 0.5);
        if (len < 0 || static_cast<size_t>(len) >= sizeof(buf)) expectedLengthsValid = false;
        else expectedBytesPerRound += static_cast<uint64_t>(len);
    }
    const uint64_t expectedBytes = expectedBytesPerRound * kRounds;
    const char* logPath = "build/profiling_overhead_tmp.log";
    std::FILE* logFile = std::fopen(logPath, "wb");
    Measurement fileLog;
    bool fileContractValid = false;
    Check(logFile != nullptr, "F05 file opens", Fmt("path=%s", logPath));
    if (logFile) {
        const bool bufferOk = std::setvbuf(logFile, nullptr, _IOFBF, 1 << 20) == 0;
        Check(bufferOk, "F06 buffering configured", "requested_buffer_bytes=1048576 (implementation may choose actual size)");
        uint64_t writtenBytes = 0;
        uint64_t writeErrors = 0;
        uint64_t fileFormatErrors = 0;
        fileLog = Measure("6 snprintf + fwrite per call", kBatch, kRounds, [&](int n) {
            char buf[160];
            for (int i = 0; i < n; ++i) {
                const int len = std::snprintf(buf, sizeof(buf), "tick=%d system=%s value=%.3f\n", i, "gameplay", i * 0.5);
                if (len < 0 || static_cast<size_t>(len) >= sizeof(buf)) { ++fileFormatErrors; continue; }
                const size_t written = std::fwrite(buf, 1, static_cast<size_t>(len), logFile);
                writtenBytes += written;
                if (written != static_cast<size_t>(len)) ++writeErrors;
            }
        });
        const bool formatOk = expectedLengthsValid && fileFormatErrors == 0;
        Check(formatOk, "F07 file formatting succeeds", Fmt("errors=%llu",
              static_cast<unsigned long long>(fileFormatErrors)));
        const bool writesOk = writeErrors == 0 && std::ferror(logFile) == 0 && writtenBytes == expectedBytes;
        Check(writesOk, "F08 complete writes and byte count", Fmt("actual=%llu expected=%llu short_writes=%llu",
              static_cast<unsigned long long>(writtenBytes), static_cast<unsigned long long>(expectedBytes),
              static_cast<unsigned long long>(writeErrors)));
        const bool flushOk = std::fflush(logFile) == 0 && std::ferror(logFile) == 0;
        Check(flushOk, "F09 file flush succeeds", "fflush and stream error indicator checked");
        const int seekResult = std::fseek(logFile, 0, SEEK_END);
        const long diskBytes = seekResult == 0 ? std::ftell(logFile) : -1;
        const bool sizeOk = diskBytes >= 0 && static_cast<uint64_t>(diskBytes) == expectedBytes;
        Check(sizeOk, "F10 flushed file size", Fmt("actual=%ld expected=%llu", diskBytes,
              static_cast<unsigned long long>(expectedBytes)));
        const bool closeOk = std::fclose(logFile) == 0;
        Check(closeOk, "F11 file close succeeds", "fclose return value checked");
        const bool removeOk = std::remove(logPath) == 0;
        std::error_code existsError;
        const bool remains = std::filesystem::exists(logPath, existsError);
        const bool cleanupOk = removeOk && !existsError && !remains;
        Check(cleanupOk, "F12 temporary file removed", Fmt("path=%s", logPath));
        fileContractValid = bufferOk && formatOk && writesOk && flushOk && sizeOk && closeOk && cleanupOk;
    } else {
        std::printf("FUNCTIONAL UNAVAILABLE F06-F12 file-dependent checks: opening the required file failed\n");
    }

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
    Check(base.valid && atomicC.valid && scoped.valid && lookup.valid && formatOnly.valid &&
          fileLog.valid && sampled.valid, "F13 all measurement samples valid",
          "seven styles, every batch duration finite and positive; p50 is upper median");
    const double perCallFile = fileContractValid ? fileLog.nsPerCall : kUnavailable;

    std::printf("--------------------------------------------------------------------------------\n");
    std::printf("[observations] Original thresholds retained; UNSUPPORTED is a failed legacy hypothesis, not a functional failure\n");
    ObserveRatio("P1", "atomic / bare", atomicC.nsPerCall, base.nsPerCall, 0.5, Comparison::AtLeast);
    ObserveRatio("P2", "scoped / bare", scoped.nsPerCall, base.nsPerCall, 1.0, Comparison::Greater);
    ObserveRatio("P3", "format / scoped", formatOnly.nsPerCall, scoped.nsPerCall, 5.0, Comparison::Greater);
    ObserveRatio("P4", "format+write / format", perCallFile, formatOnly.nsPerCall, 1.0, Comparison::Greater);
    ObserveRatio("P5", "scoped / sampled", scoped.nsPerCall, sampled.nsPerCall, 10.0, Comparison::Greater);

    // Illustrative linear extrapolations, not measured frames or budget gates.
    std::printf("\n[budget] illustrative frame_budget_ms=%.1f; no frame workload measured\n", kFrameBudgetMs);
    struct Row { const char* label; double ns; };
    const Row rows[] = {
        {"bare counter", base.nsPerCall}, {"atomic counter", atomicC.nsPerCall},
        {"scoped timer", scoped.nsPerCall}, {"snprintf label", formatOnly.nsPerCall},
        {"snprintf + fwrite", perCallFile}, {"sampled timer (1/100)", sampled.nsPerCall},
    };
    for (int calls : {kCallsPerFrame, kHotPathCallsPerFrame}) {
        std::printf("[budget] calls_per_frame=%d\n", calls);
        for (const Row& r : rows) {
            if (!IsPositiveFinite(r.ns)) {
                std::printf("[budget] %-22s UNAVAILABLE\n", r.label);
                continue;
            }
            const double ms = r.ns * calls / 1e6;
            std::printf("[budget] %-22s %8.3f ms/frame  %6.2f%% of budget\n", r.label, ms,
                        100.0 * ms / kFrameBudgetMs);
        }
    }
    const double logMs = perCallFile * kHotPathCallsPerFrame / 1e6;
    ObserveRatio("P6", "log_ms / budget_ms at 100000 calls/frame", logMs, kFrameBudgetMs, 1.0, Comparison::Greater);
    const double timerMs = scoped.nsPerCall * kHotPathCallsPerFrame / 1e6;
    ObserveRatio("P7", "timer_ms / budget_ms at 100000 calls/frame", timerMs, kFrameBudgetMs, 1.0, Comparison::Less);

    std::printf("--------------------------------------------------------------------------------\n");
    std::printf("OBSERVATION_RESULT supported=%d unsupported=%d unavailable=%d\n", gSupported, gUnsupported, gUnavailable);
    std::printf("FUNCTIONAL_RESULT pass=%d fail=%d\n", gPass, gFail);
    return FunctionalExitCode();
}
