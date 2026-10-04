#ifndef LEARNING_TICK_POLICY_HPP
#define LEARNING_TICK_POLICY_HPP

// Original, single-threaded policy model. No wall-clock work or CPU-cost model.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace tick_policy {
using Unsigned = std::uint64_t;
using ElapsedNs = std::int64_t;
static_assert(std::numeric_limits<Unsigned>::digits == 64, "64-bit credits required");
static_assert(std::numeric_limits<ElapsedNs>::digits == 63, "signed 64-bit elapsed required");
constexpr Unsigned kCreditsPerTick = UINT64_C(1000000000);
constexpr ElapsedNs kMaximumDeltaNs = INT64_C(60000000000);

namespace detail {
inline Unsigned checked_add(Unsigned a, Unsigned b) {
    if (b > std::numeric_limits<Unsigned>::max() - a)
        throw std::overflow_error("credit addition overflow");
    return a + b;
}
inline Unsigned checked_multiply(Unsigned a, Unsigned b) {
    if (a != 0 && b > std::numeric_limits<Unsigned>::max() / a)
        throw std::overflow_error("credit multiplication overflow");
    return a * b;
}
} // namespace detail

struct Config {
    std::uint32_t hz = 60;
    std::uint32_t max_ticks_per_advance = 3;
    ElapsedNs max_accepted_delta_ns = INT64_C(1000000000);
};
struct Step {
    ElapsedNs raw_ns = 0, accepted_ns = 0, clamped_ns = 0;
    Unsigned phase_before = 0, phase_after = 0;
    Unsigned due = 0, executed = 0, dropped_ticks = 0, extra_ticks = 0;
    bool catchup_iteration = false, drop_event = false;
};

#if defined(TICK_POLICY_TEST_MUTANT_CAP_AFTER) && defined(TICK_POLICY_TEST_MUTANT_DROP_FRACTION)
#error "Select only one deliberate test mutation"
#endif
#if defined(TICK_POLICY_TEST_MUTANT_CAP_AFTER)
inline constexpr const char* kMutation = "cap_after";
#elif defined(TICK_POLICY_TEST_MUTANT_DROP_FRACTION)
inline constexpr const char* kMutation = "drop_fraction";
#else
inline constexpr const char* kMutation = "none";
#endif

class Policy {
    Config config_;
    Unsigned phase_ = 0; // ns * Hz; always less than one Tick (1e9 credits).
public:
    explicit Policy(Config config) : config_(config) {
        if (config.hz == 0 || config.hz > 1000 ||
            config.max_ticks_per_advance == 0 || config.max_ticks_per_advance > 1000 ||
            config.max_accepted_delta_ns <= 0 || config.max_accepted_delta_ns > kMaximumDeltaNs)
            throw std::invalid_argument("invalid bounded tick-policy configuration");
    }
    const Config& config() const { return config_; }
    Unsigned phase() const { return phase_; }
    // Value copies are independent snapshots: no pointers, iterators, or thread ownership.
    Step advance(ElapsedNs elapsed_ns) {
        if (elapsed_ns < 0)
            throw std::invalid_argument("negative/backwards elapsed time");
        Step result;
        result.raw_ns = elapsed_ns;
        result.accepted_ns = std::min(elapsed_ns, config_.max_accepted_delta_ns);
        result.clamped_ns = elapsed_ns - result.accepted_ns;
        result.phase_before = phase_;
        const Unsigned added = detail::checked_multiply(
            static_cast<Unsigned>(result.accepted_ns), static_cast<Unsigned>(config_.hz));
        const Unsigned credit = detail::checked_add(phase_, added);
        result.due = credit / kCreditsPerTick;
#if defined(TICK_POLICY_TEST_MUTANT_CAP_AFTER)
        // Deliberate negative control: old execute-then-"count > cap" behavior.
        result.executed = std::min(result.due,
            detail::checked_add(static_cast<Unsigned>(config_.max_ticks_per_advance), 1));
#else
        result.executed = std::min(result.due, static_cast<Unsigned>(config_.max_ticks_per_advance));
#endif
        result.dropped_ticks = result.due - result.executed;
#if defined(TICK_POLICY_TEST_MUTANT_DROP_FRACTION)
        // Deliberate negative control: discard sub-Tick phase as well as full debt.
        result.phase_after = result.dropped_ticks > 0 ? 0 : credit % kCreditsPerTick;
#else
        result.phase_after = credit % kCreditsPerTick;
#endif
        result.extra_ticks = result.executed > 0 ? result.executed - 1 : 0;
        result.catchup_iteration = result.executed > 1;
        result.drop_event = result.dropped_ticks > 0;
        phase_ = result.phase_after; // Commit only after all fallible arithmetic succeeds.
        return result;
    }
};
} // namespace tick_policy
#endif
