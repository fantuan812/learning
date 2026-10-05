// evidence/tests/match-core/src/mmr_pool.cpp
//
// 匹配到对局 · 匹配池证据
//   MMR 窗口随时间放宽、公平性取双方窗口交集、固定快照小队同局、未来准入、确定性、队列时间分布。
//
// 构建：g++ -std=c++17 -O2 -Wall -o build/mmr_pool.exe src/mmr_pool.cpp

#include <algorithm>
#include <array>
#include <limits>
#include <set>
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
    std::vector<int> ids;   // Historical name: values are indices into Pool::players, NOT stable IDs.
    double mmrDiff = 0.0;
    bool relaxed = false;   // spread > baseWindow diagnostic; does NOT prove time relaxation
    int64_t tick = 0;
};

struct Config {
    int teamSize = 5;
    double baseWindow = 50.0;      // 初始可接受分差（双方都要能接受）
    double growthPerTick = 4.0;    // 每 tick 增加的可接受分差
    double maxWindow = 600.0;      // 放宽上限
    int64_t maxWaitTicks = 300;    // 仅设计阈值；本模型没有超时出队、机器人或硬等待保证
    int scanLimit = 200;           // 每种策略的候选采集上限，不是访问次数/CPU硬上界
};

// Caller domain: a complete fixed snapshot, unique stable IDs, nonnegative
// int64_t queuedAt/tick with monotonically advanced tick; players.size() <= INT_MAX.
// 1 <= teamSize <= INT_MAX/2, scanLimit > 0; finite MMR, finite
// 0 <= baseWindow <= maxWindow and growthPerTick >= 0, representable arithmetic.
// Bucket mode additionally requires positive bucketWidth and floor(MMR/width),
// ceil(maxWindow/width) representable as int. No concurrent roster mutation,
// all-pair fairness, party co-team placement, optimal packing or starvation bound.
// Allocation failures/OOM transactions are outside this serial success-path model.
struct Pool {
    Config cfg;
    std::vector<Player> players;
    int64_t tick = 0;

    double WindowFor(const Player& p) const {
        const double waited = p.queuedAt > tick ? 0.0 : static_cast<double>(tick - p.queuedAt);
        return std::min(cfg.maxWindow, cfg.baseWindow + waited * cfg.growthPerTick);
    }

    // 匹配可行性：分差必须同时落在双方的窗口内（取交集）。
    // 否则长时间等待的一方会把刚入队的新玩家强行拖进一个不公平对局。
    bool Compatible(const Player& a, const Player& b) const {
        if (a.queuedAt > tick || b.queuedAt > tick) return false;
        const double diff = std::fabs(a.mmr - b.mmr);
        return diff <= std::min(WindowFor(a), WindowFor(b));
    }

    using Parties = std::map<int, std::vector<int>>;
    Parties SnapshotParties() const {
        Parties groups;
        // Deliberately includes future and already matched members. A filtered
        // waiting list cannot establish whether a visible party is complete.
        for (size_t i = 0; i < players.size(); ++i)
            if (players[i].partyId != 0)
                groups[players[i].partyId].push_back(static_cast<int>(i));
        return groups;
    }

    // Internal helper: valid indices and nonnull out. Public Step paths supply
    // their complete snapshot roster; direct tests omit it to derive the same.
    // First index remains the anchor. Only commit after a whole lobby fits.
    bool TryForm(int seedIdx, std::vector<int>& cand, std::vector<Match>* out,
                 const Parties* snapshot = nullptr) {
        Player& seed = players[static_cast<size_t>(seedIdx)];
        if (seed.matched || seed.queuedAt > tick) return false;
        const size_t capacity = static_cast<size_t>(cfg.teamSize) * 2;
        const Parties local = snapshot ? Parties{} : SnapshotParties();
        const Parties& groups = snapshot ? *snapshot : local;
        const std::set<int> available(cand.begin(), cand.end());
        std::stable_sort(cand.begin(), cand.end(), [&](int x, int y) {
            const double dx = std::fabs(players[static_cast<size_t>(x)].mmr - seed.mmr);
            const double dy = std::fabs(players[static_cast<size_t>(y)].mmr - seed.mmr);
            if (dx != dy) return dx < dy;
            return players[static_cast<size_t>(x)].id < players[static_cast<size_t>(y)].id;
        });
        std::vector<int> chosen;
        std::set<int> selected;
        auto appendGroup = [&](int idx) {
            const Player& p = players[static_cast<size_t>(idx)];
            const std::vector<int> solo{idx};
            const std::vector<int>& members = p.partyId == 0 ? solo : groups.at(p.partyId);
            if (members.size() > capacity - chosen.size()) return false;
            for (int member : members) {
                const Player& c = players[static_cast<size_t>(member)];
                if (c.matched || c.queuedAt > tick || !Compatible(seed, c) ||
                    (member != seedIdx && available.count(member) == 0) || selected.count(member))
                    return false;
            }
            // Seed-first preserves the anchor used by the reciprocal predicate.
            if (idx == seedIdx) { chosen.push_back(seedIdx); selected.insert(seedIdx); }
            for (int member : members) {
                if (member == seedIdx) continue;
                chosen.push_back(member); selected.insert(member);
            }
            return true;
        };
        if (!appendGroup(seedIdx)) return false;
        for (int idx : cand) {
            if (chosen.size() == capacity) break;
            if (selected.count(idx) == 0) appendGroup(idx);
        }
        if (chosen.size() != capacity) return false;

        double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
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
            if (!players[i].matched && players[i].queuedAt <= tick) waiting.push_back(static_cast<int>(i));
        // 等待越久越优先；同等待时长按 id 稳定排序（保证确定性）
        std::stable_sort(waiting.begin(), waiting.end(), [&](int x, int y) {
            if (players[x].queuedAt != players[y].queuedAt)
                return players[x].queuedAt < players[y].queuedAt;
            return players[x].id < players[y].id;
        });
        return waiting;
    }

    // 朴素实现：为每个种子线性扫描整个等待池。
    // 三种候选策略可产生不同合法局；旧基准不能证明普遍性能。
    std::vector<Match> StepNaive() {
        std::vector<Match> out;
        const Parties groups = SnapshotParties();
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
            TryForm(seedIdx, cand, &out, &groups);
        }
        return out;
    }

    // 分桶策略：建索引并扫描邻近桶；名册、排序和失败访问同样有成本。
    std::vector<Match> Step(int bucketWidth = 25) {
        std::vector<Match> out;
        const Parties groups = SnapshotParties();
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
            for (int64_t b = static_cast<int64_t>(b0) - span; b <= static_cast<int64_t>(b0) + span; ++b) {
                if (static_cast<int>(cand.size()) >= cfg.scanLimit) break;
                if (b < std::numeric_limits<int>::min() || b > std::numeric_limits<int>::max()) continue;
                const auto it = buckets.find(static_cast<int>(b));
                if (it == buckets.end()) continue;
                for (int idx : it->second) {
                    if (idx == seedIdx) continue;
                    const Player& c = players[static_cast<size_t>(idx)];
                    if (c.matched || !Compatible(seed, c)) continue;
                    cand.push_back(idx);
                    if (static_cast<int>(cand.size()) >= cfg.scanLimit) break;
                }
            }
            TryForm(seedIdx, cand, &out, &groups);
        }
        return out;
    }

    // 排序策略：一次性按 MMR 排序 + 二分定位；不保证比另两种更省。
    std::vector<Match> StepSorted() {
        std::vector<Match> out;
        const Parties groups = SnapshotParties();
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
            TryForm(seedIdx, cand, &out, &groups);
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
        bool has1 = false, has2 = false;
        for (int idx : m.ids) {
            has1 = has1 || p.players[static_cast<size_t>(idx)].id == 1;
            has2 = has2 || p.players[static_cast<size_t>(idx)].id == 2;
        }
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

// Input helpers require nonnegative int counts; allocation failure is not covered.
static Pool ReplayPopulation(uint64_t seed, int count) {
    Pool p;
    Rng rng(seed);
    for (int i = 0; i < count; ++i) {
        const int mmr = 1400 + rng.Range(-120, 120);
        const int64_t queued = rng.Range(0, 3);
        p.players.push_back(Mk(i + 1, mmr, queued));
    }
    return p;
}

static void TestDeterminism() {
    auto build = [](uint64_t seed) { return ReplayPopulation(seed, 200); };
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
    // 孤立高分者窗口封顶仍无法入场；独立超时出口只是设计需求，未实现
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
    Check(waited >= p.cfg.maxWaitTicks && p.cfg.maxWindow < 2000.0 && !p.players[0].matched,
          "M12 maxWaitTicks is a design threshold only; the outlier is still waiting",
          "maxWaitTicks=" + std::to_string(p.cfg.maxWaitTicks) +
              " maxWindow=" + std::to_string(static_cast<int>(p.cfg.maxWindow)));
}

static Pool QueuePopulation() {
    Pool p;
    Rng rng(2026);
    int id = 1;
    for (int i = 0; i < 1200; ++i) p.players.push_back(Mk(id++, 1400 + rng.Range(-150, 150), 0));
    for (int i = 0; i < 20; ++i) p.players.push_back(Mk(id++, 2600 + rng.Range(-40, 40), 0));

    return p;
}

struct CohortStats {
    int total = 0, matched = 0, unmatched = 0;
    bool validTimes = true;
    std::vector<int64_t> waits;
};
static CohortStats QueueCohort(const std::vector<Player>& population, bool extreme) {
    CohortStats stats;
    // Labels come from the fixed generator's IDs, never inferred from results.
    for (const Player& p : population) {
        if ((p.id > 1200) != extreme) continue;
        ++stats.total;
        if (!p.matched) { ++stats.unmatched; continue; }
        ++stats.matched;
        if (p.matchedAtTick < p.queuedAt) stats.validTimes = false;
        else stats.waits.push_back(p.matchedAtTick - p.queuedAt);
    }
    std::sort(stats.waits.begin(), stats.waits.end());
    return stats;
}
static bool CohortAccounting(const CohortStats& normal, const CohortStats& extreme) {
    return normal.total == 1200 && extreme.total == 20 && normal.validTimes && extreme.validTimes &&
        normal.matched + normal.unmatched == normal.total && extreme.matched + extreme.unmatched == extreme.total &&
        normal.waits.size() == static_cast<size_t>(normal.matched) &&
        extreme.waits.size() == static_cast<size_t>(extreme.matched);
}
static void PrintCohort(const char* label, const CohortStats& s) {
    auto q = [&](double quantile) { return s.waits.empty() ? int64_t{-1} : s.waits[static_cast<size_t>(quantile * (s.waits.size() - 1))]; };
    std::printf("        [cohort] %s total=%d matched=%d unmatched=%d p50=%lld p95=%lld p99=%lld max=%lld\n",
        label, s.total, s.matched, s.unmatched, static_cast<long long>(q(.50)),
        static_cast<long long>(q(.95)), static_cast<long long>(q(.99)), static_cast<long long>(q(1)));
}
static void TestQueueTimeDistribution() {
    Pool p = QueuePopulation();
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

    const auto normal = QueueCohort(p.players, false);
    const auto extreme = QueueCohort(p.players, true);
    PrintCohort("normal IDs1..1200", normal);
    PrintCohort("extreme IDs1201..1220", extreme);
    Check(CohortAccounting(normal, extreme),
          "M14 cohort counts and valid waits are reported without causal tail attribution",
          "labels are generator IDs; unmatched remain in the denominators");
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
            double lo = std::numeric_limits<double>::infinity(), hi = -std::numeric_limits<double>::infinity();
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
static std::array<Pool, 3> BenchmarkInputs(int poolSize, int64_t waitTicks) {
    Rng rng(4242);
    Pool canonical;
    for (int i = 0; i < poolSize; ++i) {
        const int mmr = 1400 + rng.Range(-250, 250);
        canonical.players.push_back(Mk(i + 1, mmr, 0));
    }
    canonical.tick = waitTicks;
    return {canonical, canonical, canonical};
}
static void BenchTickCost(int poolSize, int64_t waitTicks, const char* regime) {
    const auto inputs = BenchmarkInputs(poolSize, waitTicks);

    struct Row { const char* name; double us; size_t matches; };
    std::vector<Row> rows;

    auto run = [&](const char* name, int mode) {
        Pool p = inputs[static_cast<size_t>(mode)];
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
    // 下列数量接近只是固定人口的回归观测，不是任意人口合同。
    const size_t lo = std::min(ml.size(), std::min(mb.size(), ms1.size()));
    const size_t hi = std::max(ml.size(), std::max(mb.size(), ms1.size()));
    const size_t tol = std::max<size_t>(3, hi / 100);
    Check(!ml.empty() && hi - lo <= tol,
          "M16 this fixed population has similar match counts across the three heuristics",
          "linear=" + std::to_string(ml.size()) + " bucketed=" + std::to_string(mb.size()) +
              " sorted=" + std::to_string(ms1.size()) + " (tol=" + std::to_string(tol) + ")");

    bool same = ms1.size() == ms2.size();
    for (size_t i = 0; same && i < ms1.size(); ++i) same = ms1[i].ids == ms2[i].ids;
    Check(same && !ms1.empty(),
          "M17 the sorted strategy repeats its result on this identical input",
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
    // 两种工况：基础窗口50 vs 较宽窗口450；后者未覆盖500分人口跨度，也不证明索引失效
    BenchTickCost(10000, 0, "fresh queue, window=50");
    BenchTickCost(10000, 100, "relaxed, window=450; pool MMR span=500");
    BenchTickCost(100000, 0, "fresh queue, window=50");
    BenchTickCost(100000, 100, "relaxed, window=450; pool MMR span=500");
    std::printf("\nRESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
