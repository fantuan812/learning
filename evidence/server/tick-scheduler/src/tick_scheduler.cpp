// Original deterministic Tick-policy laboratory, 2026-10-04.
// Old Windows/MSVC output stays untouched. These are model decisions, not CPU timings.
#include "tick_policy.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <locale>
#include <string>
#include <utility>
#include <vector>

using tick_policy::Config;
using tick_policy::ElapsedNs;
using tick_policy::Policy;
using tick_policy::Step;
using tick_policy::Unsigned;
constexpr Unsigned Q = tick_policy::kCreditsPerTick;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Exception, class Function>
void expect_throw(Function function, const std::string& message) {
    bool caught = false;
    try { function(); } catch (const Exception&) { caught = true; }
    require(caught, message);
}
void check_conservation(const Config& config, const Step& step) {
    require(step.raw_ns >= 0 && step.accepted_ns >= 0 && step.clamped_ns >= 0, "negative time account");
    require(step.raw_ns - step.accepted_ns == step.clamped_ns, "clamp account");
    require(step.accepted_ns <= config.max_accepted_delta_ns, "clamp limit");
    require(step.phase_before < Q && step.phase_after < Q, "phase bounds");
    const auto input = tick_policy::detail::checked_add(step.phase_before,
        tick_policy::detail::checked_multiply(static_cast<Unsigned>(step.accepted_ns), config.hz));
    const auto output = tick_policy::detail::checked_add(
        tick_policy::detail::checked_multiply(
            tick_policy::detail::checked_add(step.executed, step.dropped_ticks), Q), step.phase_after);
    require(input == output, "credit conservation");
    require(step.due == step.executed + step.dropped_ticks, "due account");
    require(step.executed <= config.max_ticks_per_advance, "pre-execution cap");
    require(step.extra_ticks == (step.executed ? step.executed - 1 : 0), "extra Tick count");
    require(step.catchup_iteration == (step.executed > 1), "catch iteration count");
    require(step.drop_event == (step.dropped_ticks > 0), "drop event count");
}
struct Rng {
    Unsigned value;
    explicit Rng(Unsigned seed) : value(seed ? seed : 1) {}
    Unsigned next() {
        value ^= value >> 12;
        value ^= value << 25;
        value ^= value >> 27;
        return value * UINT64_C(2685821657736338717); // Defined unsigned fixture arithmetic.
    }
};

int self_test() {
    int passed = 0, failed = 0;
    auto test = [&](const std::string& name, const std::function<void()>& body) {
        try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cout << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    test("units_20_30_60_one_second", [] {
        for (std::uint32_t hz : {20U, 30U, 60U}) {
            Policy policy({hz, 1000, tick_policy::kMaximumDeltaNs});
            const Step step = policy.advance(INT64_C(1000000000));
            require(step.executed == hz && step.phase_after == 0 && step.dropped_ticks == 0, "one second frequency");
        }
    });
    test("threshold_and_one_nanosecond", [] {
        Policy policy({60, 1000, tick_policy::kMaximumDeltaNs});
        const Step first = policy.advance(16666666);
        require(first.executed == 0 && first.phase_after == 999999960, "below threshold");
        const Step second = policy.advance(1);
        require(second.executed == 1 && second.phase_after == 20, "cross threshold without truncation");
    });
    test("zero_elapsed_keeps_phase", [] {
        Policy policy({20, 3, tick_policy::kMaximumDeltaNs});
        policy.advance(12500000);
        const Step step = policy.advance(0);
        require(step.executed == 0 && step.phase_before == 250000000 && step.phase_after == 250000000, "zero input phase");
    });
    test("cap_checked_before_execution", [] {
        Policy policy({60, 3, tick_policy::kMaximumDeltaNs});
        const Step step = policy.advance(200000000);
        require(step.due == 12 && step.executed == 3 && step.dropped_ticks == 9, "cap=3 must not execute 4");
    });
    test("fraction_survives_whole_tick_drop", [] {
        Policy policy({20, 1, tick_policy::kMaximumDeltaNs});
        const Step first = policy.advance(125000000);
        const Step second = policy.advance(25000000);
        require(first.executed == 1 && first.dropped_ticks == 1 && first.phase_after == 500000000, "half Tick kept after drop");
        require(second.executed == 1 && second.dropped_ticks == 0 && second.phase_after == 0, "saved fraction contributes");
    });
    test("extra_ticks_and_catch_iterations_differ", [] {
        Policy policy({60, 10, tick_policy::kMaximumDeltaNs});
        const Step step = policy.advance(50000000);
        require(step.executed == 3 && step.extra_ticks == 2 && step.catchup_iteration && !step.drop_event, "distinct metric denominators");
    });
    test("clamp_and_drop_are_separate", [] {
        Policy policy({30, 3, 100000000});
        const Step step = policy.advance(INT64_C(2000000000));
        require(step.accepted_ns == 100000000 && step.clamped_ns == INT64_C(1900000000) &&
                step.executed == 3 && step.dropped_ticks == 0, "clamp is not overload Tick drop");
        check_conservation(policy.config(), step);
    });
    test("invalid_configuration_rejected", [] {
        for (Config config : std::vector<Config>{{0, 3, 1}, {1001, 3, 1}, {60, 0, 1},
                {60, 1001, 1}, {60, 3, 0}, {60, 3, -1}, {60, 3, INT64_C(60000000001)}})
            expect_throw<std::invalid_argument>([&] { Policy invalid(config); }, "invalid config accepted");
    });
    test("negative_elapsed_rejected_without_state_change", [] {
        Policy policy({30, 3, tick_policy::kMaximumDeltaNs});
        policy.advance(1);
        const auto before = policy.phase();
        expect_throw<std::invalid_argument>([&] { policy.advance(-1); }, "backwards input accepted");
        expect_throw<std::invalid_argument>([&] { policy.advance(std::numeric_limits<ElapsedNs>::min()); }, "signed minimum accepted");
        require(policy.phase() == before, "failed advance changed phase");
    });
    test("int64_max_elapsed_clamped_safely", [] {
        Policy policy({1000, 1000, tick_policy::kMaximumDeltaNs});
        const Step step = policy.advance(std::numeric_limits<ElapsedNs>::max());
        require(step.accepted_ns == INT64_C(60000000000) && step.due == 60000 &&
                step.executed == 1000 && step.dropped_ticks == 59000 && step.phase_after == 0, "large elapsed account");
        check_conservation(policy.config(), step);
    });
    test("checked_arithmetic_boundary_values", [] {
        const auto maximum = std::numeric_limits<Unsigned>::max();
        require(tick_policy::detail::checked_add(maximum, 0) == maximum, "max+0");
        require(tick_policy::detail::checked_multiply(maximum, 1) == maximum, "max*1");
        require(tick_policy::detail::checked_multiply(0, maximum) == 0, "0*max");
        require(tick_policy::detail::checked_add(7, 9) == 16, "ordinary addition");
    });
    test("checked_arithmetic_rejects_before_overflow", [] {
        const auto maximum = std::numeric_limits<Unsigned>::max();
        expect_throw<std::overflow_error>([&] { tick_policy::detail::checked_add(maximum, 1); }, "wrapped sum accepted");
        expect_throw<std::overflow_error>([&] { tick_policy::detail::checked_multiply(maximum, 2); }, "wrapped product accepted");
    });
    test("partition_without_clamp_or_drop", [] {
        Policy whole({60, 1000, tick_policy::kMaximumDeltaNs}), parts = whole;
        const Step one = whole.advance(INT64_C(1000000000));
        Unsigned total = 0;
        for (ElapsedNs delta : {333333333, 333333333, 333333334}) total += parts.advance(delta).executed;
        require(one.executed == 60 && total == 60 && whole.phase() == parts.phase(), "partition drift");
    });
    test("partition_changes_cap_work_not_time_account", [] {
        Policy whole({30, 3, tick_policy::kMaximumDeltaNs}), parts = whole;
        const Step one = whole.advance(INT64_C(1000000000));
        Unsigned executed = 0, dropped = 0;
        for (int i = 0; i < 10; ++i) { const Step step = parts.advance(100000000); executed += step.executed; dropped += step.dropped_ticks; }
        require(one.executed == 3 && one.dropped_ticks == 27 && executed == 30 && dropped == 0, "per-call cap semantics");
        require(one.executed + one.dropped_ticks == executed + dropped && whole.phase() == parts.phase(), "partition account");
    });
    test("partition_can_change_clamped_input", [] {
        Policy whole({30, 1000, 100000000}), parts = whole;
        const Step one = whole.advance(INT64_C(1000000000));
        Unsigned executed = 0;
        for (int i = 0; i < 10; ++i) executed += parts.advance(100000000).executed;
        require(one.executed == 3 && one.clamped_ns == 900000000 && executed == 30, "clamp is per call");
    });
    test("value_copy_is_independent_phase_snapshot", [] {
        Policy original({60, 1000, tick_policy::kMaximumDeltaNs});
        original.advance(1);
        Policy copy = original;
        copy.advance(1);
        require(original.phase() == 60 && copy.phase() == 120, "independent value state");
    });
    test("deterministic_random_conservation", [] {
        Rng random(20261004);
        for (std::uint32_t hz : {1U, 20U, 30U, 60U, 1000U}) {
            Policy policy({hz, 3, 500000000});
            for (int i = 0; i < 2000; ++i) {
                const ElapsedNs delta = static_cast<ElapsedNs>(random.next() % UINT64_C(2000000001));
                const Step step = policy.advance(delta);
                check_conservation(policy.config(), step);
            }
        }
    });
    std::cout << "SELF_TEST passed=" << passed << " failed=" << failed
              << " mode=" << tick_policy::kMutation << '\n';
    return failed ? 1 : 0;
}

struct Scenario { std::string name; Config config; std::vector<ElapsedNs> input; };
std::vector<Scenario> scenarios() {
    std::vector<Scenario> result;
    for (std::uint32_t hz : {20U, 30U, 60U})
        result.push_back({"units_" + std::to_string(hz), {hz, 1000, tick_policy::kMaximumDeltaNs}, {INT64_C(1000000000)}});
    result.push_back({"boundaries_60", {60, 1000, tick_policy::kMaximumDeltaNs}, {0, 16666666, 1, 0, 16666666, 1}});
    result.push_back({"cap3_spike", {60, 3, tick_policy::kMaximumDeltaNs}, {200000000, 0, 16000000, 1000000}});
    result.push_back({"fractional_drop", {20, 1, tick_policy::kMaximumDeltaNs}, {125000000, 25000000, 125000000, 25000000}});
    result.push_back({"clamp_separate", {30, 3, 100000000}, {INT64_C(2000000000), 0, 50000000, 50000000}});
    result.push_back({"whole_second", {60, 1000, tick_policy::kMaximumDeltaNs}, {INT64_C(1000000000)}});
    result.push_back({"partition_second", {60, 1000, tick_policy::kMaximumDeltaNs}, {333333333, 333333333, 333333334}});
    result.push_back({"whole_cap", {30, 3, tick_policy::kMaximumDeltaNs}, {INT64_C(1000000000)}});
    result.push_back({"partition_cap", {30, 3, tick_policy::kMaximumDeltaNs}, std::vector<ElapsedNs>(10, 100000000)});
    result.push_back({"whole_clamp", {30, 1000, 100000000}, {INT64_C(1000000000)}});
    result.push_back({"partition_clamp", {30, 1000, 100000000}, std::vector<ElapsedNs>(10, 100000000)});
    result.push_back({"max_elapsed", {1000, 1000, tick_policy::kMaximumDeltaNs}, {std::numeric_limits<ElapsedNs>::max(), 0, 1}});
    const std::vector<Config> configurations{{30, 3, 500000000}, {60, 10, 100000000},
                                          {20, 1, INT64_C(1000000000)}, {1000, 1000, tick_policy::kMaximumDeltaNs}};
    for (std::size_t index = 0; index < configurations.size(); ++index) {
        Rng random(UINT64_C(20261004) + index);
        Scenario scenario{"random_" + std::to_string(configurations[index].hz), configurations[index], {}};
        for (int i = 0; i < 256; ++i)
            scenario.input.push_back(static_cast<ElapsedNs>(random.next() % UINT64_C(2000000001)));
        result.push_back(std::move(scenario));
    }
    return result;
}
int write_scenarios(const std::string& path) {
    require(std::string(tick_policy::kMutation) == "none", "mutation builds cannot produce evidence scenarios");
    std::ofstream output(path, std::ios::out | std::ios::trunc);
    require(output.good(), "cannot open scenario file");
    output.imbue(std::locale::classic());
    output << "scenario,step,hz,cap,max_delta_ns,raw_ns,accepted_ns,clamped_ns,phase_before,due,executed,extra_ticks,catchup_iteration,dropped_ticks,drop_event,phase_after,conservation\n";
    std::size_t rows = 0;
    for (const auto& scenario : scenarios()) {
        Policy policy(scenario.config);
        for (std::size_t index = 0; index < scenario.input.size(); ++index) {
            const Step step = policy.advance(scenario.input[index]);
            check_conservation(scenario.config, step);
            output << scenario.name << ',' << index << ',' << scenario.config.hz << ','
                << scenario.config.max_ticks_per_advance << ',' << scenario.config.max_accepted_delta_ns << ','
                << step.raw_ns << ',' << step.accepted_ns << ',' << step.clamped_ns << ',' << step.phase_before << ','
                << step.due << ',' << step.executed << ',' << step.extra_ticks << ',' << step.catchup_iteration << ','
                << step.dropped_ticks << ',' << step.drop_event << ',' << step.phase_after << ",1\n";
            ++rows;
        }
    }
    output.flush();
    require(output.good(), "scenario write failed");
    std::cout << "SCENARIOS rows=" << rows << " groups=" << scenarios().size()
              << " type=pure_policy_model_no_cpu_measurement\n";
    return 0;
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") return self_test();
        if (argc == 3 && std::string(argv[1]) == "--scenarios") return write_scenarios(argv[2]);
        throw std::invalid_argument("use --self-test or --scenarios NEW_FILE (use the safe runner for capture)");
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 2;
    }
}
