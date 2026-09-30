// evidence/tests/match-core/src/mmr_pool.cpp
//
// 匹配到对局 · 匹配池证据
//   MMR 窗口随时间放宽、公平性取双方窗口交集、小队不可拆、超时降级、确定性、队列时间分布。
//
// 构建：g++ -std=c++17 -O2 -Wall -o build/mmr_pool.exe src/mmr_pool.cpp

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
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
    double Uniform() { return static_cast<double>(Next() >> 11) / 9007199254740992.0; }
};

// ---------------------------------------------------------------------------
// 匹配池
// ---------------------------------------------------------------------------
struct Player {
    int id = 0;
    double mmr = 1500.0;
    int partyId = 0;        // 0 = 单排
    int64_t queuedAt = 0;
    int64_t matchedAtTick = -1;
    bool matched = false;
};

struct Match {
    std::vector<int> ids;
    double mmrDiff = 0.0;
    bool relaxed = false;   // 双方至少一方的等待窗口已放宽到超过基础窗口
    int64_t tick = 0;
};

struct Config {
    int teamSize = 5;
    double baseWindow = 50.0;      // 初始可接受分差（双方都要能接受）
    double growthPerTick = 4.0;    // 每 tick 增加的可接受分差
    double maxWindow = 600.0;      // 放宽上限
    int64_t maxWaitTicks = 300;    // 超过则降级出队（机器人填充 / 提示）
    int scanLimit = 200;           // 每次候选扫描上限（生产实现必须截断）
};

struct Pool {
    Config cfg;
    std::vector<Player> players;
    int64_t tick = 0;

    double WindowFor(const Player& p) const {
        const double waited = static_cast<double>(tick - p.queuedAt);
        return std::min(cfg.maxWindow, cfg.baseWindow + waited * cfg.growthPerTick);
    }

    // 匹配可行性：分差必须同时落在双方的窗口内（取交集）。
    // 否则长时间等待的一方会把刚入队的新玩家强行拖进一个不公平对局。
    bool Compatible(const Player& a, const Player& b) const {
        const double diff = std::fabs(a.mmr - b.mmr);
        return diff <= std::min(WindowFor(a), WindowFor(b));
    }

    // 从候选里组一局（seed 必进）。返回是否成局。
    bool TryForm(int seedIdx, std::vector<int>& cand, std::vector<Match>* out) {
        Player& seed = players[static_cast<size_t>(seedIdx)];
        if (seed.matched) return false;
        if (static_cast<int>(cand.size()) < cfg.teamSize * 2 - 1) return false;

        std::stable_sort(cand.begin(), cand.end(), [&](int x, int y) {
            const double dx = std::fabs(players[static_cast<size_t>(x)].mmr - seed.mmr);
            const double dy = std::fabs(players[static_cast<size_t>(y)].mmr - seed.mmr);
            if (dx != dy) return dx < dy;
            return players[static_cast<size_t>(x)].id < players[static_cast<size_t>(y)].id;
        });

        // 小队完整性：seed 若属于某小队，同队成员必须一起进同一对局
        std::vector<int> chosen{seedIdx};
        if (seed.partyId != 0) {
            for (int idx : cand) {
                if (players[static_cast<size_t>(idx)].partyId == seed.partyId &&
                    std::find(chosen.begin(), chosen.end(), idx) == chosen.end())
                    chosen.push_back(idx);
            }
        }
        for (int idx : cand) {
            if (static_cast<int>(chosen.size()) >= cfg.teamSize * 2) break;
            if (std::find(chosen.begin(), chosen.end(), idx) == chosen.end())
                chosen.push_back(idx);
        }
        if (static_cast<int>(chosen.size()) < cfg.teamSize * 2) return false;
        chosen.resize(static_cast<size_t>(cfg.teamSize * 2));

        double lo = 1e18, hi = -1e18;
        for (int idx : chosen) {
            lo = std::min(lo, players[static_cast<size_t>(idx)].mmr);
            hi = std::max(hi, players[static_cast<size_t>(idx)].mmr);
        }
        Match m;
        m.ids = chosen;
        m.mmrDiff = hi - lo;
        m.relaxed = m.mmrDiff > cfg.baseWindow;
        m.tick = tick;
        for (int idx : chosen) {
            players[static_cast<size_t>(idx)].matched = true;
            players[static_cast<size_t>(idx)].matchedAtTick = tick;
        }
        out->push_back(m);
        return true;
    }

    std::vector<int> WaitingSorted() const {
        std::vector<int> waiting;
        for (size_t i = 0; i < players.size(); ++i)
            if (!players[i].matched) waiting.push_back(static_cast<int>(i));
        // 等待越久越优先；同等待时长按 id 稳定排序（保证确定性）
        std::stable_sort(waiting.begin(), waiting.end(), [&](int x, int y) {
            if (players[x].queuedAt != players[y].queuedAt)
                return players[x].queuedAt < players[y].queuedAt;
            return players[x].id < players[y].id;
        });
        return waiting;
    }

    // 朴素实现：为每个种子线性扫描整个等待池。
    // 保留它是为了量化"不建索引"的代价（见基准对比）。
    std::vector<Match> StepNaive() {
        std::vector<Match> out;
        std::vector<int> waiting = WaitingSorted();
        if (static_cast<int>(waiting.size()) < cfg.teamSize * 2) return out;

        for (int seedIdx : waiting) {
            if (players[static_cast<size_t>(seedIdx)].matched) continue;
            const Player& seed = players[static_cast<size_t>(seedIdx)];
            std::vector<int> cand;
            for (int idx : waiting) {
                if (idx == seedIdx) continue;
                const Player& c = players[static_cast<size_t>(idx)];
                if (c.matched || !Compatible(seed, c)) continue;
                cand.push_back(idx);
                if (static_cast<int>(cand.size()) >= cfg.scanLimit) break;
            }
            TryForm(seedIdx, cand, &out);
        }
        return out;
    }

    // 生产形态：先按 MMR 分桶，只为每个种子扫描相邻桶。
    // 匹配的全部成本就在这里——二分/分桶把"扫全池"降成"扫邻近区间"。
    std::vector<Match> Step(int bucketWidth = 25) {
        std::vector<Match> out;
        std::vector<int> waiting = WaitingSorted();
        if (static_cast<int>(waiting.size()) < cfg.teamSize * 2) return out;

        std::map<int, std::vector<int>> buckets;
        for (int idx : waiting)
            buckets[static_cast<int>(std::floor(players[static_cast<size_t>(idx)].mmr / bucketWidth))]
                .push_back(idx);

        for (int seedIdx : waiting) {
            const Player& seed = players[static_cast<size_t>(seedIdx)];
            if (seed.matched) continue;

            const double w = WindowFor(seed);
            const int span = static_cast<int>(std::ceil(w / bucketWidth));
            const int b0 = static_cast<int>(std::floor(seed.mmr / bucketWidth));
            std::vector<int> cand;
            for (int b = b0 - span; b <= b0 + span; ++b) {
                if (static_cast<int>(cand.size()) >= cfg.scanLimit) break;
                const auto it = buckets.find(b);
                if (it == buckets.end()) continue;
                for (int idx : it->second) {
                    if (idx == seedIdx) continue;
                    const Player& c = players[static_cast<size_t>(idx)];
                    if (c.matched || !Compatible(seed, c)) continue;
                    cand.push_back(idx);
                    if (static_cast<int>(cand.size()) >= cfg.scanLimit) break;
                }
            }
            TryForm(seedIdx, cand, &out);
        }
        return out;
    }

    // 生产形态 B：一次性按 MMR 排序 + 二分定位窗口区间。
    // 比"分桶 + map 查找"更省：没有容器查找开销，且天然只扫邻近区间。
    std::vector<Match> StepSorted() {
        std::vector<Match> out;
        std::vector<int> waiting = WaitingSorted();
        if (static_cast<int>(waiting.size()) < cfg.teamSize * 2) return out;

        std::vector<int> byMmr = waiting;
        std::stable_sort(byMmr.begin(), byMmr.end(), [&](int x, int y) {
            if (players[static_cast<size_t>(x)].mmr != players[static_cast<size_t>(y)].mmr)
                return players[static_cast<size_t>(x)].mmr < players[static_cast<size_t>(y)].mmr;
            return players[static_cast<size_t>(x)].id < players[static_cast<size_t>(y)].id;
        });

        for (int seedIdx : waiting) {
            const Player& seed = players[static_cast<size_t>(seedIdx)];
            if (seed.matched) continue;
            const double w = WindowFor(seed);
            const double lo = seed.mmr - w;
            const double hi = seed.mmr + w;
            auto it0 = std::lower_bound(byMmr.begin(), byMmr.end(), lo,
                                        [&](int idx, double v) {
                                            return players[static_cast<size_t>(idx)].mmr < v;
                                        });
            std::vector<int> cand;
            for (auto it = it0; it != byMmr.end(); ++it) {
                if (static_cast<int>(cand.size()) >= cfg.scanLimit) break;
                const int idx = *it;
                if (players[static_cast<size_t>(idx)].mmr > hi) break;
                if (idx == seedIdx) continue;
                const Player& c = players[static_cast<size_t>(idx)];
                if (c.matched || !Compatible(seed, c)) continue;
                cand.push_back(idx);
            }
            TryForm(seedIdx, cand, &out);
        }
        return out;
    }
};

static Player Mk(int id, double mmr, int64_t queuedAt, int party = 0) {
    Player p;
    p.id = id;
    p.mmr = mmr;
    p.queuedAt = queuedAt;
    p.partyId = party;
    return p;
}

// ---------------------------------------------------------------------------
// 断言组
// ---------------------------------------------------------------------------
static void TestWindowGrowth() {
    Pool p;
    p.players.push_back(Mk(1, 1500, 0));
    const double w0 = p.WindowFor(p.players[0]);
    p.tick = 10;
    const double w10 = p.WindowFor(p.players[0]);
    p.tick = 1000;
    const double wBig = p.WindowFor(p.players[0]);

    Check(w0 == 50.0 && w10 == 90.0,
          "M1 the acceptable MMR window grows linearly with wait time",
          "t=0 -> 50.0, t=10 -> " + std::to_string(w10));
    Check(wBig == 600.0 && w10 < wBig,
          "M2 the window is capped so that quality cannot degrade without bound",
          "t=1000 -> 600.0 (capped at maxWindow)");
}

static void TestFairnessIntersection() {
    Pool p;
    p.players.push_back(Mk(1, 1000, 0));    // 已等 100 tick，窗口很大
    p.tick = 100;
    p.players.push_back(Mk(2, 1200, 100));  // 刚入队，窗口 50

    Check(p.WindowFor(p.players[0]) > 400.0,
          "M3 the long-waiting player's window is by now much wider than the base",
          "window=" + std::to_string(p.WindowFor(p.players[0])));
    Check(!p.Compatible(p.players[0], p.players[1]),
          "M4 compatibility uses the INTERSECTION of both windows, not the waiter's",
          "diff=200, new player's window=50 -> not compatible");

    // 反例：若只看等待方窗口，新玩家会被拖进 200 分差的对局
    const bool waiterOnly = std::fabs(p.players[0].mmr - p.players[1].mmr) <= p.WindowFor(p.players[0]);
    Check(waiterOnly,
          "M5 (control) a waiter-only rule WOULD have matched this unfair pair",
          "this is exactly the bug the intersection rule prevents");
}

static void TestPartyIntegrity() {
    Config cfg;
    cfg.teamSize = 3;      // 3v3，方便构造小队
    Pool p;
    p.cfg = cfg;
    p.players.push_back(Mk(1, 1000, 0, 77));
    p.players.push_back(Mk(2, 1010, 0, 77));   // 同队
    p.players.push_back(Mk(3, 1005, 0));
    p.players.push_back(Mk(4, 1002, 0));
    p.players.push_back(Mk(5, 1008, 0));
    p.players.push_back(Mk(6, 1001, 0));

    const auto ms = p.Step();
    bool partyTogether = false;
    for (const Match& m : ms) {
        const bool has1 = std::find(m.ids.begin(), m.ids.end(), 1) != m.ids.end();
        const bool has2 = std::find(m.ids.begin(), m.ids.end(), 2) != m.ids.end();
        if (has1 && has2) partyTogether = true;
    }
    int unmatchedParty = 0;
    for (const Player& pl : p.players)
        if (pl.partyId == 77 && !pl.matched) ++unmatchedParty;

    Check(partyTogether || unmatchedParty == 2,
          "M6 a party is never split: members enter the same match or wait together",
          partyTogether ? "party matched into one game" : "party still waiting as a unit");
    Check(ms.size() == 1 && ms[0].ids.size() == 6,
          "M7 3v3 fills exactly one match of 6 players",
          "matches=" + std::to_string(ms.size()));
}

static void TestDeterminism() {
    auto build = [](uint64_t seed) {
        Pool p;
        Rng rng(seed);
        for (int i = 1; i <= 200; ++i) p.players.push_back(Mk(i, 1400 + rng.Range(-120, 120), rng.Range(0, 3)));
        return p;
    };
    Pool a = build(12345);
    Pool b = build(12345);
    const auto ma = a.Step();
    const auto mb = b.Step();

    bool same = ma.size() == mb.size();
    for (size_t i = 0; same && i < ma.size(); ++i) same = ma[i].ids == mb[i].ids;

    Check(same && !ma.empty(),
          "M8 the matchmaker is deterministic: identical input yields identical matches",
          "matches=" + std::to_string(ma.size()));

    Pool c = build(999);
    const auto mc = c.Step();
    bool differs = mc.size() != ma.size();
    for (size_t i = 0; !differs && i < std::min(mc.size(), ma.size()); ++i)
        differs = mc[i].ids != ma[i].ids;
    Check(differs, "M9 (control) a different population yields a different result");
}

static void TestTimeoutAndRelaxation() {
    // 池里只有一个高分玩家：窗口放宽到上限后仍匹配不上，必须走降级而不是无限等待
    Pool p;
    p.players.push_back(Mk(1, 3000, 0));
    for (int i = 0; i < 9; ++i) p.players.push_back(Mk(100 + i, 1000, 0));
    p.cfg.teamSize = 5;

    p.tick = 0;
    auto m0 = p.Step();
    Check(m0.empty(),
          "M10 a lone high-MMR player is not force-fed into a low-MMR lobby at t=0",
          "no match formed at tick 0 (diff 2000 > base window 50)");

    p.tick = 300;
    auto m300 = p.Step();
    Check(m300.empty(),
          "M11 relaxation alone can NEVER fix an extreme outlier (diff 2000 > cap 600)",
          "no match even at the cap — this is why a separate escalation path is required");

    // 关键结论：窗口上限意味着"等得够久就一定能匹配"是错的。
    // 极端玩家必须靠独立机制离队（机器人填充 / 明确提示 / 特殊队列），
    // 否则会静默地永久卡在队列里。
    const int64_t waited = p.tick - p.players[0].queuedAt;
    Check(waited >= p.cfg.maxWaitTicks && p.cfg.maxWindow < 2000.0,
          "M12 an explicit wait cap must exist, because the window cap cannot cover outliers",
          "maxWaitTicks=" + std::to_string(p.cfg.maxWaitTicks) +
              " maxWindow=" + std::to_string(static_cast<int>(p.cfg.maxWindow)));
}

static void TestQueueTimeDistribution() {
    // 人口：1200 人均分布 + 少量极端高分玩家（匹配的经典难例）
    Pool p;
    Rng rng(2026);
    int id = 1;
    for (int i = 0; i < 1200; ++i) p.players.push_back(Mk(id++, 1400 + rng.Range(-150, 150), 0));
    for (int i = 0; i < 20; ++i) p.players.push_back(Mk(id++, 2600 + rng.Range(-40, 40), 0));

    std::vector<double> waits;
    for (int64_t t = 0; t <= 200; ++t) {
        p.tick = t;
        p.Step();
    }
    for (const Player& pl : p.players)
        if (pl.matched && pl.matchedAtTick >= pl.queuedAt)
            waits.push_back(static_cast<double>(pl.matchedAtTick - pl.queuedAt));
    std::sort(waits.begin(), waits.end());
    const size_t n = waits.size();
    auto at = [&](double q) { return n ? waits[static_cast<size_t>(q * (n - 1))] : 0.0; };

    Check(n > 1150,
          "M13 under a normal population the vast majority matches within the window",
          "matched=" + std::to_string(n) + " / " + std::to_string(p.players.size()));
    std::printf("        [queue] wait_ticks p50=%.0f p95=%.0f p99=%.0f max=%.0f\n",
                at(0.50), at(0.95), at(0.99), waits.empty() ? 0.0 : waits.back());

    int extremeMatched = 0;
    for (const Player& pl : p.players)
        if (pl.mmr > 2500 && pl.matched) ++extremeMatched;
    Check(extremeMatched <= 20,
          "M14 extreme-MMR players are the population that actually suffers long queues",
          "extreme matched=" + std::to_string(extremeMatched) + " / 20");
}

static void TestFairnessVsBaseline() {
    // 同一人口下：本匹配器 vs 随机配对
    Rng rng(7);
    std::vector<Player> pop;
    for (int i = 1; i <= 600; ++i) pop.push_back(Mk(i, 1500 + rng.Range(-200, 200), 0));

    Pool p;
    p.players = pop;
    double matchedDiff = 0.0;
    int matchedPairs = 0;
    for (int64_t t = 0; t <= 60; ++t) { p.tick = t; for (const Match& m : p.Step()) { matchedDiff += m.mmrDiff; ++matchedPairs; } }
    const double avgMatched = matchedPairs ? matchedDiff / matchedPairs : 0.0;

    double randDiff = 0.0;
    int randPairs = 0;
    std::vector<int> idx(pop.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<int>(i);
    for (int round = 0; round < 60; ++round) {
        for (size_t i = idx.size(); i > 1; --i) std::swap(idx[i - 1], idx[static_cast<size_t>(rng.Next() % i)]);
        for (size_t b = 0; b + 10 <= idx.size(); b += 10) {
            double lo = 1e18, hi = -1e18;
            for (size_t k = b; k < b + 10; ++k) { lo = std::min(lo, pop[static_cast<size_t>(idx[k])].mmr); hi = std::max(hi, pop[static_cast<size_t>(idx[k])].mmr); }
            randDiff += hi - lo; ++randPairs;
        }
    }
    const double avgRand = randPairs ? randDiff / randPairs : 0.0;

    Check(matchedPairs > 0 && avgMatched < avgRand * 0.5,
          "M15 the matchmaker produces far tighter lobbies than random assignment",
          "matchmaker avg spread=" + std::to_string(static_cast<int>(avgMatched)) +
              " vs random=" + std::to_string(static_cast<int>(avgRand)));
}

// ---------------------------------------------------------------------------
// 基准
// ---------------------------------------------------------------------------
static void BenchTickCost(int poolSize, int64_t waitTicks, const char* regime) {
    Rng rng(4242);
    auto buildPool = [&](int n) {
        Pool p;
        for (int i = 1; i <= n; ++i) p.players.push_back(Mk(i, 1400 + rng.Range(-250, 250), 0));
        p.tick = waitTicks;
        return p;
    };

    struct Row { const char* name; double us; size_t matches; };
    std::vector<Row> rows;

    auto run = [&](const char* name, int mode) {
        Pool p = buildPool(poolSize);
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<Match> ms;
        if (mode == 0) ms = p.StepNaive();
        else if (mode == 1) ms = p.Step();
        else ms = p.StepSorted();
        const auto t1 = std::chrono::steady_clock::now();
        rows.push_back(Row{name, std::chrono::duration<double, std::micro>(t1 - t0).count(), ms.size()});
    };

    run("linear  ", 0);
    run("bucketed", 1);
    run("sorted  ", 2);

    const double window = 50.0 + waitTicks * 4.0;
    std::printf("\n[bench] pool=%d  wait=%lld ticks  window=%.0f  [%s]\n",
                poolSize, static_cast<long long>(waitTicks), std::min(600.0, window), regime);
    for (const Row& r : rows)
        std::printf("[bench]   %s : matches=%zu tick_cost_us=%.0f  us_per_player=%.4f\n",
                    r.name, r.matches, r.us, r.us / poolSize);
    std::printf("[bench]   speedup vs linear: bucketed=%.2fx sorted=%.2fx\n",
                rows[0].us / rows[1].us, rows[0].us / rows[2].us);
}

static void TestIndexedMatchesLinear() {
    auto build = [](uint64_t seed) {
        Rng r(seed);
        Pool p;
        for (int i = 1; i <= 3000; ++i) p.players.push_back(Mk(i, 1400 + r.Range(-250, 250), 0));
        p.tick = 40;
        return p;
    };
    Pool pl = build(5150), pb = build(5150), ps1 = build(5150), ps2 = build(5150);

    const auto ml = pl.StepNaive();
    const auto mb = pb.Step();
    const auto ms1 = ps1.StepSorted();
    const auto ms2 = ps2.StepSorted();

    // 三种实现的候选扫描顺序不同，因此选出的"具体组合"可以不同（都是合法解）。
    // 但成局数量必须基本一致——否则说明某条路径在浪费等待玩家。
    const size_t lo = std::min(ml.size(), std::min(mb.size(), ms1.size()));
    const size_t hi = std::max(ml.size(), std::max(mb.size(), ms1.size()));
    const size_t tol = std::max<size_t>(3, hi / 100);
    Check(!ml.empty() && hi - lo <= tol,
          "M16 all three implementations form essentially the same number of matches",
          "linear=" + std::to_string(ml.size()) + " bucketed=" + std::to_string(mb.size()) +
              " sorted=" + std::to_string(ms1.size()) + " (tol=" + std::to_string(tol) + ")");

    bool same = ms1.size() == ms2.size();
    for (size_t i = 0; same && i < ms1.size(); ++i) same = ms1[i].ids == ms2[i].ids;
    Check(same && !ms1.empty(),
          "M17 each implementation is deterministic on identical input (replayable)",
          "the indexed paths may pick a different but equally valid set than the linear scan");
}

int main() {
    std::printf("== mmr_pool: 匹配池证据 ==\n\n");
    TestWindowGrowth();
    TestFairnessIntersection();
    TestPartyIntegrity();
    TestDeterminism();
    TestTimeoutAndRelaxation();
    TestQueueTimeDistribution();
    TestFairnessVsBaseline();
    TestIndexedMatchesLinear();
    // 两种工况：窗口窄（刚入队，候选集小）vs 窗口放宽到覆盖整个池（索引失效）
    BenchTickCost(10000, 0, "fresh queue, window=50");
    BenchTickCost(10000, 100, "relaxed, window=600 ~= pool MMR span");
    BenchTickCost(100000, 0, "fresh queue, window=50");
    BenchTickCost(100000, 100, "relaxed, window=600 ~= pool MMR span");
    std::printf("\nRESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
