// aoi_simulator.cpp — AOI 与 Interest Management 模拟器（证据：evidence/algorithms/aoi）
//
// 模拟问题：
//  1) 网格 AOI（九宫格扩展，视野半径 R 格）在 100/1000/10000 实体下的每帧实际比较次数；
//  2) 网格 AOI 相对"切比雪夫格距"ground truth 的 Enter/Leave 正确率（应 ≈100%）；
//  3) 欧氏圆视野 vs 切比雪夫方视野的近似差异（圆方差异率，工程取舍）；
//  4) 每帧估算发送量（位置更新 + Enter/Leave）；
//  5) 网格 vs 暴力逐帧耗时（校验和防编译器消除）。
//
// 模型：200×200 格地图，实体随机游走，视野半径 R=3 格（扫描 7×7=49 格）。
//
// 构建：cl /nologo /utf-8 /O2 /std:c++17 /EHsc aoi_simulator.cpp /Fe:aoi_simulator.exe

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static const int kMapCells = 200;
static const int kViewRadius = 3;          // 视野半径（格）
static const int kCellCount = kMapCells * kMapCells;

using IntSet = std::vector<int>;           // 有序向量（生产形态的兴趣集）

// 对称差大小：|a\b| + |b\a|
static int symmetricDiff(const IntSet& a, const IntSet& b) {
    int diff = 0;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] < b[j]) { diff++; i++; }
        else if (a[i] > b[j]) { diff++; j++; }
        else { i++; j++; }
    }
    diff += (int)(a.size() - i) + (int)(b.size() - j);
    return diff;
}

struct Scenario {
    long long gridComparisons = 0;         // 网格法实际比较次数（扫描到的候选实体数）
    long long bruteComparisons = 0;        // 暴力法两两对数
    long long gridEvents = 0;              // 网格 Enter+Leave
    long long bruteEvents = 0;             // 暴力 Enter+Leave（ground truth）
    long long eventMismatch = 0;           // 网格与暴力事件不一致次数
    long long circleSquarePairs = 0;       // 欧氏圆与切比雪夫方差异对（角格误差）
    long long sends = 0;                   // 每帧发送估算（位置更新 + Enter/Leave）
    double gridMs = 0, bruteMs = 0;
    unsigned long long checksum = 0;
};

static int cellKey(int cx, int cy) { return cy * kMapCells + cx; }

static Scenario runScenario(int n, int frames, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> pos(0.0, (double)kMapCells);
    std::uniform_real_distribution<double> step(-0.6, 0.6);

    Scenario sc;
    std::vector<double> x(n), y(n);
    std::vector<int> cx(n), cy(n);
    std::vector<std::vector<int>> grid(kCellCount);

    for (int i = 0; i < n; ++i) {
        x[i] = pos(rng);
        y[i] = pos(rng);
        cx[i] = std::min(kMapCells - 1, (int)x[i]);
        cy[i] = std::min(kMapCells - 1, (int)y[i]);
        grid[cellKey(cx[i], cy[i])].push_back(i);
    }

    // 初始兴趣集合：ground truth = 切比雪夫格距 <= R（与网格 AOI 同口径，验证实现正确性）
    std::vector<IntSet> prev(n);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (std::max(std::abs(cx[i] - cx[j]), std::abs(cy[i] - cy[j])) <= kViewRadius) {
                prev[i].push_back(j);
                prev[j].push_back(i);
            }
    for (auto& s : prev) std::sort(s.begin(), s.end());

    double gridMsAcc = 0.0, bruteMsAcc = 0.0;
    for (int f = 0; f < frames; ++f) {
        auto tGrid = std::chrono::steady_clock::now();
        // 1) 移动 + 网格维护（网格路径）
        for (int i = 0; i < n; ++i) {
            x[i] = std::clamp(x[i] + step(rng), 0.0, (double)kMapCells - 0.001);
            y[i] = std::clamp(y[i] + step(rng), 0.0, (double)kMapCells - 0.001);
            int nc = std::min(kMapCells - 1, (int)x[i]);
            int nr = std::min(kMapCells - 1, (int)y[i]);
            if (nc != cx[i] || nr != cy[i]) {
                auto& old = grid[cellKey(cx[i], cy[i])];
                old.erase(std::remove(old.begin(), old.end(), i), old.end());
                cx[i] = nc;
                cy[i] = nr;
                grid[cellKey(nc, nr)].push_back(i);
            }
        }

        // 2) 网格法兴趣集（网格路径）
        std::vector<IntSet> g(n);
        for (int i = 0; i < n; ++i) {
            for (int dx = -kViewRadius; dx <= kViewRadius; ++dx) {
                for (int dy = -kViewRadius; dy <= kViewRadius; ++dy) {
                    int ccx = cx[i] + dx, ccy = cy[i] + dy;
                    if (ccx < 0 || ccy < 0 || ccx >= kMapCells || ccy >= kMapCells) continue;
                    for (int j : grid[cellKey(ccx, ccy)]) {
                        sc.gridComparisons++;
                        if (j != i) g[i].push_back(j);
                    }
                }
            }
            std::sort(g[i].begin(), g[i].end());
        }
        gridMsAcc += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tGrid).count();

        auto tBrute = std::chrono::steady_clock::now();
        // 3) 暴力法 ground truth（暴力路径）
        std::vector<IntSet> b(n);
        sc.bruteComparisons += (long long)n * (n - 1) / 2;
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                int cd = std::max(std::abs(cx[i] - cx[j]), std::abs(cy[i] - cy[j]));
                double ed = std::sqrt((x[i] - x[j]) * (x[i] - x[j]) + (y[i] - y[j]) * (y[i] - y[j]));
                if (cd <= kViewRadius) {
                    b[i].push_back(j);
                    b[j].push_back(i);
                }
                // 圆方差异：一个口径在内、另一个口径在外
                if ((cd <= kViewRadius) != (ed <= (double)kViewRadius)) {
                    sc.circleSquarePairs++;
                }
                sc.checksum += (unsigned long long)cd;
            }
        }
        for (auto& s : b) std::sort(s.begin(), s.end());
        bruteMsAcc += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tBrute).count();

        // 4) 事件对比与正确率（网格路径）
        auto tEvent = std::chrono::steady_clock::now();
        for (int i = 0; i < n; ++i) {
            sc.gridEvents += symmetricDiff(g[i], prev[i]);
            sc.bruteEvents += symmetricDiff(b[i], prev[i]);
            // 不一致 = 兴趣集本身不同（事件流不同）
            sc.eventMismatch += symmetricDiff(g[i], b[i]);
            sc.sends += (long long)g[i].size();      // 位置更新 ≈ 兴趣集大小（简化模型）
            prev[i].swap(g[i]);
        }
        gridMsAcc += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tEvent).count();
    }
    sc.gridMs = gridMsAcc / frames;
    sc.bruteMs = bruteMsAcc / frames;
    return sc;
}

int main() {
    printf("== AOI Simulator（200x200 格，视野半径 %d 格，随机游走；ground truth = 切比雪夫格距）==\n\n", kViewRadius);
    printf("%-8s | %10s | %12s | %9s | %9s | %10s | %9s | %9s\n",
           "实体数", "网格比较/帧", "暴力比较/帧", "网格事件", "暴力事件", "事件不一致", "网格ms/帧", "暴力ms/帧");
    printf("-----------------------------------------------------------------------------------------------\n");
    struct Case { int n; int frames; unsigned seed; };
    std::vector<Case> cases = {
        {100, 200, 1}, {1000, 100, 2}, {10000, 20, 3}
    };
    for (auto& c : cases) {
        Scenario s = runScenario(c.n, c.frames, c.seed);
        double correct = s.bruteEvents > 0
            ? (1.0 - (double)s.eventMismatch / (double)(s.bruteEvents + s.gridEvents)) * 100.0
            : 100.0;
        printf("%-8d | %10lld | %12lld | %9lld | %9lld | %10lld | %9.3f | %9.3f\n",
               c.n, s.gridComparisons / c.frames, s.bruteComparisons / c.frames,
               s.gridEvents, s.bruteEvents, s.eventMismatch, s.gridMs, s.bruteMs);
        printf("   正确率≈%.2f%%（事件口径），发送估算/帧=%lld，圆方差异对/帧=%lld，暴力/网格耗时≈%.0fx，校验和=%llu\n",
               correct, s.sends / c.frames, s.circleSquarePairs / c.frames,
               s.bruteMs / (s.gridMs > 0 ? s.gridMs : 1e-9), s.checksum);
    }
    return 0;
}
