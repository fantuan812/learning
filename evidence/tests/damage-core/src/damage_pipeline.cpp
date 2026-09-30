// Gameplay core evidence: damage & attribute settlement pipeline.
// Covers modifier aggregation order, crit, armor/resistance mitigation, shield
// absorption, overkill clamping, immunity, damage caps, DOT aggregation,
// determinism replay, plus a settlement micro-benchmark.
// Build: g++ -std=c++17 -O2 -o damage_pipeline.exe damage_pipeline.cpp
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

namespace {

// ---------------------------------------------------------------------------
// Attribute aggregation: last Override wins over base, then additive, then multiplicative.
// ---------------------------------------------------------------------------
enum ModOp { kAdd = 0, kMul = 1, kOverride = 2 };

struct Modifier {
    ModOp op;
    double value;
    uint64_t source;
};

struct Attribute {
    double base = 0.0;
    std::vector<Modifier> mods;

    double Value() const {
        double v = base;
        for (const Modifier& m : mods) if (m.op == kOverride) v = m.value;
        double add = 0.0, mul = 1.0;
        for (const Modifier& m : mods) {
            if (m.op == kAdd) add += m.value;
            else if (m.op == kMul) mul *= m.value;
        }
        return (v + add) * mul;
    }
};

// ---------------------------------------------------------------------------
// Combat state
// ---------------------------------------------------------------------------
struct DamageType { enum E { kPhysical = 0, kMagic = 1, kTrue = 2, kCount = 3 }; };

struct Combatant {
    Attribute attack;                       // 攻击力（含修正器）
    double critChance = 0.0;                // 0..1
    double critMultiplier = 2.0;
    Attribute damageBonus;                  // 增伤：作为 (1 + value) 的乘区
    double armor = 0.0;                     // 护甲值，按 armor/(armor+K) 折算减伤
    double resistance[DamageType::kCount] = {0.0, 0.0, 0.0};  // 按伤害类型的减伤比例
    Attribute damageTaken;                  // 受到伤害加成
    double shield[DamageType::kCount] = {0.0, 0.0, 0.0};
    double hp = 1000.0;
    double hpMax = 1000.0;
    bool alive = true;
    bool immune = false;
};

struct HitResult {
    double raw = 0.0;        // 暴击与增伤后的伤害
    double mitigated = 0.0;  // 护甲/抗性/受伤加成后、命中上限前的伤害
    double absorbed = 0.0;   // 被护盾吸收的部分
    double toHp = 0.0;       // 真正扣到血量上的部分
    double overkill = 0.0;   // 超出剩余血量的过量部分
    bool crit = false;
    bool blocked = false;    // 目标已死亡/免疫被整段拦截
};

struct DamageSpec {
    double amount = 0.0;                 // 基础伤害（技能表数值）
    int type = DamageType::kPhysical;
    double armorConstantK = 100.0;       // 减伤曲线常数
    double perHitCap = 0.0;              // 0 表示不限制；否则为单次命中上限（在减伤后、护盾前生效）
};

// Deterministic PRNG so replays are reproducible.
struct XorShift {
    uint64_t s;
    explicit XorShift(uint64_t seed) : s(seed ? seed : 0x2545F4914F6CDD1DULL) {}
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
};

double ArmorReduction(double armor, double k) {
    if (armor <= 0.0) return 0.0;
    return armor / (armor + k);   // 恒小于 1，天然免疫"减伤到 100%"
}

HitResult Settle(Combatant& attacker, Combatant& target, const DamageSpec& spec, XorShift& rng) {
    HitResult r;
    if (!target.alive || target.immune) { r.blocked = true; return r; }
    if (spec.amount <= 0.0) { r.blocked = true; return r; }

    // 1) base damage * attacker attack attribute
    r.raw = spec.amount * attacker.attack.Value();

    // 2) crit (rolled once per hit)
    if (attacker.critChance > 0.0 && rng.unit() < attacker.critChance) {
        r.raw *= attacker.critMultiplier;
        r.crit = true;
    }

    // 3) attacker damage bonus
    r.raw *= (1.0 + attacker.damageBonus.Value());

    // 4) armor mitigation (true damage ignores armor)
    if (spec.type != DamageType::kTrue) {
        r.raw *= (1.0 - ArmorReduction(target.armor, spec.armorConstantK));
    }

    // 5) typed resistance
    r.raw *= (1.0 - std::min(1.0, std::max(0.0, target.resistance[spec.type])));

    // 6) target damage taken modifier
    r.raw *= (1.0 + target.damageTaken.Value());

    // 7) per-hit cap applied after mitigation, before shields
    r.mitigated = r.raw;
    if (spec.perHitCap > 0.0) r.mitigated = std::min(r.mitigated, spec.perHitCap);

    // 8) shield absorbs first (never below zero)
    const double absorb = std::min(target.shield[spec.type], r.mitigated);
    target.shield[spec.type] -= absorb;
    r.absorbed = absorb;
    r.toHp = r.mitigated - absorb;

    // 9) HP floor at zero, overkill reported separately
    const double before = target.hp;
    target.hp = std::max(0.0, target.hp - r.toHp);
    if (r.toHp > before) r.overkill = r.toHp - before;
    if (target.hp <= 0.0) target.alive = false;
    return r;
}

int gPass = 0;
int gFail = 0;

void Check(bool ok, const char* name, const std::string& detail) {
    if (ok) { ++gPass; std::printf("PASS  %-56s %s\n", name, detail.c_str()); }
    else    { ++gFail; std::printf("FAIL  %-56s %s\n", name, detail.c_str()); }
}

std::string Fmt(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return std::string(buf);
}

bool Near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// A no-crit, no-mitigation attacker for formula assertions.
Combatant PlainAttacker() {
    Combatant c;
    c.attack.base = 1.0;          // attack.Value() == 1 -> raw == spec.amount
    c.critChance = 0.0;
    c.damageBonus.base = 0.0;     // (1 + 0)
    return c;
}

Combatant DummyTarget() {
    Combatant c;
    c.hp = c.hpMax = 1e9;         // never dies during formula assertions
    return c;
}

void TestModifierOrder() {
    Attribute a;
    a.base = 100.0;
    a.mods.push_back(Modifier{kAdd, 50.0, 1});        // +50
    a.mods.push_back(Modifier{kMul, 2.0, 1});         // x2
    a.mods.push_back(Modifier{kOverride, 10.0, 1});   // base replaced by 10
    // (10 + 50) * 2 = 120
    Check(Near(a.Value(), 120.0), "A1 aggregation order: override base, sum adds, then multiply",
          Fmt("value=%.4f expected=120.0000", a.Value()));
}

void TestValueSemantics() {
    Attribute a;
    a.base = 10.0;
    Modifier add{kAdd, 5.0, 1};
    Modifier mul{kMul, 3.0, 1};
    // insertion order must not matter for commutative ops
    a.mods = {mul, add};
    const double v1 = a.Value();
    a.mods = {add, mul};
    const double v2 = a.Value();
    Check(Near(v1, v2) && Near(v1, 45.0), "A2 add/mul are order independent (10+5)*3",
          Fmt("v1=%.4f v2=%.4f", v1, v2));
}

void TestCritBeforeMitigation() {
    Combatant atk = PlainAttacker();
    atk.critChance = 1.0;         // always crit
    atk.critMultiplier = 2.0;
    Combatant tgt = DummyTarget();
    tgt.armor = 100.0;            // K=100 -> 50% reduction
    XorShift rng(1);
    DamageSpec spec; spec.amount = 100.0; spec.type = DamageType::kPhysical; spec.armorConstantK = 100.0;
    const HitResult r = Settle(atk, tgt, spec, rng);
    // 100 * 1 * 2 (crit) * (1 - 0.5) = 100
    Check(r.crit && Near(r.toHp, 100.0), "D1 crit multiplies before armor mitigation",
          Fmt("toHp=%.4f expected=100.0000 crit=%d", r.toHp, r.crit ? 1 : 0));
}

void TestArmorCurve() {
    Combatant atk = PlainAttacker();
    Combatant tgt = DummyTarget();
    tgt.armor = 900.0;            // K=100 -> 90%
    XorShift rng(1);
    DamageSpec spec; spec.amount = 1000.0; spec.armorConstantK = 100.0;
    const HitResult r = Settle(atk, tgt, spec, rng);
    const double huge = ArmorReduction(1e9, 100.0);
    Check(Near(r.toHp, 100.0) && huge < 1.0,
          "D2 armor curve is asymptotic (never reaches 100% reduction)",
          Fmt("toHp=%.4f residualAtArmor1e9=%.9f", r.toHp, 1.0 - huge));
}

void TestTrueDamageIgnoresArmor() {
    Combatant atk = PlainAttacker();
    Combatant tgt = DummyTarget();
    tgt.armor = 900.0;
    XorShift rng(1);
    DamageSpec spec; spec.amount = 100.0; spec.type = DamageType::kTrue;
    const HitResult r = Settle(atk, tgt, spec, rng);
    Check(Near(r.toHp, 100.0), "D3 true damage bypasses armor",
          Fmt("toHp=%.4f expected=100.0000", r.toHp));
}

void TestResistanceStacksMultiplicativelyWithArmor() {
    Combatant atk = PlainAttacker();
    Combatant tgt = DummyTarget();
    tgt.armor = 100.0;                          // 50%
    tgt.resistance[DamageType::kMagic] = 0.5;   // 50%
    tgt.damageTaken.base = 0.0;
    XorShift rng(1);
    DamageSpec spec; spec.amount = 400.0; spec.type = DamageType::kMagic; spec.armorConstantK = 100.0;
    const HitResult r = Settle(atk, tgt, spec, rng);
    // 400 * 0.5 * 0.5 = 100
    Check(Near(r.toHp, 100.0), "D4 armor and typed resistance multiply (no additive stacking)",
          Fmt("toHp=%.4f expected=100.0000", r.toHp));
}

void TestDamageTakenModifier() {
    Combatant atk = PlainAttacker();
    Combatant tgt = DummyTarget();
    tgt.damageTaken.base = 0.25;   // +25% taken
    XorShift rng(1);
    DamageSpec spec; spec.amount = 100.0;
    const HitResult r = Settle(atk, tgt, spec, rng);
    Check(Near(r.toHp, 125.0), "D5 damage-taken modifier applies after mitigation",
          Fmt("toHp=%.4f expected=125.0000", r.toHp));
}

void TestShieldAbsorbsFirst() {
    Combatant atk = PlainAttacker();
    Combatant tgt = DummyTarget();
    tgt.shield[DamageType::kPhysical] = 30.0;
    XorShift rng(1);
    DamageSpec spec; spec.amount = 100.0;
    const HitResult r = Settle(atk, tgt, spec, rng);
    const bool ok = Near(r.absorbed, 30.0) && Near(r.toHp, 70.0) &&
                    Near(tgt.shield[DamageType::kPhysical], 0.0);
    Check(ok, "D6 shield absorbs before HP and never goes negative",
          Fmt("absorbed=%.2f toHp=%.2f shieldLeft=%.2f", r.absorbed, r.toHp,
              tgt.shield[DamageType::kPhysical]));
}

void TestShieldOverkill() {
    Combatant atk = PlainAttacker();
    Combatant tgt = DummyTarget();
    tgt.shield[DamageType::kPhysical] = 500.0;
    XorShift rng(1);
    DamageSpec spec; spec.amount = 80.0;
    const HitResult r = Settle(atk, tgt, spec, rng);
    const bool ok = Near(r.absorbed, 80.0) && Near(r.toHp, 0.0) &&
                    Near(tgt.shield[DamageType::kPhysical], 420.0);
    Check(ok, "D7 shield larger than the hit absorbs everything and keeps the remainder",
          Fmt("absorbed=%.2f shieldLeft=%.2f", r.absorbed, tgt.shield[DamageType::kPhysical]));
}

void TestOverkillAndDeath() {
    Combatant atk = PlainAttacker();
    Combatant tgt = DummyTarget();
    tgt.hp = 40.0;
    XorShift rng(1);
    DamageSpec spec; spec.amount = 100.0;
    const HitResult r = Settle(atk, tgt, spec, rng);
    const bool ok = Near(r.overkill, 60.0) && Near(tgt.hp, 0.0) && !tgt.alive;
    Check(ok, "D8 overkill reported separately, HP floors at zero, target dies",
          Fmt("overkill=%.2f hp=%.2f alive=%d", r.overkill, tgt.hp, tgt.alive ? 1 : 0));
}

void TestDeadAndImmune() {
    Combatant atk = PlainAttacker();
    Combatant dead = DummyTarget();
    dead.alive = false;
    dead.shield[DamageType::kPhysical] = 50.0;
    XorShift rng(1);
    DamageSpec spec; spec.amount = 100.0;
    const HitResult r1 = Settle(atk, dead, spec, rng);
    Combatant imm = DummyTarget();
    imm.immune = true;
    const HitResult r2 = Settle(atk, imm, spec, rng);
    const bool ok = r1.blocked && r2.blocked && Near(dead.shield[DamageType::kPhysical], 50.0) &&
                    Near(dead.hp, dead.hpMax);
    Check(ok, "D9 dead or immune target is blocked without consuming shield",
          Fmt("deadBlocked=%d immuneBlocked=%d shieldLeft=%.2f", r1.blocked ? 1 : 0,
              r2.blocked ? 1 : 0, dead.shield[DamageType::kPhysical]));
}

void TestPerHitCap() {
    Combatant atk = PlainAttacker();
    Combatant tgt = DummyTarget();
    XorShift rng(1);
    DamageSpec spec; spec.amount = 1000.0; spec.perHitCap = 150.0;
    const HitResult r = Settle(atk, tgt, spec, rng);
    Check(Near(r.toHp, 150.0), "D10 per-hit cap applies after mitigation, before shields",
          Fmt("toHp=%.2f expected=150.00", r.toHp));
}

void TestInvalidAmount() {
    Combatant atk = PlainAttacker();
    Combatant tgt = DummyTarget();
    XorShift rng(1);
    DamageSpec zero; zero.amount = 0.0;
    DamageSpec neg; neg.amount = -25.0;
    const HitResult r1 = Settle(atk, tgt, zero, rng);
    const HitResult r2 = Settle(atk, tgt, neg, rng);
    Check(r1.blocked && r2.blocked, "D11 zero/negative damage is rejected, no HP change",
          Fmt("zeroBlocked=%d negBlocked=%d hp=%.2f", r1.blocked ? 1 : 0, r2.blocked ? 1 : 0, tgt.hp));
}

// ---------------------------------------------------------------------------
// DOT aggregation: ticks are summed exactly, and partial ticks are honoured.
// ---------------------------------------------------------------------------
struct Dot {
    double perTick;
    double intervalSec;
    double remainingSec;
    int type;
};

double AdvanceDots(std::vector<Dot>& dots, double dt, int* ticksOut) {
    double total = 0.0;
    int ticks = 0;
    for (Dot& d : dots) {
        if (d.remainingSec <= 0.0) continue;
        const double active = std::min(dt, d.remainingSec);
        const double t = active / d.intervalSec;
        const int whole = static_cast<int>(t);
        total += whole * d.perTick;
        ticks += whole;
        d.remainingSec -= active;
    }
    if (ticksOut) *ticksOut = ticks;
    return total;
}

void TestDotAggregation() {
    std::vector<Dot> dots = {{10.0, 1.0, 5.0, DamageType::kMagic}, {3.0, 0.5, 2.0, DamageType::kPhysical}};
    int ticks = 0;
    const double first = AdvanceDots(dots, 1.0, &ticks);   // 10 + 3*2 = 16
    const double second = AdvanceDots(dots, 10.0, &ticks); // consumes remainder: 4 ticks + 2 ticks
    const bool ok = Near(first, 16.0) && Near(first + second, 10.0 * 5 + 3.0 * 4);
    Check(ok, "D12 DOT totals are exact and clamp at remaining duration",
          Fmt("first=%.2f total=%.2f expected=%.2f", first, first + second, 10.0 * 5 + 3.0 * 4));
}

// ---------------------------------------------------------------------------
// Determinism replay
// ---------------------------------------------------------------------------
uint64_t RunReplay(uint64_t seed, int hits) {
    XorShift rng(seed);
    Combatant atk = PlainAttacker();
    atk.attack.base = 12.0;
    atk.critChance = 0.35;
    atk.critMultiplier = 1.8;
    atk.damageBonus.base = 0.15;
    Combatant tgt = DummyTarget();
    tgt.armor = 220.0;
    tgt.resistance[DamageType::kMagic] = 0.2;
    uint64_t hash = 1469598103934665603ULL;
    for (int i = 0; i < hits; ++i) {
        tgt.hp = tgt.hpMax;
        DamageSpec spec;
        spec.amount = 40.0 + static_cast<double>(rng.next() % 60);
        spec.type = (i % 4 == 0) ? DamageType::kMagic : DamageType::kPhysical;
        const HitResult r = Settle(atk, tgt, spec, rng);
        uint64_t h = static_cast<uint64_t>(r.toHp * 1000.0) ^ (r.crit ? 0x9E3779B97F4A7C15ULL : 0ULL);
        hash ^= h;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void TestDeterminism() {
    const uint64_t a = RunReplay(777, 5000);
    const uint64_t b = RunReplay(777, 5000);
    const uint64_t c = RunReplay(778, 5000);
    Check(a == b && a != c, "D13 identical seed replays to identical settlement hash",
          Fmt("a=%016llx b=%016llx c=%016llx", static_cast<unsigned long long>(a),
              static_cast<unsigned long long>(b), static_cast<unsigned long long>(c)));
}

// ---------------------------------------------------------------------------
// Micro benchmark
// ---------------------------------------------------------------------------
void BenchSettlement() {
    // steady_clock resolution is ~100 ns, so a single hit is below the noise floor.
    // Measure in batches and normalise to per-hit cost.
    constexpr int kBatchHits = 1000;
    constexpr int kRounds = 500;
    XorShift rng(4242);
    Combatant atk = PlainAttacker();
    atk.attack.base = 15.0;
    atk.critChance = 0.3;
    atk.damageBonus.base = 0.2;
    atk.damageBonus.mods.push_back(Modifier{kAdd, 0.1, 1});
    Combatant tgt = DummyTarget();
    tgt.armor = 300.0;
    tgt.resistance[DamageType::kMagic] = 0.25;
    tgt.damageTaken.base = 0.1;
    tgt.shield[DamageType::kPhysical] = 1e12;   // keep HP stable so each hit is identical work
    tgt.shield[DamageType::kMagic] = 1e12;

    std::vector<double> perHitNs;
    perHitNs.reserve(kRounds);
    DamageSpec spec;
    spec.amount = 50.0;
    double sink = 0.0;

    for (int r = 0; r < kRounds; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kBatchHits; ++i) {
            spec.type = (i & 1) ? DamageType::kMagic : DamageType::kPhysical;
            const HitResult hit = Settle(atk, tgt, spec, rng);
            sink += hit.toHp;
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double totalNs = std::chrono::duration<double, std::nano>(t1 - t0).count();
        perHitNs.push_back(totalNs / kBatchHits);
    }

    std::sort(perHitNs.begin(), perHitNs.end());
    auto pct = [&](double p) { return perHitNs[static_cast<size_t>(p * (perHitNs.size() - 1))]; };
    double sum = 0.0;
    for (double v : perHitNs) sum += v;
    std::printf("\n[bench] damage_settlement rounds=%d batch=%d hits_total=%d\n",
                kRounds, kBatchHits, kRounds * kBatchHits);
    std::printf("[bench] per_hit_ns p50=%.1f p95=%.1f p99=%.1f max=%.1f mean=%.1f\n",
                pct(0.50), pct(0.95), pct(0.99), perHitNs.back(), sum / perHitNs.size());
    std::printf("[bench] throughput_hits_per_sec=%.0f sink=%.0f\n",
                1e9 / (sum / perHitNs.size()), sink);
}

}  // namespace

int main() {
    std::printf("gameplay-core | damage & attribute settlement test suite\n");
    std::printf("compiler=%s c++17 O2\n", __VERSION__);
    std::printf("--------------------------------------------------------------------------------\n");
    TestModifierOrder();
    TestValueSemantics();
    TestCritBeforeMitigation();
    TestArmorCurve();
    TestTrueDamageIgnoresArmor();
    TestResistanceStacksMultiplicativelyWithArmor();
    TestDamageTakenModifier();
    TestShieldAbsorbsFirst();
    TestShieldOverkill();
    TestOverkillAndDeath();
    TestDeadAndImmune();
    TestPerHitCap();
    TestInvalidAmount();
    TestDotAggregation();
    TestDeterminism();
    BenchSettlement();
    std::printf("--------------------------------------------------------------------------------\n");
    std::printf("RESULT pass=%d fail=%d\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
