// C++17 local, serial settlement model. No engine/network/event dispatcher.
// Compile the separate tests/damage_contract.cpp; this file has no test main.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <utility>
#include <limits>
#include <stdexcept>
#include <vector>

namespace damage {
enum class Status { accepted, dead, immune, invalid, range };
enum ModOp : int { kAdd = 0, kMul = 1, kOverride = 2 };
struct Modifier { ModOp op; double value; uint64_t source; };

// Check BEFORE arithmetic. Overflow is a rejected transaction, not saturation.
bool Add(double a, double b, double& out) {
    const double max = std::numeric_limits<double>::max();
    if ((b > 0 && a > max - b) || (b < 0 && a < -max - b)) return false;
    out = a + b;
    return std::isfinite(out);
}
bool Mul(double a, double b, double& out) {
    const double aa = std::fabs(a), bb = std::fabs(b);
    if (bb > 1 && aa > std::numeric_limits<double>::max() / bb) return false;
    out = a * b;
    return std::isfinite(out); // underflow to zero is allowed (no minimum damage)
}
struct Attribute {
    double base = 0;
    std::vector<Modifier> mods;
    Status Evaluate(double& out) const {
        if (!std::isfinite(base)) return Status::invalid;
        double v = base, add = 0, mul = 1;
        for (const auto& m : mods) {
            if (!std::isfinite(m.value) || (m.op != kAdd && m.op != kMul && m.op != kOverride))
                return Status::invalid;
            if (m.op == kOverride) v = m.value; // last override wins
        }
        // Preserve source order WITHIN a category, not permutation invariance.
        for (const auto& m : mods) {
            if (m.op == kAdd && !Add(add, m.value, add)) return Status::range;
            if (m.op == kMul && !Mul(mul, m.value, mul)) return Status::range;
        }
        if (!Add(v, add, v) || !Mul(v, mul, v)) return Status::range;
        out = v;
        return Status::accepted;
    }
    double Value() const { // thin compatibility interface; no unchecked arithmetic
        double out = 0;
        const auto status = Evaluate(out);
        if (status == Status::invalid) throw std::invalid_argument("invalid attribute");
        if (status == Status::range) throw std::overflow_error("attribute range");
        return out;
    }
};
struct DamageType { enum E { kPhysical = 0, kMagic = 1, kTrue = 2, kCount = 3 }; };
bool ValidType(int type) { return type >= 0 && type < DamageType::kCount; }
struct Combatant {
    Attribute attack;
    double critChance = 0;
    double critMultiplier = 2;
    Attribute damageBonus;
    double armor = 0;
    double resistance[DamageType::kCount] = {0, 0, 0};
    Attribute damageTaken;
    double shield[DamageType::kCount] = {0, 0, 0};
    double hp = 1000, hpMax = 1000;
    bool alive = true, immune = false;
};
struct DamageSpec {
    double amount = 0;
    int type = DamageType::kPhysical;
    double armorConstantK = 100;
    double perHitCap = 0; // zero unlimited; otherwise post-mitigation, pre-shield
};
struct HitResult {
    Status status = Status::accepted;
    double raw = 0, mitigated = 0, capped = 0;
    double absorbed = 0; // allocated shield absorption, possibly rounded in storage
    double requestedHpDamage = 0, toHp = 0, overkill = 0;
    double actualShieldLoss = 0, shieldRoundingResidual = 0, roundingResidual = 0;
    bool crit = false, blocked = false;
};
struct XorShift {
    uint64_t s;
    explicit XorShift(uint64_t seed) : s(seed ? seed : 0x2545F4914F6CDD1DULL) {}
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
};
struct Values { double attack = 0, bonusFactor = 0, takenFactor = 0; };
Status Validate(const Combatant& c, Values& v) {
    if (!std::isfinite(c.hp) || !std::isfinite(c.hpMax) || c.hp < 0 || c.hpMax <= 0 ||
        c.hp > c.hpMax || c.alive != (c.hp > 0) || !std::isfinite(c.armor) ||
        !std::isfinite(c.critChance) || c.critChance < 0 || c.critChance > 1 ||
        !std::isfinite(c.critMultiplier) || c.critMultiplier < 0) return Status::invalid;
    for (int i = 0; i < DamageType::kCount; ++i)
        if (!std::isfinite(c.resistance[i]) || !std::isfinite(c.shield[i]) || c.shield[i] < 0)
            return Status::invalid;
    double bonus = 0, taken = 0;
    for (auto pair : {std::pair<const Attribute*, double*>{&c.attack, &v.attack},
                      {&c.damageBonus, &bonus}, {&c.damageTaken, &taken}}) {
        const auto status = pair.first->Evaluate(*pair.second);
        if (status != Status::accepted) return status;
    }
    if (v.attack < 0 || bonus < -1 || taken < -1) return Status::invalid;
    if (!Add(1, bonus, v.bonusFactor) || !Add(1, taken, v.takenFactor)) return Status::range;
    return Status::accepted;
}
// Preconditions: finite armor and finite positive K (checked by Settle).
// Avoid cancellation in 1-A/(A+K) and overflow in A+K.
double ArmorRemaining(double armor, double k) {
    if (armor <= 0) return 1;
    if (armor >= k) { const double q = k / armor; return q / (1 + q); }
    const double q = armor / k;
    return 1 / (1 + q);
}
HitResult Rejected(Status status) { HitResult r; r.status = status; r.blocked = true; return r; }
HitResult Settle(const Combatant& attacker, Combatant& target, const DamageSpec& spec, XorShift& rng) {
    // Invalid snapshot/configuration takes precedence over dead/immune.
    if (!ValidType(spec.type) || !std::isfinite(spec.amount) || spec.amount <= 0 ||
        !std::isfinite(spec.armorConstantK) || spec.armorConstantK <= 0 ||
        !std::isfinite(spec.perHitCap) || spec.perHitCap < 0) return Rejected(Status::invalid);
    Values av, tv;
    auto status = Validate(attacker, av);
    if (status != Status::accepted) return Rejected(status);
    status = Validate(target, tv);
    if (status != Status::accepted) return Rejected(status);
    if (!target.alive) return Rejected(Status::dead);
    if (target.immune) return Rejected(Status::immune);
    XorShift pendingRng = rng; // a rejected calculation does not consume the stream
    HitResult r;
    double raw = 0;
    if (!Mul(spec.amount, av.attack, raw)) return Rejected(Status::range);
    if (attacker.critChance > 0 && pendingRng.unit() < attacker.critChance) {
        r.crit = true;
        if (!Mul(raw, attacker.critMultiplier, raw)) return Rejected(Status::range);
    }
    if (!Mul(raw, av.bonusFactor, raw)) return Rejected(Status::range);
    r.raw = raw;
    double value = raw;
    if (spec.type != DamageType::kTrue &&
        !Mul(value, ArmorRemaining(target.armor, spec.armorConstantK), value)) return Rejected(Status::range);
    if (!Mul(value, 1 - std::clamp(target.resistance[spec.type], 0.0, 1.0), value) ||
        !Mul(value, tv.takenFactor, value)) return Rejected(Status::range);
    r.mitigated = value;
    r.capped = spec.perHitCap > 0 ? std::min(value, spec.perHitCap) : value;
    const double shieldBefore = target.shield[spec.type];
    r.absorbed = std::min(shieldBefore, r.capped);
    const double shieldAfter = shieldBefore - r.absorbed;
    r.actualShieldLoss = shieldBefore - shieldAfter;
    r.shieldRoundingResidual = r.absorbed - r.actualShieldLoss;
    r.requestedHpDamage = r.capped - r.absorbed;
    const double before = target.hp;
    const double after = r.requestedHpDamage >= before ? 0 : before - r.requestedHpDamage;
    r.toHp = before - after;
    r.overkill = r.requestedHpDamage > before ? r.requestedHpDamage - before : 0;
    r.roundingResidual = (r.requestedHpDamage - r.toHp) - r.overkill;
    // Commit only after every potentially failing calculation succeeds.
    target.shield[spec.type] = shieldAfter;
    target.hp = after;
    target.alive = after > 0;
    rng = pendingRng;
    return r;
}

// DOT is only a raw count/value accumulator, NOT calls to Settle.
// One unit = one microsecond. First tick at interval; terminal partial discarded.
struct DotTicks {
    double perTick;
    uint64_t intervalUs, remainingUs;
    int type;
    uint64_t phaseUs = 0;
};
struct DotResult { Status status = Status::accepted; double total = 0; uint64_t ticks = 0; };
DotResult AdvanceDotUnits(std::vector<DotTicks>& dots, uint64_t dtUs) {
    auto pending = dots;
    DotResult result;
    for (auto& d : pending) {
        if (!std::isfinite(d.perTick) || d.perTick <= 0 || !ValidType(d.type) ||
            d.intervalUs == 0 || d.phaseUs >= d.intervalUs || (d.remainingUs == 0 && d.phaseUs != 0))
            return {Status::invalid, 0, 0};
        const uint64_t active = std::min(dtUs, d.remainingUs);
        uint64_t whole = active / d.intervalUs;
        const uint64_t remainder = active % d.intervalUs;
        // divmod plus carry avoids overflowing phase+active.
        uint64_t phase = d.phaseUs;
        if (remainder >= d.intervalUs - phase) {
            if (whole == std::numeric_limits<uint64_t>::max()) return {Status::range, 0, 0};
            ++whole;
            phase = remainder - (d.intervalUs - phase);
        } else {
            phase += remainder; // proven < intervalUs, hence representable
        }
        if (whole > std::numeric_limits<uint64_t>::max() - result.ticks) return {Status::range, 0, 0};
        double amount = 0;
        if (!Mul(d.perTick, static_cast<double>(whole), amount) || !Add(result.total, amount, result.total))
            return {Status::range, 0, 0};
        result.ticks += whole;
        d.remainingUs -= active;
        d.phaseUs = d.remainingUs == 0 ? 0 : phase;
    }
    dots.swap(pending);
    return result;
}

// Compatibility only: each supplied seconds value is independently rounded to
// nearest microsecond, ties upward. This does NOT promise arbitrary-double
// partition invariance. Authoritative callers use integer time above.
Status SecondsToUnits(double seconds, uint64_t& out) {
    static_assert(std::numeric_limits<double>::radix == 2 &&
                  std::numeric_limits<double>::digits == 53, "binary64 required");
    if (!std::isfinite(seconds) || seconds < 0) return Status::invalid;
    if (seconds == 0) { out = 0; return Status::accepted; }
    int exponent = 0;
    const double fraction = std::frexp(seconds, &exponent);
    // seconds = M * 2^(exponent-53), exactly. M is representable as uint64.
    const uint64_t m = static_cast<uint64_t>(std::ldexp(fraction, 53));
    // Multiplication by 1,000,000 = 15,625 * 2^6. Keep the <=67-bit
    // product in two limbs: no long-double precision assumption or __int128.
    const uint64_t lowProduct = (m & 0xffffffffULL) * 15625ULL;
    const uint64_t highProduct = (m >> 32) * 15625ULL;
    const uint64_t low = lowProduct + (highProduct << 32); // defined unsigned wrap
    const uint64_t high = (highProduct >> 32) + (low < lowProduct ? 1ULL : 0ULL);
    const int shift = 47 - exponent;
    if (shift <= 0) return Status::range; // magnitude already exceeds uint64
    if (shift > 68) { out = 0; return Status::accepted; }
    uint64_t whole = 0, halfBit = 0;
    if (shift < 64) {
        if ((high >> shift) != 0) return Status::range;
        whole = (high << (64 - shift)) | (low >> shift);
        halfBit = (low >> (shift - 1)) & 1ULL;
    } else {
        whole = high >> (shift - 64);
        halfBit = shift == 64 ? low >> 63 : (high >> (shift - 65)) & 1ULL;
    }
    // All discarded bits below halfBit are irrelevant to positive half-up.
    if (halfBit != 0) {
        if (whole == std::numeric_limits<uint64_t>::max()) return Status::range;
        ++whole;
    }
    out = whole;
    return Status::accepted;
}
void ThrowIfRejected(Status s) {
    if (s == Status::invalid) throw std::invalid_argument("invalid DOT/time");
    if (s == Status::range) throw std::overflow_error("DOT/time range");
}
struct Dot {
    double perTick, intervalSec, remainingSec;
    int type;
    DotTicks clock;
    Dot(double damage, double interval, double duration, int damageType)
        : perTick(damage), intervalSec(interval), remainingSec(duration), type(damageType),
          clock{damage, 0, 0, damageType, 0} {
        ThrowIfRejected(SecondsToUnits(interval, clock.intervalUs));
        ThrowIfRejected(SecondsToUnits(duration, clock.remainingUs));
        remainingSec = static_cast<double>(clock.remainingUs) / 1000000.0;
    }
};
double AdvanceDots(std::vector<Dot>& dots, double dt, int* ticksOut) {
    uint64_t units = 0;
    ThrowIfRejected(SecondsToUnits(dt, units));
    std::vector<DotTicks> clocks;
    for (const auto& d : dots) {
        // Legacy public mirrors are read-only after creation; corrupt edits reject.
        uint64_t interval = 0;
        ThrowIfRejected(SecondsToUnits(d.intervalSec, interval));
        if (d.perTick != d.clock.perTick || d.type != d.clock.type || interval != d.clock.intervalUs ||
            d.remainingSec != static_cast<double>(d.clock.remainingUs) / 1000000.0)
            throw std::invalid_argument("changed DOT compatibility mirror");
        clocks.push_back(d.clock);
    }
    const auto result = AdvanceDotUnits(clocks, units);
    ThrowIfRejected(result.status);
    if (result.ticks > static_cast<uint64_t>(std::numeric_limits<int>::max()))
        throw std::overflow_error("legacy int tick count");
    for (std::size_t i = 0; i < dots.size(); ++i) {
        dots[i].clock = clocks[i];
        dots[i].remainingSec = static_cast<double>(clocks[i].remainingUs) / 1000000.0;
    }
    if (ticksOut) *ticksOut = static_cast<int>(result.ticks);
    return result.total;
}
} // namespace damage
