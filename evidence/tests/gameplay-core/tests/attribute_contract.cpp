// Independent literal snapshots and intent-ledger checks; no assert/NDEBUG dependency.
#ifndef ATTRIBUTE_SOURCE
#define ATTRIBUTE_SOURCE "../src/attr_modifier_bench.cpp"
#endif
#define ATTRIBUTE_MODEL_ONLY
#include ATTRIBUTE_SOURCE
#include <cstring>
#include <numeric>
#include <utility>

namespace {
int totalChecks = 0;
int totalFailures = 0;
int totalCases = 0;
struct Case {
    const char* name;
    int checks = 0;
    int failures = 0;
    explicit Case(const char* n) : name(n) { ++totalCases; }
    void Check(bool okay) {
        ++checks; ++totalChecks;
        if (!okay) { ++failures; ++totalFailures; }
        std::printf("CHECK %s %d %s\n", name, checks, okay ? "PASS" : "FAIL");
    }
    ~Case() { std::printf("CASE %s checks=%d fail=%d\n", name, checks, failures); }
};
uint64_t Bits(double x) {
    uint64_t b = 0; static_assert(sizeof b == sizeof x, "binary64 size");
    std::memcpy(&b, &x, sizeof b); return b;
}
bool Equal(double actual, double expected) {
    return std::isfinite(actual) && std::isfinite(expected) && actual == expected;
}
// Intentionally a distinct definition type: never reconstructed from the DUT.
struct IntentMod { int operation; double operand; uint64_t identity; };
struct Intent { double base; std::vector<IntentMod> modifiers; };
// Independent valid-input oracle: reverse Override selection and separate reductions.
// Literal expected answers additionally catch an oracle formula error.
double Reference(const Intent& intent) {
    double basis = intent.base;
    const auto winner = std::find_if(intent.modifiers.rbegin(), intent.modifiers.rend(),
        [](const IntentMod& m) { return m.operation == 2; });
    if (winner != intent.modifiers.rend()) basis = winner->operand;
    std::vector<double> adds, factors;
    for (const auto& m : intent.modifiers) {
        if (m.operation == 0) adds.push_back(m.operand);
        if (m.operation == 1) factors.push_back(m.operand);
    }
    const double sum = std::accumulate(adds.begin(), adds.end(), 0.0);
    const double product = std::accumulate(factors.begin(), factors.end(), 1.0,
        [](double a, double b) { return a * b; });
    return (basis + sum) * product;
}
bool SameDefinition(const Attribute& dut, const Intent& intended) {
    if (Bits(dut.base) != Bits(intended.base) || dut.mods.size() != intended.modifiers.size()) return false;
    for (size_t i = 0; i < dut.mods.size(); ++i) {
        const auto& d = dut.mods[i]; const auto& s = intended.modifiers[i];
        if (static_cast<int>(d.op) != s.operation || Bits(d.value) != Bits(s.operand) || d.source != s.identity) return false;
    }
    return true;
}
// Fixtures operate public fields; they do not implement production mutation APIs.
void MarkDirty(Attribute& dut) { dut.dirty = true; }
size_t Flush(std::vector<Attribute>& attributes) {
    size_t attempted = 0;
    for (auto& a : attributes) {
        if (a.dirty) { ++attempted; (void)TryRefresh(a); }
    }
    return attempted;
}
void Current(Case& c, const Attribute& dut, const Intent& intended, double literal) {
    c.Check(Equal(Reference(intended), literal));
    double out = -345.0;
    c.Check(TryReadCurrent(dut, out));
    c.Check(Equal(out, literal));
    c.Check(Equal(dut.cached, Reference(intended)));
    c.Check(!dut.dirty && dut.error == EvalError::kNone);
    c.Check(SameDefinition(dut, intended));
}
void TableCase(const char* name, double base, const std::vector<Modifier>& modifiers,
               const Intent& intended, double literal) {
    Case c(name);
    Attribute dut; dut.base = base; dut.mods = modifiers;
    double out = -345.0;
    c.Check(!TryReadCurrent(dut, out));
    c.Check(out == -345.0);
    const auto evaluated = TryEvaluate(dut);
    c.Check(evaluated.error == EvalError::kNone);
    c.Check(Equal(evaluated.value, literal));
    c.Check(dut.dirty && dut.error == EvalError::kNone && Bits(dut.cached) == Bits(0.0));
    c.Check(TryRefresh(dut) == EvalError::kNone);
    Current(c, dut, intended, literal);
}
void MutationSequence() {
    Case c("mutation_sequence");
    std::vector<Attribute> entities(2);
    Intent intended{100.0, {}};
    c.Check(Flush(entities) == 2);
    Current(c, entities[0], intended, 100.0);
    const uint64_t peerCache = Bits(entities[1].cached);
    auto step = [&](double literal) {
        double out = -999.0;
        c.Check(!TryReadCurrent(entities[0], out) && out == -999.0);
        c.Check(Flush(entities) == 1);
        Current(c, entities[0], intended, literal);
        c.Check(Bits(entities[1].cached) == peerCache && !entities[1].dirty);
    };
    // Intended state and DUT are deliberately updated by different statements.
    intended.base = 120.0;
    entities[0].base = 120.0; MarkDirty(entities[0]);
    step(120.0);
    intended.modifiers.push_back({0, 15.0, 10});
    entities[0].mods.push_back({kAdd, 15.0, 10}); // MUTATION_SITE dropped_modifier
    MarkDirty(entities[0]); step(135.0);
    intended.modifiers.push_back({1, 2.0, 20});
    entities[0].mods.push_back({kMul, 2.0, 20}); MarkDirty(entities[0]); step(270.0);
    intended.modifiers[0].operand = -20.0;
    entities[0].mods[0].value = -20.0; MarkDirty(entities[0]); step(200.0);
    intended.modifiers.erase(intended.modifiers.begin() + 1);
    // Removing by index is harness behavior, not source-based revocation.
    if (entities[0].mods.size() > 1) entities[0].mods.erase(entities[0].mods.begin() + 1);
    MarkDirty(entities[0]); step(100.0);
    intended.modifiers.push_back({2, 50.0, 30});
    entities[0].mods.push_back({kOverride, 50.0, 30}); MarkDirty(entities[0]); step(30.0);
    intended.modifiers[0].operation = 1; intended.modifiers[0].operand = -2.0;
    entities[0].mods[0].op = kMul; entities[0].mods[0].value = -2.0; MarkDirty(entities[0]); step(-100.0);
    intended.modifiers.clear();
    entities[0].mods.clear(); MarkDirty(entities[0]); step(120.0);
    c.Check(Flush(entities) == 0);
}
void OverrideOrder() {
    Case c("override_order");
    Attribute dut; dut.base = 100.0;
    dut.mods = {{kOverride, 10.0, 999}, {kOverride, 20.0, 1}};
    Intent intent{100.0, {{2, 10.0, 999}, {2, 20.0, 1}}};
    auto step = [&](double literal) {
        MarkDirty(dut); c.Check(TryRefresh(dut) == EvalError::kNone); Current(c, dut, intent, literal);
    };
    step(20.0);
    intent.modifiers[0].operand = 30.0; dut.mods[0].value = 30.0; step(20.0);
    intent.modifiers.pop_back(); dut.mods.pop_back(); step(30.0);
    intent.modifiers.push_back({2, 20.0, 1}); dut.mods.push_back({kOverride, 20.0, 1}); step(20.0);
    auto firstIntent = intent.modifiers.front(); intent.modifiers.erase(intent.modifiers.begin()); intent.modifiers.push_back(firstIntent);
    auto firstDut = dut.mods.front(); dut.mods.erase(dut.mods.begin()); dut.mods.push_back(firstDut); step(30.0);
    intent.modifiers.back().identity = 0; dut.mods.back().source = 0; step(30.0);
    std::reverse(intent.modifiers.begin(), intent.modifiers.end()); std::reverse(dut.mods.begin(), dut.mods.end()); step(20.0);
}
void Coalescing() {
    Case c("coalescing_and_barrier");
    std::vector<Attribute> entities(3); Intent intended{100.0, {}};
    c.Check(Flush(entities) == 3);
    const uint64_t old = Bits(entities[0].cached);
    intended.base = 130.0; entities[0].base = 130.0; MarkDirty(entities[0]);
    intended.base = 140.0; entities[0].base = 140.0; MarkDirty(entities[0]);
    c.Check(Bits(entities[0].cached) == old);
    double out = 901.0; c.Check(!TryReadCurrent(entities[0], out) && out == 901.0);
    c.Check(Flush(entities) == 1);
    Current(c, entities[0], intended, 140.0);
    c.Check(Flush(entities) == 0);
    c.Check(entities[1].cached == 100.0 && entities[2].cached == 100.0);
}
void RejectCase(const char* name, double badBase, const std::vector<Modifier>& badMods,
                const Intent& intended, EvalError error) {
    Case c(name); Attribute dut;
    c.Check(TryRefresh(dut) == EvalError::kNone);
    const uint64_t old = Bits(dut.cached);
    dut.base = badBase; dut.mods = badMods; MarkDirty(dut);
    const auto evaluated = TryEvaluate(dut);
    c.Check(evaluated.error == error);
    c.Check(Bits(evaluated.value) == Bits(0.0));
    c.Check(dut.dirty && dut.error == EvalError::kNone && Bits(dut.cached) == old);
    c.Check(TryRefresh(dut) == error);
    c.Check(dut.dirty);
    c.Check(dut.error == error);
    c.Check(Bits(dut.cached) == old);
    c.Check(SameDefinition(dut, intended)); // No rollback of already-mutated source fields.
    double out = 4321.0;
    c.Check(!TryReadCurrent(dut, out));
    c.Check(out == 4321.0);
    dut.base = 100.0; dut.mods.clear(); MarkDirty(dut);
    c.Check(!TryReadCurrent(dut, out));
    c.Check(TryRefresh(dut) == EvalError::kNone);
    c.Check(TryReadCurrent(dut, out) && Equal(out, 100.0));
    c.Check(!dut.dirty && dut.error == EvalError::kNone);
}
void PerAttributeFailure() {
    Case c("per_attribute_failure");
    std::vector<Attribute> entities(3); c.Check(Flush(entities) == 3);
    entities[0].base = 110.0; MarkDirty(entities[0]);
    entities[1].base = std::numeric_limits<double>::quiet_NaN(); MarkDirty(entities[1]);
    entities[2].base = 130.0; MarkDirty(entities[2]);
    c.Check(Flush(entities) == 3);
    Current(c, entities[0], {110.0, {}}, 110.0);
    Current(c, entities[2], {130.0, {}}, 130.0);
    c.Check(entities[1].dirty && entities[1].error == EvalError::kNonfiniteInput);
    c.Check(entities[1].cached == 100.0 && std::isnan(entities[1].base));
    double out = 567.0; c.Check(!TryReadCurrent(entities[1], out) && out == 567.0);
    c.Check(Flush(entities) == 1);
    entities[1].base = 120.0; MarkDirty(entities[1]); c.Check(Flush(entities) == 1);
    Current(c, entities[1], {120.0, {}}, 120.0);
    c.Check(Flush(entities) == 0);
}
void ExplicitRefreshFailure() {
    Case c("explicit_refresh_failure"); Attribute dut;
    c.Check(TryRefresh(dut) == EvalError::kNone);
    // Even a direct explicit refresh of invalid input with dirty incorrectly false
    // must fail closed. Normal dirty scans still require caller marking.
    dut.base = std::numeric_limits<double>::infinity();
    c.Check(TryRefresh(dut) == EvalError::kNonfiniteInput);
    c.Check(dut.dirty && dut.cached == 100.0 && dut.error == EvalError::kNonfiniteInput);
    double out = 543.0; c.Check(!TryReadCurrent(dut, out) && out == 543.0);
    dut.dirty = false; // Inconsistent state: error alone must also deny current reads.
    c.Check(!TryReadCurrent(dut, out) && out == 543.0);
}
} // namespace

int main(int argc, char**) {
    if (argc != 1) { std::fputs("attribute contract accepts no arguments\n", stderr); return 2; }
    std::puts("ATTRIBUTE_CONTRACT version=1");
    TableCase("base", 100, {}, {100, {}}, 100);
    TableCase("category_order", 100, {{kAdd,10,1},{kMul,2,2},{kOverride,50,3}},
        {100,{{0,10,1},{1,2,2},{2,50,3}}},120);
    TableCase("duplicate_source",100,{{kAdd,10,7},{kAdd,20,7}}, {100,{{0,10,7},{0,20,7}}},130);
    TableCase("signed_negative",-100,{{kAdd,-20,1},{kMul,2,2}}, {-100,{{0,-20,1},{1,2,2}}},-240);
    TableCase("negative_mul",100,{{kMul,-2,1}}, {100,{{1,-2,1}}},-200);
    TableCase("zero_mul",100,{{kMul,0,1}}, {100,{{1,0,1}}},0);
    TableCase("add_order_forward",0,{{kAdd,1e16,1},{kAdd,-1e16,2},{kAdd,1,3}}, {0,{{0,1e16,1},{0,-1e16,2},{0,1,3}}},1);
    TableCase("add_order_reverse",0,{{kAdd,1,3},{kAdd,-1e16,2},{kAdd,1e16,1}}, {0,{{0,1,3},{0,-1e16,2},{0,1e16,1}}},0);
    const double tiny=std::numeric_limits<double>::denorm_min();
    TableCase("underflow_allowed",tiny,{{kMul,0.5,1}}, {tiny,{{1,0.5,1}}},0);
    TableCase("mul_order_forward",1,{{kMul,tiny,1},{kMul,0.5,2},{kMul,2,3}}, {1,{{1,tiny,1},{1,0.5,2},{1,2,3}}},0);
    TableCase("mul_order_reverse",1,{{kMul,2,3},{kMul,0.5,2},{kMul,tiny,1}}, {1,{{1,2,3},{1,0.5,2},{1,tiny,1}}},tiny);
    MutationSequence(); OverrideOrder(); Coalescing(); PerAttributeFailure(); ExplicitRefreshFailure();
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    const double max=std::numeric_limits<double>::max();
    struct BadValue { const char* label; double value; };
    for (const auto& item : std::vector<BadValue>{{"nan",nan},{"posinf",inf},{"neginf",-inf}}) {
        const std::string name=item.label;
        RejectCase(("base_"+name).c_str(),item.value,{}, {item.value,{}},EvalError::kNonfiniteInput);
        RejectCase(("add_"+name).c_str(),100,{{kAdd,item.value,1}}, {100,{{0,item.value,1}}},EvalError::kNonfiniteInput);
        RejectCase(("mul_"+name).c_str(),100,{{kMul,item.value,1}}, {100,{{1,item.value,1}}},EvalError::kNonfiniteInput);
        RejectCase(("override_"+name).c_str(),100,{{kOverride,item.value,1}}, {100,{{2,item.value,1}}},EvalError::kNonfiniteInput);
    }
    RejectCase("masked_base",nan,{{kOverride,10,1}},{nan,{{2,10,1}}},EvalError::kNonfiniteInput);
    RejectCase("masked_override",100,{{kOverride,nan,1},{kOverride,10,2}},{100,{{2,nan,1},{2,10,2}}},EvalError::kNonfiniteInput);
    RejectCase("zero_masks_add",100,{{kAdd,inf,1},{kMul,0,2}},{100,{{0,inf,1},{1,0,2}}},EvalError::kNonfiniteInput);
    RejectCase("invalid_op",100,{{static_cast<ModOp>(3),7,1}},{100,{{3,7,1}}},EvalError::kInvalidOp);
    RejectCase("negative_op",100,{{static_cast<ModOp>(-1),7,1}},{100,{{-1,7,1}}},EvalError::kInvalidOp);
    RejectCase("masked_invalid_op",100,{{static_cast<ModOp>(3),7,1},{kOverride,10,2}},{100,{{3,7,1},{2,10,2}}},EvalError::kInvalidOp);
    RejectCase("sum_overflow",0,{{kAdd,max,1},{kAdd,max,2}},{0,{{0,max,1},{0,max,2}}},EvalError::kRange);
    RejectCase("sum_overflow_cancels",0,{{kAdd,max,1},{kAdd,max,2},{kAdd,-max,3}},{0,{{0,max,1},{0,max,2},{0,-max,3}}},EvalError::kRange);
    RejectCase("product_overflow",1,{{kMul,max,1},{kMul,2,2}},{1,{{1,max,1},{1,2,2}}},EvalError::kRange);
    RejectCase("product_overflow_zero",1,{{kMul,max,1},{kMul,2,2},{kMul,0,3}},{1,{{1,max,1},{1,2,2},{1,0,3}}},EvalError::kRange);
    RejectCase("final_add_overflow",max,{{kAdd,max,1},{kMul,0,2}},{max,{{0,max,1},{1,0,2}}},EvalError::kRange);
    RejectCase("final_mul_overflow",max,{{kMul,2,1}},{max,{{1,2,1}}},EvalError::kRange);
    RejectCase("negative_overflow",-max,{{kAdd,-max,1}},{-max,{{0,-max,1}}},EvalError::kRange);
    std::printf("RESULT version=1 cases=%d checks=%d fail=%d\n",totalCases,totalChecks,totalFailures);
    return totalFailures ? 1 : 0;
}
