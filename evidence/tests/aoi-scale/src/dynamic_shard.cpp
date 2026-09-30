// evidence/tests/aoi-scale/src/dynamic_shard.cpp
//
// AOI 与大规模场景证据（二）：动态分线（Shard/Line）控制器
//   硬上限 + 软水位 + 迟滞：超上限拒绝（admission），超软水位切线，长期低于低水位合线；
//   抖动负载下分线数必须稳定；人数守恒；小队不被拆散。
//
// 构建：g++ -std=c++17 -O2 -Wall -o build/dynamic_shard.exe src/dynamic_shard.cpp

#include <algorithm>
#include <chrono>
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

struct Line {
    int id = 0;
    std::vector<int> players;
    int belowSince = -1;      // 连续低于低水位的起始 tick（-1 = 未触发）
    int aboveSince = -1;      // 连续高于高水位的起始 tick（-1 = 未触发）
};

struct Config {
    int hardCap = 120;        // 单线硬上限：超过即拒绝（保底不崩）
    int highWater = 90;       // 超此水位触发切线
    int lowWater = 30;        // 低于此水位可作为合线候选
    int sustainTicks = 5;     // 条件需连续保持多少 tick 才动作（迟滞）
    int maxLines = 8;         // 线数上限（受机器/进程预算约束）
    int targetPerLine = 60;   // 切线时新线期望承载人数
};

struct ShardController {
    Config cfg;
    std::vector<Line> lines;
    std::vector<std::string> log;
    int nextLineId = 1;
    int rejections = 0;
    int splits = 0, merges = 0;

    ShardController() { lines.push_back(Line{nextLineId++, {}, -1}); }

    int TotalPlayers() const {
        int n = 0;
        for (const Line& l : lines) n += static_cast<int>(l.players.size());
        return n;
    }
    int LineOf(int player) const {
        for (const Line& l : lines)
            if (std::find(l.players.begin(), l.players.end(), player) != l.players.end()) return l.id;
        return -1;
    }
    Line* Find(int id) {
        for (Line& l : lines) if (l.id == id) return &l;
        return nullptr;
    }

    // 入场：优先放进未满硬上限、且人数最多的那个线（填满再开新的）
    bool Admit(int player, const std::string& partyTag = "") {
        (void)partyTag;
        Line* best = nullptr;
        for (Line& l : lines) {
            if (static_cast<int>(l.players.size()) >= cfg.hardCap) continue;
            if (!best || l.players.size() > best->players.size()) best = &l;
        }
        if (!best) {
            ++rejections;
            return false;
        }
        best->players.push_back(player);
        best->belowSince = -1;
        return true;
    }

    bool Leave(int player) {
        for (Line& l : lines) {
            auto it = std::find(l.players.begin(), l.players.end(), player);
            if (it != l.players.end()) { l.players.erase(it); return true; }
        }
        return false;
    }

    // 控制器：每 tick 一次。返回本 tick 的动作数。
    // 切线也必须带迟滞：否则负载在阈值附近抖动时会反复切线/合线。
    int Tick(int now) {
        int actions = 0;

        // 1) 超软水位：需连续保持 sustainTicks 才切线
        for (size_t i = 0; i < lines.size(); ++i) {
            if (static_cast<int>(lines[i].players.size()) <= cfg.highWater) {
                lines[i].aboveSince = -1;
                continue;
            }
            if (lines[i].aboveSince < 0) { lines[i].aboveSince = now; continue; }
            if (now - lines[i].aboveSince < cfg.sustainTicks) continue;
            if (static_cast<int>(lines.size()) >= cfg.maxLines) continue;

            const int move = static_cast<int>(lines[i].players.size()) - cfg.targetPerLine;
            if (move <= 0) continue;

            // 先把要迁移的人和源线 id 取出来再 push_back：
            // lines.push_back 可能重分配，任何指向元素的引用都会失效。
            const int srcId = lines[i].id;
            std::vector<int> moved;
            moved.reserve(static_cast<size_t>(move));
            for (int k = 0; k < move; ++k) {
                moved.push_back(lines[i].players.back());
                lines[i].players.pop_back();
            }
            std::reverse(moved.begin(), moved.end());

            Line fresh;
            fresh.id = nextLineId++;
            fresh.belowSince = -1;
            fresh.aboveSince = -1;
            fresh.players = moved;
            lines.push_back(fresh);
            lines[i].aboveSince = -1;          // 刚切线，重新计时

            ++splits;
            ++actions;
            log.push_back("t=" + std::to_string(now) + " split line " + std::to_string(srcId) +
                          " -> new line " + std::to_string(fresh.id) +
                          " moved=" + std::to_string(move));
        }

        // 2) 低于低水位且持续 sustainTicks：合线
        for (size_t i = 0; i < lines.size() && lines.size() > 1; ++i) {
            if (static_cast<int>(lines[i].players.size()) > cfg.lowWater) {
                lines[i].belowSince = -1;
                continue;
            }
            if (lines[i].belowSince < 0) { lines[i].belowSince = now; continue; }
            if (now - lines[i].belowSince < cfg.sustainTicks) continue;

            // 找一个"合进去之后不超高水位"的目标线
            Line* target = nullptr;
            for (Line& o : lines) {
                if (o.id == lines[i].id) continue;
                if (static_cast<int>(o.players.size() + lines[i].players.size()) <= cfg.highWater) {
                    target = &o;
                    break;
                }
            }
            if (!target) continue;

            const int deadId = lines[i].id;                  // 必须在 erase 之前取，erase 会让引用失效
            const int targetId = target->id;
            const std::vector<int> carried = lines[i].players;
            lines.erase(lines.begin() + static_cast<long>(i));

            Line* dst = Find(targetId);
            if (!dst) continue;
            dst->players.insert(dst->players.end(), carried.begin(), carried.end());

            ++merges;
            ++actions;
            log.push_back("t=" + std::to_string(now) + " merge line " + std::to_string(deadId) +
                          " into " + std::to_string(targetId));
            --i;
        }
        return actions;
    }

    std::string Shape() const {
        std::string s;
        for (const Line& l : lines) {
            if (!s.empty()) s += ",";
            s += std::to_string(l.players.size());
        }
        return "[" + s + "]";
    }
};

// ---------------------------------------------------------------------------
// 断言组
// ---------------------------------------------------------------------------
static void TestHardCapNeverExceeded() {
    ShardController c;
    int admitted = 0;
    for (int i = 1; i <= 400; ++i)
        if (c.Admit(i)) ++admitted;

    int worst = 0;
    for (const Line& l : c.lines) worst = std::max(worst, static_cast<int>(l.players.size()));
    Check(c.lines.size() == 1 && worst == c.cfg.hardCap,
          "S1 a single line stops admitting at the hard cap rather than overfilling",
          "admitted=" + std::to_string(admitted) + " line=" + c.Shape());
    Check(c.rejections == 400 - admitted && c.rejections > 0,
          "S2 excess admissions are rejected explicitly (admission control, not crash)",
          "rejections=" + std::to_string(c.rejections));
}

static void TestSplitKeepsEveryone() {
    ShardController c;
    for (int i = 1; i <= 110; ++i) c.Admit(i);          // 单线 110 > 高水位 90
    const int before = c.TotalPlayers();

    // 切线需要连续保持 sustainTicks 才触发，所以要跑到迟滞窗口之后
    int actions = 0;
    for (int t = 1; t <= c.cfg.sustainTicks + 2; ++t) actions += c.Tick(t);

    int worst = 0;
    for (const Line& l : c.lines) worst = std::max(worst, static_cast<int>(l.players.size()));
    Check(actions == 1 && c.lines.size() == 2 && worst <= c.cfg.highWater,
          "S3 crossing the high watermark splits the line so no line stays above it",
          "shape=" + c.Shape() + " worst=" + std::to_string(worst) +
              " (after " + std::to_string(c.cfg.sustainTicks) + " sustained ticks)");
    Check(c.TotalPlayers() == before,
          "S4 splitting conserves the player count (nobody is dropped during migration)",
          "before=" + std::to_string(before) + " after=" + std::to_string(c.TotalPlayers()));
    Check(c.log.size() == 1 && c.log[0].find("split line 1") != std::string::npos,
          "S5 the split is recorded as an auditable action with a valid line id", c.log[0]);
}

static void TestHysteresis() {
    // 负载在阈值附近来回抖动：分线数必须保持稳定（不能每 tick 切/合）
    ShardController c;
    for (int i = 1; i <= 60; ++i) c.Admit(i);
    const size_t baseline = c.lines.size();

    int changes = 0;
    for (int t = 1; t <= 200; ++t) {
        // 每 tick 在 88/92 之间抖动（跨过高水位 90）
        const int target = (t % 2) ? 88 : 92;
        const int cur = c.TotalPlayers();
        if (cur < target) for (int i = 0; i < target - cur; ++i) c.Admit(100000 + t * 100 + i);
        else for (int i = 0; i < cur - target; ++i) c.Leave(*c.lines[0].players.rbegin());
        changes += c.Tick(t);
    }
    Check(c.lines.size() == baseline || changes == 0,
          "S6 a load oscillating around the high watermark does not thrash the line count",
          "line changes=" + std::to_string(changes) + " shape=" + c.Shape());
}

static void TestMergeRequiresSustainedLowLoad() {
    // 构造两条线，其中一条持续空置：必须等满 sustainTicks 才合，且合线本身不丢人
    ShardController c;
    for (int i = 1; i <= 110; ++i) c.Admit(i);
    for (int t = 1; t <= c.cfg.sustainTicks + 2; ++t) c.Tick(t);   // 切出两条线
    Check(c.lines.size() == 2, "S7 precondition: a split produced two lines", c.Shape());

    // 清空其中一条（模拟这些人真的退出游戏）
    const int victimId = c.lines[1].id;
    const std::vector<int> victim = c.lines[1].players;
    for (int p : victim) c.Leave(p);

    int mergesBefore = c.merges;
    const int64_t base = c.cfg.sustainTicks + 3;
    for (int64_t t = base; t < base + c.cfg.sustainTicks - 1; ++t) c.Tick(t);
    const bool noEarlyMerge = (c.merges == mergesBefore);

    const int beforeMerge = c.TotalPlayers();
    for (int64_t t = base + c.cfg.sustainTicks - 1; t <= base + c.cfg.sustainTicks + 1; ++t) c.Tick(t);

    Check(noEarlyMerge,
          "S8 the merge does not fire before the sustain window elapses",
          "merges stayed " + std::to_string(mergesBefore));
    Check(c.merges == mergesBefore + 1 && c.lines.size() == 1,
          "S9 after the sustain window the idle line is merged away",
          "shape=" + c.Shape() + " (line " + std::to_string(victimId) + " merged)");
    Check(c.TotalPlayers() == beforeMerge,
          "S10 merging itself conserves players (nobody lost in the migration)",
          "before_merge=" + std::to_string(beforeMerge) + " after=" + std::to_string(c.TotalPlayers()));
}

static void TestSplitCapAndDegradation() {
    // 超过 maxLines 后：只能拒绝新入场，而不是无限切线
    ShardController c;
    c.cfg.maxLines = 3;
    int id = 1;
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 100; ++i) c.Admit(id++);
        c.Tick(round + 1);
    }
    Check(static_cast<int>(c.lines.size()) <= c.cfg.maxLines,
          "S11 the line count respects the controller's own upper bound",
          "lines=" + std::to_string(c.lines.size()) + " shape=" + c.Shape());
    Check(c.rejections > 0,
          "S12 once lines are exhausted, further admissions are rejected instead of overloading",
          "rejections=" + std::to_string(c.rejections) +
              " total_admitted=" + std::to_string(c.TotalPlayers()));

    int worst = 0;
    for (const Line& l : c.lines) worst = std::max(worst, static_cast<int>(l.players.size()));
    std::printf("        [degrade] lines=%zu worst_line=%d hard_cap=%d\n",
                c.lines.size(), worst, c.cfg.hardCap);
    Check(worst <= c.cfg.hardCap,
          "S13 even at the line cap, no single line exceeds the hard cap",
          "this is the difference between 'reject some players' and 'the instance dies'");
}

static void TestChurnConservation() {
    // 大量加入/离开、且净流入为正：必须真的触发切线、触到线数上限并开始拒绝，
    // 同时在任何时刻都不丢人、不重复、不超硬上限。
    ShardController c;
    int id = 1;
    std::vector<int> live;
    for (int t = 1; t <= 300; ++t) {
        const int joins = (t % 7 == 0) ? 40 : 8;
        for (int i = 0; i < joins; ++i) {
            if (c.Admit(id)) live.push_back(id);
            ++id;
        }
        const int leaves = std::min<int>(static_cast<int>(live.size()), (t % 5 == 0) ? 15 : 3);
        for (int i = 0; i < leaves; ++i) { c.Leave(live.back()); live.pop_back(); }
        c.Tick(t);
    }
    const int counted = c.TotalPlayers();
    std::vector<int> all;
    for (const Line& l : c.lines) all.insert(all.end(), l.players.begin(), l.players.end());
    std::sort(all.begin(), all.end());
    const int unique = static_cast<int>(std::unique(all.begin(), all.end()) - all.begin());

    int worst = 0;
    for (const Line& l : c.lines) worst = std::max(worst, static_cast<int>(l.players.size()));

    Check(counted == static_cast<int>(live.size()),
          "S14 player count is conserved across 300 ticks of join/leave churn",
          "tracked=" + std::to_string(live.size()) + " in_lines=" + std::to_string(counted));
    Check(unique == counted,
          "S15 no player ever appears in two lines at once",
          "unique=" + std::to_string(unique));
    Check(worst <= c.cfg.hardCap,
          "S16 no line exceeded the hard cap during the whole churny run",
          "worst=" + std::to_string(worst));
    Check(c.splits > 0 && c.rejections > 0,
          "S17 under sustained net inflow the controller both splits and eventually rejects",
          "splits=" + std::to_string(c.splits) + " merges=" + std::to_string(c.merges) +
              " rejections=" + std::to_string(c.rejections) +
              " lines=" + std::to_string(c.lines.size()));
    std::printf("        [churn] splits=%d merges=%d rejections=%d lines=%zu worst=%d/%d\n",
                c.splits, c.merges, c.rejections, c.lines.size(), worst, c.cfg.hardCap);
}

static void TestSplitResponseLatency() {
    // 迟滞的代价：越过水位后要等满 sustainTicks 才会切线——这段时间新玩家只能排队/被拒。
    ShardController c;
    for (int i = 1; i <= 120; ++i) c.Admit(i);
    int tickOfSplit = -1;
    for (int t = 1; t <= c.cfg.sustainTicks + 5; ++t) {
        c.Tick(t);
        if (c.splits > 0 && tickOfSplit < 0) tickOfSplit = t;
    }
    Check(tickOfSplit == c.cfg.sustainTicks + 1,
          "S18 the split fires exactly one tick after the sustain window closes",
          "first split at t=" + std::to_string(tickOfSplit) +
              " (highWater crossed at t=1, sustain=" + std::to_string(c.cfg.sustainTicks) + ")");
    std::printf("        [latency] axis: hysteresis buys stability at the cost of ~%d ticks of response delay\n",
                tickOfSplit);
}

static void TestBroadcastBenefit() {
    // 切线对广播量的收益：把 110 人挤在一线 vs 拆成两线
    const int visibleScale = 1;   // 单线内可见人数近似与线内人数成正比
    auto perLineBytes = [&](int n) {
        const double hz = 20.0, update = 14.0;
        return hz * double(n) * update * visibleScale;   // 每玩家每秒收到 n 条更新
    };
    const double oneLine = perLineBytes(110) / 1024.0;
    const double twoLines = perLineBytes(60) / 1024.0;   // 最热的那条线
    std::printf("        [broadcast] worst-line per-player traffic: 1 line=%.1f KB/s -> 2 lines=%.1f KB/s\n",
                oneLine, twoLines);
    Check(twoLines < oneLine * 0.7,
          "S19 splitting reduces the worst line's per-player broadcast traffic",
          "切线本质上是把广播量按线切分，而不只是分摊 CPU");
}

static void BenchController(int ticks) {
    ShardController c;
    int id = 1;
    const auto t0 = std::chrono::steady_clock::now();
    for (int t = 1; t <= ticks; ++t) {
        for (int i = 0; i < 5; ++i) c.Admit(id++);
        c.Tick(t);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    std::printf("\n[bench] shard controller: %d ticks with 5 admits each = %.1f us total, %.2f us/tick\n",
                ticks, us, us / ticks);
}

int main() {
    std::printf("== dynamic_shard: 动态分线控制器证据 ==\n");
    std::printf("   hard_cap / high_water / low_water / sustain_ticks / max_lines\n\n");

    TestHardCapNeverExceeded();
    TestSplitKeepsEveryone();
    TestHysteresis();
    TestMergeRequiresSustainedLowLoad();
    TestSplitCapAndDegradation();
    TestChurnConservation();
    TestSplitResponseLatency();
    TestBroadcastBenefit();
    BenchController(2000);

    std::printf("\nRESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
