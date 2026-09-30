// evidence/tests/aoi-scale/src/aoi_scale.cpp
//
// AOI 与大规模场景证据（一）：可见性、Tick 成本、广播带宽与同屏容量
//   网格 AOI（cell 分桶 + 圆/方视野）在大规模下的：
//     - 每帧可见实体数分布（含热点聚集场景）
//     - 兴趣集差分产生的 enter/leave 事件数（双指针 O(M+N)）
//     - 广播带宽（进入 14B / 更新 14B / 离开 4B，含 IP+UDP 开销与 MTU 批包）
//     - 相对暴力全量扫描的耗时倍数
//
// 构建：g++ -std=c++17 -O2 -Wall -o build/aoi_scale.exe src/aoi_scale.cpp

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
    double Uniform() { return static_cast<double>(Next() >> 11) / 9007199254740992.0; }
    int Range(int lo, int hi) { return lo + static_cast<int>(Next() % static_cast<uint64_t>(hi - lo + 1)); }
};

// ---------------------------------------------------------------------------
// 世界与网格
// ---------------------------------------------------------------------------
struct Entity {
    int id = 0;
    double x = 0.0, y = 0.0;
    bool isPlayer = false;
};

struct World {
    double width = 1000.0, height = 1000.0;
    std::vector<Entity> ents;

    void Move(Rng* rng, double step) {
        for (Entity& e : ents) {
            e.x += (rng->Uniform() * 2.0 - 1.0) * step;
            e.y += (rng->Uniform() * 2.0 - 1.0) * step;
            e.x = std::max(0.0, std::min(width - 1e-6, e.x));
            e.y = std::max(0.0, std::min(height - 1e-6, e.y));
        }
    }
};

struct Grid {
    int cols = 0, rows = 0;
    double cell = 0.0;
    std::vector<std::vector<int>> cells;

    void Build(const World& w, double cellSize) {
        cell = cellSize;
        cols = static_cast<int>(std::ceil(w.width / cell));
        rows = static_cast<int>(std::ceil(w.height / cell));
        cells.assign(static_cast<size_t>(cols * rows), {});
        for (size_t i = 0; i < w.ents.size(); ++i) {
            const int cx = std::min(cols - 1, static_cast<int>(w.ents[i].x / cell));
            const int cy = std::min(rows - 1, static_cast<int>(w.ents[i].y / cell));
            cells[static_cast<size_t>(cy * cols + cx)].push_back(static_cast<int>(i));
        }
    }

    // 方视野（切比雪夫）：扫描 (2R+1)^2 个格
    void Interest(const World& w, int self, int R, double rangeSq, std::vector<int>* out) const {
        out->clear();
        const Entity& p = w.ents[static_cast<size_t>(self)];
        const int cx = std::min(cols - 1, static_cast<int>(p.x / cell));
        const int cy = std::min(rows - 1, static_cast<int>(p.y / cell));
        const int x0 = std::max(0, cx - R), x1 = std::min(cols - 1, cx + R);
        const int y0 = std::max(0, cy - R), y1 = std::min(rows - 1, cy + R);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                for (int idx : cells[static_cast<size_t>(y * cols + x)]) {
                    if (idx == self) continue;
                    const Entity& o = w.ents[static_cast<size_t>(idx)];
                    const double dx = o.x - p.x, dy = o.y - p.y;
                    if (dx * dx + dy * dy <= rangeSq) out->push_back(idx);
                }
            }
        }
    }
};

// 兴趣集差分：双指针 O(M+N)，返回 (enter, leave, update)
struct DiffResult { int enter = 0, leave = 0, update = 0; };

static DiffResult DiffSorted(const std::vector<int>& a, const std::vector<int>& b) {
    DiffResult r;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) { ++r.update; ++i; ++j; }
        else if (a[i] < b[j]) { ++r.leave; ++i; }
        else { ++r.enter; ++j; }
    }
    r.leave += static_cast<int>(a.size() - i);
    r.enter += static_cast<int>(b.size() - j);
    return r;
}

// 广播包体模型（字节）
struct Wire {
    static constexpr int kEnter = 14;    // id(4) + pos(8) + 状态(2)
    static constexpr int kUpdate = 14;
    static constexpr int kLeave = 4;     // 只需 id
    static constexpr int kIpUdpHeader = 28;
    static constexpr int kMtu = 1200;
};

static double BytesFor(const DiffResult& d) {
    const double payload = d.enter * double(Wire::kEnter) + d.update * double(Wire::kUpdate) +
                           d.leave * double(Wire::kLeave);
    const double packets = std::ceil(payload / Wire::kMtu);
    return payload + packets * Wire::kIpUdpHeader;   // 按 MTU 批包，每包一次头开销
}

// ---------------------------------------------------------------------------
// 断言组
// ---------------------------------------------------------------------------
static World MakeWorld(int n, int playerPercent, uint64_t seed, bool hotspot) {
    Rng rng(seed);
    World w;
    for (int i = 0; i < n; ++i) {
        Entity e;
        e.id = i;
        if (hotspot && i % 10 < 3) {              // 30% 挤在 100x100 热点区
            e.x = 400.0 + rng.Uniform() * 100.0;
            e.y = 400.0 + rng.Uniform() * 100.0;
        } else {
            e.x = rng.Uniform() * w.width;
            e.y = rng.Uniform() * w.height;
        }
        e.isPlayer = (rng.Range(1, 100) <= playerPercent);
        w.ents.push_back(e);
    }
    return w;
}

static void TestGridCorrectness() {
    // 网格 AOI 与暴力扫描必须给出同一个兴趣集合（只比较集合大小与内容）
    World w = MakeWorld(2000, 100, 11, false);
    Grid g;
    g.Build(w, 20.0);
    const int R = 3;
    const double rangeSq = (R * 20.0) * (R * 20.0);

    int mismatch = 0;
    for (int i = 0; i < 40; ++i) {
        std::vector<int> a;
        g.Interest(w, i, R, rangeSq, &a);
        std::vector<int> b;
        for (size_t j = 0; j < w.ents.size(); ++j) {
            if (static_cast<int>(j) == i) continue;
            const double dx = w.ents[j].x - w.ents[i].x;
            const double dy = w.ents[j].y - w.ents[i].y;
            if (dx * dx + dy * dy <= rangeSq) b.push_back(static_cast<int>(j));
        }
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b) ++mismatch;
    }
    Check(mismatch == 0,
          "A1 grid AOI returns exactly the same interest set as a brute-force radius scan",
          "checked 40 entities, mismatches=" + std::to_string(mismatch));
}

static void TestVisibilityScaling() {
    // 均匀分布下，可见数应与"密度 × 视面积"成正比，而不是与总人数成正比
    struct Row { int n; double avgVisible; };
    std::vector<Row> rows;
    for (int n : {1000, 10000, 100000}) {
        World w = MakeWorld(n, 20, 7, false);
        Grid g;
        g.Build(w, 20.0);
        const int R = 3;
        const double rangeSq = (R * 20.0) * (R * 20.0);
        double sum = 0;
        int players = 0;
        std::vector<int> tmp;
        for (size_t i = 0; i < w.ents.size(); ++i) {
            if (!w.ents[i].isPlayer) continue;
            g.Interest(w, static_cast<int>(i), R, rangeSq, &tmp);
            sum += static_cast<double>(tmp.size());
            ++players;
        }
        rows.push_back(Row{n, sum / std::max(1, players)});
    }
    std::printf("        [visibility] avg visible: N=1000 -> %.1f, N=10000 -> %.1f, N=100000 -> %.1f\n",
                rows[0].avgVisible, rows[1].avgVisible, rows[2].avgVisible);

    // 面积恒定则密度随 N 线性增长，因此可见数也线性增长（这是物理事实，不是缺陷）；
    // 关键在下面：可见数增长会直接转化为广播带宽。
    Check(rows[1].avgVisible > rows[0].avgVisible * 5.0 &&
              rows[2].avgVisible > rows[1].avgVisible * 5.0,
          "A2 with a fixed map, average visible count grows with density (linear in N)",
          "this is exactly why 同屏容量 is a bandwidth problem, not a CPU problem");
}

static void TestHotspotSpike() {
    World uniform = MakeWorld(20000, 20, 21, false);
    World hot = MakeWorld(20000, 20, 21, true);
    Grid g1, g2;
    g1.Build(uniform, 20.0);
    g2.Build(hot, 20.0);
    const int R = 3;
    const double rangeSq = (R * 20.0) * (R * 20.0);

    auto maxVisible = [&](const World& w, const Grid& g) {
        int mx = 0;
        std::vector<int> tmp;
        for (size_t i = 0; i < w.ents.size(); ++i) {
            if (!w.ents[i].isPlayer) continue;
            g.Interest(w, static_cast<int>(i), R, rangeSq, &tmp);
            mx = std::max(mx, static_cast<int>(tmp.size()));
        }
        return mx;
    };
    const int mxUniform = maxVisible(uniform, g1);
    const int mxHot = maxVisible(hot, g2);

    std::printf("        [hotspot] max visible: uniform=%d, clustered=%d (%.1fx)\n",
                mxUniform, mxHot, double(mxHot) / std::max(1, mxUniform));
    Check(mxHot > mxUniform * 3,
          "A3 a local hotspot multiplies the worst-case interest set, not the average",
          "average is a lie in large scenes; the tail is what breaks the frame");

    // 尾部分位数：平均值掩盖的问题
    std::vector<int> counts;
    std::vector<int> tmp;
    for (size_t i = 0; i < hot.ents.size(); ++i) {
        if (!hot.ents[i].isPlayer) continue;
        g2.Interest(hot, static_cast<int>(i), R, rangeSq, &tmp);
        counts.push_back(static_cast<int>(tmp.size()));
    }
    std::sort(counts.begin(), counts.end());
    auto at = [&](double q) { return counts[static_cast<size_t>(q * (counts.size() - 1))]; };
    std::printf("        [hotspot] visible p50=%d p95=%d p99=%d max=%d\n",
                at(0.50), at(0.95), at(0.99), counts.back());
    Check(at(0.99) > at(0.50) * 3,
          "A4 p99 visible count is far above the median in a clustered scene",
          "budget must be sized on p99, and AOI LOD/frequency tiers exist for this tail");
}

static void TestInterestChurn() {
    // 兴趣集差分的 enter/leave 规模：决定"抖动"成本
    World w = MakeWorld(20000, 20, 31, false);
    Grid g;
    g.Build(w, 20.0);
    const int R = 3;
    const double rangeSq = (R * 20.0) * (R * 20.0);
    Rng rng(31);

    std::vector<std::vector<int>> prev(w.ents.size());
    std::vector<std::vector<int>> cur(w.ents.size());
    std::vector<int> tmp;
    for (size_t i = 0; i < w.ents.size(); ++i)
        if (w.ents[i].isPlayer) g.Interest(w, static_cast<int>(i), R, rangeSq, &prev[i]);

    long long enterSum = 0, leaveSum = 0, updateSum = 0;
    double byteSum = 0.0;
    int players = 0;
    const int ticks = 20;
    for (int t = 0; t < ticks; ++t) {
        w.Move(&rng, 0.6);
        g.Build(w, 20.0);
        for (size_t i = 0; i < w.ents.size(); ++i) {
            if (!w.ents[i].isPlayer) continue;
            g.Interest(w, static_cast<int>(i), R, rangeSq, &tmp);
            cur[i] = tmp;
            std::sort(cur[i].begin(), cur[i].end());
            const DiffResult d = DiffSorted(prev[i], cur[i]);
            enterSum += d.enter;
            leaveSum += d.leave;
            updateSum += d.update;
            byteSum += BytesFor(d);
            prev[i] = cur[i];
            if (t == 0) ++players;
        }
    }
    const double perTickEnter = double(enterSum) / ticks;
    const double perTickUpdate = double(updateSum) / ticks;
    const double bytesPerPlayerPerTick = byteSum / ticks / std::max(1, players);
    std::printf("        [churn] per-tick per-player: enter=%.3f leave=%.3f update=%.1f  bytes=%.1f B\n",
                perTickEnter, double(leaveSum) / ticks, perTickUpdate, bytesPerPlayerPerTick);
    std::printf("        [churn] at 20 Hz that is %.1f KB/s per player (MTU-batched, IP+UDP included)\n",
                bytesPerPlayerPerTick * 20.0 / 1024.0);
    Check(enterSum > 0 && perTickEnter < perTickUpdate * 0.2,
          "A5 enter/leave churn is a small fraction of steady-state updates",
          "so bandwidth is dominated by steady-state updates, not by enter/leave spikes");
    Check(std::llabs(enterSum - leaveSum) <= static_cast<long long>(w.ents.size()),
          "A6 enter and leave counts stay balanced over time (no interest-set leakage)",
          "enter=" + std::to_string(enterSum) + " leave=" + std::to_string(leaveSum));
}

static void TestBandwidthBudget() {
    // 同屏容量：给定每玩家下行预算，反推可承受的可见实体数
    const int playersPerLine = 100;
    const int hz = 20;
    const double budgetKbpsPerPlayer = 32.0;     // 每玩家下行预算（示例）
    const double budgetBytesPerSec = budgetKbpsPerPlayer * 1024.0 / 8.0;

    // 稳态下每秒 = hz × (updates×14 + churn)
    const double bytesPerUpdatePerSec = hz * double(Wire::kUpdate);
    const double maxVisible = budgetBytesPerSec / bytesPerUpdatePerSec;

    std::printf("        [bandwidth] budget=%.1f KB/s per player -> max visible ~= %.0f entities\n",
                budgetBytesPerSec / 1024.0, maxVisible);
    Check(maxVisible > 5 && maxVisible < 60,
          "A7 a modest per-player budget implies a surprisingly small visible-entity cap",
          "cap=~" + std::to_string(static_cast<int>(maxVisible)) +
              " entities; this cap — not the CPU — is what limits 同屏人数");

    // 100 人同屏（密集战斗）在 20 Hz 下的总下行
    const int visible = 100;
    const double perPlayerKBps = hz * visible * double(Wire::kUpdate) / 1024.0;
    const double totalMBps = perPlayerKBps * playersPerLine / 1024.0;
    std::printf("        [bandwidth] visible=%d, %d players, %d Hz -> %.1f KB/s per player, %.2f MB/s per line\n",
                visible, playersPerLine, hz, perPlayerKBps, totalMBps);
    Check(totalMBps > 1.0,
          "A8 a 100-player 20 Hz melee costs megabytes per second per line",
          "which is why large fights use LOD tiers: reduce rate for far/low-priority entities");
}

static void TestVisibilityLodSavings() {
    // LOD 分层：近距全频、中距半频、远距低频，能省多少
    const int hz = 20;
    const int nNear = 10, nMid = 30, nFar = 160;
    const double fullRate = 1.0, midRate = 0.5, farRate = 0.125;

    const double flat = hz * (nNear + nMid + nFar) * double(Wire::kUpdate);
    const double tiered = hz * (nNear * fullRate + nMid * midRate + nFar * farRate) * double(Wire::kUpdate);
    const double saving = 1.0 - tiered / flat;

    std::printf("        [lod] flat=%.0f B/s -> tiered=%.0f B/s (saving=%.1f%%)\n",
                flat, tiered, saving * 100.0);
    Check(saving > 0.5,
          "A9 per-distance frequency tiers remove the majority of downstream traffic",
          "naive 'everyone at full rate' is not a viable large-scene default");
}

// ---------------------------------------------------------------------------
// 基准：网格 vs 暴力
// ---------------------------------------------------------------------------
struct Pct { double p50, p95, p99, max; };
static Pct Percentiles(std::vector<double> v) {
    if (v.empty()) return {0, 0, 0, 0};
    std::sort(v.begin(), v.end());
    auto at = [&](double q) { return v[static_cast<size_t>(q * (v.size() - 1))]; };
    return {at(0.50), at(0.95), at(0.99), v.back()};
}

static void BenchGridVsBrute(int n, bool runBrute) {
    World w = MakeWorld(n, 20, 77, false);
    const double rangeSq = (3 * 20.0) * (3 * 20.0);
    std::vector<int> tmp;

    // 网格：含每 tick 重建
    std::vector<double> gridMs;
    for (int t = 0; t < 15; ++t) {
        const auto t0 = std::chrono::steady_clock::now();
        Grid g;
        g.Build(w, 20.0);
        for (size_t i = 0; i < w.ents.size(); ++i)
            if (w.ents[i].isPlayer) g.Interest(w, static_cast<int>(i), 3, rangeSq, &tmp);
        const auto t1 = std::chrono::steady_clock::now();
        gridMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    const Pct gp = Percentiles(gridMs);

    std::printf("\n[bench] N=%d  grid_aoi(rebuild+query all players) ms p50=%.2f p95=%.2f p99=%.2f\n",
                n, gp.p50, gp.p95, gp.p99);

    if (!runBrute) {
        std::printf("[bench]   brute force skipped (P x N too large to be a fair single-tick cost)\n");
        return;
    }
    std::vector<double> bruteMs;
    for (int t = 0; t < 5; ++t) {
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t i = 0; i < w.ents.size(); ++i) {
            if (!w.ents[i].isPlayer) continue;
            const Entity& p = w.ents[i];
            for (size_t j = 0; j < w.ents.size(); ++j) {
                if (i == j) continue;
                const double dx = w.ents[j].x - p.x, dy = w.ents[j].y - p.y;
                if (dx * dx + dy * dy <= rangeSq) tmp.push_back(static_cast<int>(j));
            }
            tmp.clear();
        }
        const auto t1 = std::chrono::steady_clock::now();
        bruteMs.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    const Pct bp = Percentiles(bruteMs);
    std::printf("[bench]   brute_force ms p50=%.2f  speedup=%.1fx\n", bp.p50, bp.p50 / gp.p50);
}

int main() {
    std::printf("== aoi_scale: AOI 与大规模场景证据（可见性/成本/带宽） ==\n\n");

    TestGridCorrectness();
    TestVisibilityScaling();
    TestHotspotSpike();
    TestInterestChurn();
    TestBandwidthBudget();
    TestVisibilityLodSavings();

    std::printf("\n-- benchmark --\n");
    BenchGridVsBrute(1000, true);
    BenchGridVsBrute(10000, true);
    BenchGridVsBrute(100000, false);

    std::printf("\nRESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
