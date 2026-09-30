// Gameplay core evidence: skill request pipeline (validation, gates, idempotency,
// deterministic replay, client prediction rollback).
// Build: g++ -std=c++17 -O2 -o skill_pipeline.exe skill_pipeline.cpp
#include <cstdio>
#include <cmath>
#include <limits>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace {

struct SkillDef {
    int id;
    int64_t cooldownMs;
    int manaCost;
    int64_t castMs;
    bool interruptible;
    double minRange;
    double maxRange;
    bool hostile;
};

struct Request {
    uint64_t requestId;
    int skillId;
    int targetId;
    int64_t nowMs;
    double targetDistance;
};

struct Actor {
    bool alive = true;
    int mana = 100;
    int maxMana = 100;
    int64_t gcdUntilMs = 0;
    int64_t castingUntilMs = 0;
    int castingSkill = 0;
    bool castingInterruptible = true;
    std::unordered_map<int, int64_t> cooldownEnd;
    std::unordered_set<uint64_t> applied;
    // telemetry for the replay hash
    uint64_t acceptedHash = 1469598103934665603ULL;
    int accepted = 0;
    int rejected = 0;
};

class SkillServer {
public:
    void Register(const SkillDef& d) { defs_[d.id] = d; }
    const SkillDef* Def(int id) const {
        auto it = defs_.find(id);
        return it == defs_.end() ? nullptr : &it->second;
    }

    std::string Handle(Actor& a, const Request& r) {
        const int64_t GCD_MS = 500;
        if (a.applied.count(r.requestId)) { ++a.rejected; return "duplicate"; }
        const SkillDef* d = Def(r.skillId);
        if (!d) { ++a.rejected; return "unknown-skill"; }
        if (!a.alive) { ++a.rejected; return "dead"; }
        if (r.nowMs < a.castingUntilMs && !a.castingInterruptible) { ++a.rejected; return "busy"; }
        if (r.nowMs < a.gcdUntilMs) { ++a.rejected; return "gcd"; }
        auto cd = a.cooldownEnd.find(r.skillId);
        if (cd != a.cooldownEnd.end() && r.nowMs < cd->second) { ++a.rejected; return "cooldown"; }
        if (a.mana < d->manaCost) { ++a.rejected; return "no-mana"; }
        if (d->hostile && r.targetId == 0) { ++a.rejected; return "invalid-target"; }
        // Range comparisons alone do not reject NaN. Validate before mutation.
        if (!std::isfinite(r.targetDistance)) { ++a.rejected; return "invalid-distance"; }
        if (r.targetDistance < d->minRange || r.targetDistance > d->maxRange) {
            ++a.rejected; return "out-of-range";
        }

        a.mana -= d->manaCost;
        a.cooldownEnd[r.skillId] = r.nowMs + d->cooldownMs;
        a.castingSkill = r.skillId;
        a.castingUntilMs = r.nowMs + d->castMs;
        a.castingInterruptible = d->interruptible;
        a.gcdUntilMs = r.nowMs + GCD_MS;
        a.applied.insert(r.requestId);
        ++a.accepted;
        // FNV-1a over the accepted request keeps the replay hash comparable across runs.
        uint64_t h = a.acceptedHash;
        h ^= static_cast<uint64_t>(r.requestId * 1315423911u + r.skillId);
        h *= 1099511628211ULL;
        a.acceptedHash = h;
        return "accepted";
    }

    void Interrupt(Actor& a) {
        if (a.castingInterruptible) { a.castingUntilMs = 0; a.castingSkill = 0; }
    }

private:
    std::unordered_map<int, SkillDef> defs_;
};

int gPass = 0;
int gFail = 0;
void Check(bool ok, const char* name, const std::string& detail) {
    if (ok) { ++gPass; std::printf("PASS  %-52s %s\n", name, detail.c_str()); }
    else    { ++gFail; std::printf("FAIL  %-52s %s\n", name, detail.c_str()); }
}

SkillServer MakeServer() {
    SkillServer s;
    s.Register(SkillDef{1, 6000, 20, 300, true,  0.0, 5.0, true});   // instant-ish attack
    s.Register(SkillDef{2, 12000, 40, 1500, false, 0.0, 30.0, true}); // hard cast, not interruptible
    s.Register(SkillDef{3, 3000, 0, 0, true, 0.0, 100.0, false});    // self buff, free
    return s;
}

void TestAccept() {
    SkillServer s = MakeServer();
    Actor a;
    const std::string r = s.Handle(a, Request{1, 1, 9, 0, 3.0});
    Check(r == "accepted" && a.mana == 80 && a.cooldownEnd[1] == 6000 && a.gcdUntilMs == 500,
          "S1 valid request accepted and state mutated",
          "reason=" + r + " mana=" + std::to_string(a.mana));
}

void TestCooldownGate() {
    SkillServer s = MakeServer();
    Actor a;
    s.Handle(a, Request{1, 1, 9, 0, 3.0});
    const std::string r = s.Handle(a, Request{2, 1, 9, 1000, 3.0});
    Check(r == "cooldown" && a.mana == 80,
          "S2 cooldown gate rejects recast and refunds nothing",
          "reason=" + r + " mana=" + std::to_string(a.mana));
}

void TestGcdGate() {
    SkillServer s = MakeServer();
    Actor a;
    s.Handle(a, Request{1, 3, 0, 0, 0.0});
    const std::string r = s.Handle(a, Request{2, 1, 9, 200, 3.0});
    Check(r == "gcd",
          "S3 global cooldown gates a different skill",
          "reason=" + r);
}

void TestManaGate() {
    SkillServer s = MakeServer();
    Actor a;
    a.mana = 10;
    const std::string r = s.Handle(a, Request{1, 1, 9, 0, 3.0});
    Check(r == "no-mana" && a.mana == 10 && a.cooldownEnd.find(1) == a.cooldownEnd.end(),
          "S4 insufficient mana rejected without side effects",
          "reason=" + r);
}

void TestDeadAndRange() {
    SkillServer s = MakeServer();
    Actor a;
    a.alive = false;
    const std::string dead = s.Handle(a, Request{1, 1, 9, 0, 3.0});
    Actor b;
    const std::string far = s.Handle(b, Request{1, 1, 9, 0, 42.0});
    Check(dead == "dead" && far == "out-of-range" && b.mana == 100,
          "S5 dead actor and out-of-range target rejected",
          "dead=" + dead + " far=" + far);
}

void TestBusyNonInterruptible() {
    SkillServer s = MakeServer();
    Actor a;
    s.Handle(a, Request{1, 2, 9, 0, 10.0});     // long, non-interruptible
    const std::string r = s.Handle(a, Request{2, 1, 9, 100, 3.0});
    Check(r == "busy",
          "S6 non-interruptible cast blocks other requests",
          "reason=" + r);
}

void TestIdempotency() {
    SkillServer s = MakeServer();
    Actor a;
    s.Handle(a, Request{77, 1, 9, 0, 3.0});
    const std::string again = s.Handle(a, Request{77, 1, 9, 0, 3.0});
    Check(again == "duplicate" && a.mana == 80 && a.accepted == 1,
          "S7 retransmitted requestId is applied exactly once",
          "reason=" + again + " mana=" + std::to_string(a.mana) +
          " accepted=" + std::to_string(a.accepted));
}

// Deterministic replay: same scripted stream must reach the same state hash.
uint64_t RunScript(uint64_t seed) {
    SkillServer s = MakeServer();
    Actor a;
    uint64_t x = seed ? seed : 88172645463325252ULL;
    for (uint64_t i = 0; i < 4000; ++i) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;      // xorshift64
        const int skill = 1 + static_cast<int>(x % 3);
        const int64_t now = static_cast<int64_t>(i * 137);
        const double dist = static_cast<double>((x >> 8) % 40);
        s.Handle(a, Request{i + 1, skill, (x % 5 == 0) ? 0 : 9, now, dist});
        if (i % 97 == 0) s.Interrupt(a);
    }
    uint64_t h = a.acceptedHash;
    h ^= static_cast<uint64_t>(a.mana) * 2654435761u;
    h ^= static_cast<uint64_t>(a.accepted) * 40503u;
    h ^= static_cast<uint64_t>(a.rejected) * 2246822519u;
    return h;
}

void TestDeterminism() {
    const uint64_t h1 = RunScript(12345);
    const uint64_t h2 = RunScript(12345);
    const uint64_t h3 = RunScript(999);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "h1=%016llx h2=%016llx h3=%016llx",
                  static_cast<unsigned long long>(h1),
                  static_cast<unsigned long long>(h2),
                  static_cast<unsigned long long>(h3));
    Check(h1 == h2 && h1 != h3, "S8 identical input stream replays to identical state", buf);
}

void TestPredictionRollback() {
    SkillServer s = MakeServer();
    Actor server;
    // Client predicts the cast optimistically.
    int predictedMana = 100 - 20;
    server.cooldownEnd[1] = 5000;                     // authoritative: still on cooldown
    const std::string r = s.Handle(server, Request{5, 1, 9, 3000, 3.0});
    if (r != "accepted") predictedMana = server.mana; // reconcile back to authoritative value
    Check(r == "cooldown" && predictedMana == 100,
          "S9 client prediction rolls back to authoritative on reject",
          "serverReason=" + r + " reconciledMana=" + std::to_string(predictedMana));
}

void TestInterrupt() {
    SkillServer s = MakeServer();
    Actor a;
    s.Handle(a, Request{1, 1, 9, 0, 3.0});   // interruptible cast
    s.Interrupt(a);
    Check(a.castingUntilMs == 0 && a.castingSkill == 0,
          "S10 interruptible cast is cancelled by damage",
          "castingUntil=" + std::to_string(a.castingUntilMs));
}


void TestNonFiniteDistance() {
    SkillServer s = MakeServer();
    const double values[] = {std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity(),
                            -std::numeric_limits<double>::infinity()};
    const char* names[] = {"S11 NaN rejected without gameplay mutations",
                           "S12 positive infinity rejected without mutations",
                           "S13 negative infinity rejected without mutations"};
    for (int i = 0; i < 3; ++i) {
        Actor a;
        const auto beforeHash = a.acceptedHash;
        const std::string r = s.Handle(a, Request{88, 1, 9, 0, values[i]});
        Check(r == "invalid-distance" && a.mana == 100 && a.cooldownEnd.empty() &&
              a.gcdUntilMs == 0 && a.castingUntilMs == 0 && a.castingSkill == 0 &&
              a.applied.empty() && a.accepted == 0 && a.rejected == 1 &&
              a.acceptedHash == beforeHash, names[i], "reason=" + r);
    }
}

void TestRangeBoundaries() {
    SkillServer s = MakeServer();
    const double values[] = {-1.0, 0.0, 5.0, 5.01};
    const char* expected[] = {"out-of-range", "accepted", "accepted", "out-of-range"};
    bool ok = true;
    for (int i = 0; i < 4; ++i) {
        Actor a;
        const std::string result = s.Handle(a, Request{90, 1, 9, 0, values[i]});
        ok = (result == expected[i]) && ok;
    }
    Check(ok, "S14 finite inclusive range boundaries preserved", "[-1, 0, 5, 5.01]");
}

}  // namespace

int main() {
    std::printf("gameplay-core | skill request pipeline test suite\n");
    std::printf("compiler=%s (build flags recorded by runner)\n", __VERSION__);
    std::printf("--------------------------------------------------------------------------------\n");
    TestAccept();
    TestCooldownGate();
    TestGcdGate();
    TestManaGate();
    TestDeadAndRange();
    TestBusyNonInterruptible();
    TestIdempotency();
    TestDeterminism();
    TestPredictionRollback();
    TestInterrupt();
    TestNonFiniteDistance();
    TestRangeBoundaries();
    std::printf("--------------------------------------------------------------------------------\n");
    std::printf("RESULT pass=%d fail=%d\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
