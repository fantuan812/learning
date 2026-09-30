// evidence/tests/match-core/src/team_balance.cpp
//
// 匹配到对局 · 分队平衡证据
//   蛇形分配 + 局部交换改进：在满足"每队至少 1 坦克 / 1 治疗"的硬约束下最小化两队 MMR 差；
//   约束不可满足时必须显式失败，而不是产出一个看似平衡但位置残缺的阵容。
//
// 构建：g++ -std=c++17 -O2 -Wall -o build/team_balance.exe src/team_balance.cpp

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name, const std::string& detail = "") {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!detail.empty()) std::printf("        %s\n", detail.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed ? seed : 88172645463325252ULL) {}
    uint64_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    int Range(int lo, int hi) { return lo + static_cast<int>(Next() % static_cast<uint64_t>(hi - lo + 1)); }
};

enum Role : int { kTank = 0, kHealer = 1, kDps = 2 };

struct Player {
    int id = 0;
    double mmr = 0.0;
    int role = kDps;
};

struct Split {
    std::vector<int> a, b;      // 存 index
    double diff = 0.0;
    bool feasible = true;
    std::string note;
};

struct RoleRequirement {
    int minTankPerTeam = 1;
    int minHealerPerTeam = 1;
};

static double Sum(const std::vector<Player>& ps, const std::vector<int>& idx) {
    double s = 0.0;
    for (int i : idx) s += ps[static_cast<size_t>(i)].mmr;
    return s;
}

static int CountRole(const std::vector<Player>& ps, const std::vector<int>& idx, int role) {
    int n = 0;
    for (int i : idx) if (ps[static_cast<size_t>(i)].role == role) ++n;
    return n;
}

static double Diff(const std::vector<Player>& ps, const std::vector<int>& a, const std::vector<int>& b) {
    return std::fabs(Sum(ps, a) - Sum(ps, b));
}

static bool Satisfies(const std::vector<Player>& ps, const Split& s, const RoleRequirement& req) {
    return CountRole(ps, s.a, kTank) >= req.minTankPerTeam &&
           CountRole(ps, s.b, kTank) >= req.minTankPerTeam &&
           CountRole(ps, s.a, kHealer) >= req.minHealerPerTeam &&
           CountRole(ps, s.b, kHealer) >= req.minHealerPerTeam;
}

// 蛇形分配：按 MMR 降序，依次 A,B,B,A,A,B,... 使两队总分天然接近
static void SnakeAssign(const std::vector<Player>& ps, int teamSize,
                        std::vector<int>* a, std::vector<int>* b) {
    std::vector<int> order(ps.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
        if (ps[static_cast<size_t>(x)].mmr != ps[static_cast<size_t>(y)].mmr)
            return ps[static_cast<size_t>(x)].mmr > ps[static_cast<size_t>(y)].mmr;
        return ps[static_cast<size_t>(x)].id < ps[static_cast<size_t>(y)].id;
    });

    a->clear();
    b->clear();
    for (size_t i = 0; i < order.size(); ++i) {
        const int pick = static_cast<int>(i);
        const bool toA = (pick % 4 == 0 || pick % 4 == 3);
        if (toA && static_cast<int>(a->size()) < teamSize) a->push_back(order[i]);
        else if (!toA && static_cast<int>(b->size()) < teamSize) b->push_back(order[i]);
        else if (static_cast<int>(a->size()) < teamSize) a->push_back(order[i]);
        else b->push_back(order[i]);
    }
}

// 局部交换改进：任何一次交换使分差下降就接受；同时不允许破坏角色约束
static int ImproveBySwaps(const std::vector<Player>& ps, Split* s, const RoleRequirement& req) {
    int accepted = 0;
    bool improved = true;
    while (improved) {
        improved = false;
        for (size_t ia = 0; ia < s->a.size(); ++ia) {
            for (size_t ib = 0; ib < s->b.size(); ++ib) {
                std::swap(s->a[ia], s->b[ib]);
                Split probe = *s;
                const double nd = Diff(ps, probe.a, probe.b);
                if (nd < s->diff - 1e-9 && Satisfies(ps, probe, req)) {
                    s->diff = nd;
                    accepted++;
                    improved = true;
                } else {
                    std::swap(s->a[ia], s->b[ib]);   // 回滚
                }
            }
        }
    }
    return accepted;
}

// 角色缺口：两队各自缺失的"必需角色"数量之和
static int Deficiency(const std::vector<Player>& ps, const Split& s, const RoleRequirement& req) {
    auto miss = [&](const std::vector<int>& t, int role, int need) {
        return std::max(0, need - CountRole(ps, t, role));
    };
    return miss(s.a, kTank, req.minTankPerTeam) + miss(s.b, kTank, req.minTankPerTeam) +
           miss(s.a, kHealer, req.minHealerPerTeam) + miss(s.b, kHealer, req.minHealerPerTeam);
}

// 角色修复：按"缺口"逐步贪心。
// 一次交换就要求完全满足是不成立的：当某队同时缺坦克和治疗时（角色全挤在对面），
// 任何单次交换都补不齐两个缺口，必须允许"先补一个、再补另一个"。
static bool RepairRoles(const std::vector<Player>& ps, Split* s, const RoleRequirement& req) {
    for (int step = 0; step < 64; ++step) {
        const int cur = Deficiency(ps, *s, req);
        if (cur == 0) return true;

        int bestDef = cur;
        double bestDiff = 1e18;
        size_t bestA = 0, bestB = 0;
        bool found = false;
        for (size_t ia = 0; ia < s->a.size(); ++ia) {
            for (size_t ib = 0; ib < s->b.size(); ++ib) {
                std::swap(s->a[ia], s->b[ib]);
                const int def = Deficiency(ps, *s, req);
                const double nd = Diff(ps, s->a, s->b);
                std::swap(s->a[ia], s->b[ib]);
                if (def < bestDef || (def == bestDef && def < cur && nd < bestDiff)) {
                    bestDef = def;
                    bestDiff = nd;
                    bestA = ia;
                    bestB = ib;
                    found = true;
                }
            }
        }
        if (!found) return false;                 // 无任何交换能减少缺口 -> 真的不可行
        std::swap(s->a[bestA], s->b[bestB]);
        s->diff = Diff(ps, s->a, s->b);
    }
    return Deficiency(ps, *s, req) == 0;
}

static Split Partition(const std::vector<Player>& ps, int teamSize, const RoleRequirement& req) {
    Split s;
    if (static_cast<int>(ps.size()) != teamSize * 2) {
        s.feasible = false;
        s.note = "lobby size mismatch";
        return s;
    }
    const int tanks = static_cast<int>(std::count_if(ps.begin(), ps.end(),
                                                     [](const Player& p) { return p.role == kTank; }));
    const int healers = static_cast<int>(std::count_if(ps.begin(), ps.end(),
                                                       [](const Player& p) { return p.role == kHealer; }));
    if (tanks < req.minTankPerTeam * 2 || healers < req.minHealerPerTeam * 2) {
        s.feasible = false;
        s.note = "not enough tanks/healers for both teams (tanks=" + std::to_string(tanks) +
                 ", healers=" + std::to_string(healers) + ")";
        return s;
    }

    SnakeAssign(ps, teamSize, &s.a, &s.b);
    s.diff = Diff(ps, s.a, s.b);
    RepairRoles(ps, &s, req);
    ImproveBySwaps(ps, &s, req);

    if (!Satisfies(ps, s, req)) {
        s.feasible = false;
        s.note = "role repair failed after snake assignment";
    }
    s.note = "tank A/B=" + std::to_string(CountRole(ps, s.a, kTank)) + "/" +
             std::to_string(CountRole(ps, s.b, kTank)) + " healer A/B=" +
             std::to_string(CountRole(ps, s.a, kHealer)) + "/" +
             std::to_string(CountRole(ps, s.b, kHealer));
    return s;
}

static std::vector<Player> MakeLobby(int teamSize, uint64_t seed, int tanksTotal, int healersTotal) {
    Rng rng(seed);
    std::vector<Player> ps;
    for (int i = 0; i < teamSize * 2; ++i) {
        Player p;
        p.id = i + 1;
        p.mmr = 1500 + rng.Range(-300, 300);
        p.role = kDps;
        ps.push_back(p);
    }
    // 确定性放置指定数量的坦克/治疗
    for (int i = 0; i < tanksTotal; ++i) ps[static_cast<size_t>((i * 3) % ps.size())].role = kTank;
    for (int i = 0; i < healersTotal; ++i) ps[static_cast<size_t>((i * 5 + 2) % ps.size())].role = kHealer;
    return ps;
}

// ---------------------------------------------------------------------------
// 断言组
// ---------------------------------------------------------------------------
static void TestSnakeBeatsRandom() {
    Rng rng(31337);
    double snakeSum = 0, randomSum = 0;
    const int rounds = 500;
    for (int r = 0; r < rounds; ++r) {
        const auto ps = MakeLobby(5, 1000 + static_cast<uint64_t>(r), 2, 2);
        const Split s = Partition(ps, 5, RoleRequirement{});
        if (!s.feasible) { Check(false, "B1 snake + swap improves on random assignment", "infeasible lobby"); return; }
        snakeSum += s.diff;

        // 随机基线：随机打乱后取前 5 / 后 5
        std::vector<int> idx(ps.size());
        for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<int>(i);
        for (size_t i = idx.size(); i > 1; --i) std::swap(idx[i - 1], idx[static_cast<size_t>(rng.Next() % i)]);
        std::vector<int> a(idx.begin(), idx.begin() + 5), b(idx.begin() + 5, idx.end());
        randomSum += Diff(ps, a, b);
    }
    const double avgSnake = snakeSum / rounds;
    const double avgRandom = randomSum / rounds;
    Check(avgSnake < avgRandom * 0.35,
          "B1 snake + local swaps produce a far tighter split than random assignment",
          "avg |mmr diff| snake=" + std::to_string(static_cast<int>(avgSnake)) +
              " vs random=" + std::to_string(static_cast<int>(avgRandom)));
}

static void TestImprovementIsMonotone() {
    Rng rng(4242);
    double worstIncrease = 0.0;
    for (int r = 0; r < 300; ++r) {
        const auto ps = MakeLobby(5, 2000 + static_cast<uint64_t>(r), 2, 2);
        Split s;
        SnakeAssign(ps, 5, &s.a, &s.b);
        s.diff = Diff(ps, s.a, s.b);
        const double before = s.diff;
        ImproveBySwaps(ps, &s, RoleRequirement{});
        worstIncrease = std::max(worstIncrease, s.diff - before);
        (void)rng;
    }
    Check(worstIncrease <= 1e-9,
          "B2 local swap improvement never makes the split worse (monotone descent)",
          "worst increase over 300 lobbies = " + std::to_string(worstIncrease));
}

static void TestDeterminism() {
    const auto ps = MakeLobby(5, 777, 2, 2);
    const Split s1 = Partition(ps, 5, RoleRequirement{});
    const Split s2 = Partition(ps, 5, RoleRequirement{});
    Check(s1.a == s2.a && s1.b == s2.b && std::fabs(s1.diff - s2.diff) < 1e-9,
          "B3 partitioning is deterministic for identical input",
          "diff=" + std::to_string(s1.diff));
}

static void TestRoleConstraint() {
    Rng rng(9);
    int checked = 0;
    bool allSatisfied = true;
    double sumDiff = 0.0;
    for (int r = 0; r < 200; ++r) {
        const auto ps = MakeLobby(5, 3000 + static_cast<uint64_t>(r), 3, 4);
        const Split s = Partition(ps, 5, RoleRequirement{});
        if (!s.feasible) { allSatisfied = false; break; }
        if (CountRole(ps, s.a, kTank) < 1 || CountRole(ps, s.b, kTank) < 1 ||
            CountRole(ps, s.a, kHealer) < 1 || CountRole(ps, s.b, kHealer) < 1) {
            allSatisfied = false;
            break;
        }
        sumDiff += s.diff;
        ++checked;
        (void)rng;
    }
    Check(allSatisfied && checked == 200,
          "B4 every team gets at least one tank and one healer when the lobby allows it",
          "checked=" + std::to_string(checked) + " avg diff=" + std::to_string(static_cast<int>(sumDiff / std::max(1, checked))));
}

static void TestInfeasibleIsExplicit() {
    // 10 人里只有 1 个治疗：两队都带治疗不可能，必须显式失败
    const auto ps = MakeLobby(5, 5, 2, 1);
    const Split s = Partition(ps, 5, RoleRequirement{});
    Check(!s.feasible,
          "B5 an unfillable role requirement fails explicitly instead of shipping a broken comp",
          "note=" + s.note);

    // 降级到"只要求坦克"后可继续
    RoleRequirement relaxed;
    relaxed.minHealerPerTeam = 0;
    const Split s2 = Partition(ps, 5, relaxed);
    Check(s2.feasible,
          "B6 relaxing the requirement makes the same lobby feasible again",
          s2.note);
}

static void TestConstraintVsBalanceTradeoff() {
    // 角色约束会牺牲一定平衡度，这个代价必须被量化而非忽略
    Rng rng(11);
    double withConstraint = 0, withoutConstraint = 0;
    const int rounds = 200;
    for (int r = 0; r < rounds; ++r) {
        const auto ps = MakeLobby(5, 4000 + static_cast<uint64_t>(r), 2, 2);
        withConstraint += Partition(ps, 5, RoleRequirement{}).diff;
        RoleRequirement none;
        none.minTankPerTeam = 0;
        none.minHealerPerTeam = 0;
        withoutConstraint += Partition(ps, 5, none).diff;
        (void)rng;
    }
    const double a = withConstraint / rounds, b = withoutConstraint / rounds;
    std::printf("        [balance] avg diff with role constraint=%.1f without=%.1f (cost=%.1f)\n", a, b, a - b);
    Check(a >= b - 1e-9,
          "B7 enforcing role constraints costs some balance — the cost is measured, not assumed",
          "delta=" + std::to_string(a - b));
}

static void BenchPartition() {
    const auto ps = MakeLobby(5, 123, 2, 2);
    constexpr int kRounds = 20000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kRounds; ++i) {
        const Split s = Partition(ps, 5, RoleRequirement{});
        if (!s.feasible) { std::printf("unexpected infeasible\n"); break; }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kRounds;
    std::printf("\n[bench] team split (5v5, with role repair + local swaps) = %.2f us/lobby\n", us);
}

int main() {
    std::printf("== team_balance: 分队平衡证据 ==\n\n");
    TestSnakeBeatsRandom();
    TestImprovementIsMonotone();
    TestDeterminism();
    TestRoleConstraint();
    TestInfeasibleIsExplicit();
    TestConstraintVsBalanceTradeoff();
    BenchPartition();
    std::printf("\nRESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
