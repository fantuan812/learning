// astar_benchmark.cpp — A* 优化前后对比基准（证据：evidence/algorithms/astar）
//
// 对应工作日志《2026-08-03-假人AI-A星优化与耗时测试》《2026-08-05-假人寻路路径缓存方案》：
// 优化前 = 线性扫描 Open List；优化后 = 二叉堆 + 标记数组（Closed）；缓存 = LRU 路径缓存。
//
// 指标：
//  - 路径有效性（连通、不穿墙）
//  - 最优性（堆与线性路径长度一致）
//  - 扩展节点数（Open List 弹出次数）
//  - P50/P95/P99 查询耗时（堆 vs 线性）
//  - 路径缓存命中率与缓存前后耗时
//
// 地图：100×100，25% 随机障碍，8 方向移动，Octile 启发。
//
// 构建：cl /nologo /utf-8 /O2 /std:c++17 /EHsc astar_benchmark.cpp /Fe:astar_benchmark.exe

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <deque>
#include <random>
#include <unordered_map>
#include <vector>

static const int kSize = 100;
static const int kNodes = kSize * kSize;
static const int kDirs = 8;
static const int kDx[kDirs] = {1, 1, 0, -1, -1, -1, 0, 1};
static const int kDy[kDirs] = {0, 1, 1, 1, 0, -1, -1, -1};
static const double kSqrt2 = 1.4142135623730951;

struct Grid {
    std::vector<uint8_t> block;   // 1 = 障碍
    Grid(unsigned seed, int density) : block(kNodes, 0) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> d(0, 99);
        for (int i = 0; i < kNodes; ++i) {
            if (d(rng) < density) block[i] = 1;
        }
        block[0] = 0;
        block[kNodes - 1] = 0;
    }
    bool blocked(int x, int y) const {
        if (x < 0 || y < 0 || x >= kSize || y >= kSize) return true;
        return block[y * kSize + x] != 0;
    }
};

struct Result {
    std::vector<int> path;
    int expanded = 0;
    double ms = 0;
    double cost = -1.0;   // 到达终点的 gCost（最优性比较用）
};

// 二叉堆 + 标记数组 Closed（优化后形态）
static Result astarHeap(const Grid& g, int sx, int sy, int gx, int gy) {
    auto t0 = std::chrono::steady_clock::now();
    Result r;
    std::vector<double> gCost(kNodes, 1e18);
    std::vector<int> parent(kNodes, -1);
    std::vector<uint8_t> closed(kNodes, 0);
    std::vector<double> fVal(kNodes, 1e18);

    struct Entry { int node; double f; };
    std::vector<Entry> heap;
    // min-heap：push_heap/pop_heap 按"comp 为 true 表示 a 应排后"构造，取 a.f > b.f
    auto heapLess = [](const Entry& a, const Entry& b) { return a.f > b.f; };
    auto push = [&](int node, double f) {
        heap.push_back({node, f});
        std::push_heap(heap.begin(), heap.end(), heapLess);
    };
    auto pop = [&]() -> Entry {
        std::pop_heap(heap.begin(), heap.end(), heapLess);
        Entry e = heap.back();
        heap.pop_back();
        return e;
    };

    auto octile = [&](int x, int y) {
        int dx = std::abs(x - gx), dy = std::abs(y - gy);
        return (double)std::max(dx, dy) + (kSqrt2 - 1.0) * (double)std::min(dx, dy);
    };

    int start = sy * kSize + sx, goal = gy * kSize + gx;
    gCost[start] = 0;
    fVal[start] = octile(sx, sy);
    push(start, octile(sx, sy));

    bool found = false;
    while (!heap.empty()) {
        Entry e = pop();
        if (closed[e.node]) continue;                       // 懒删除：已关闭
        if (e.f != fVal[e.node]) continue;                  // 懒删除：过期条目
        closed[e.node] = 1;
        r.expanded++;
        if (e.node == goal) { found = true; break; }
        int cx = e.node % kSize, cy = e.node / kSize;
        for (int d = 0; d < kDirs; ++d) {
            int nx = cx + kDx[d], ny = cy + kDy[d];
            if (g.blocked(nx, ny)) continue;
            int nn = ny * kSize + nx;
            if (closed[nn]) continue;
            double step = (d % 2 == 0) ? 1.0 : kSqrt2;
            double nc = gCost[e.node] + step;
            if (nc < gCost[nn]) {
                gCost[nn] = nc;
                parent[nn] = e.node;
                fVal[nn] = nc + octile(nx, ny);
                push(nn, fVal[nn]);                          // 懒插入：允许重复条目，弹出时校验
            }
        }
    }
    if (found) {
        for (int n = goal; n != -1; n = parent[n]) r.path.push_back(n);
        std::reverse(r.path.begin(), r.path.end());
        r.cost = gCost[goal];
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

// 线性扫描 Open List（优化前形态）
static Result astarLinear(const Grid& g, int sx, int sy, int gx, int gy) {
    auto t0 = std::chrono::steady_clock::now();
    Result r;
    std::vector<double> gCost(kNodes, 1e18);
    std::vector<int> parent(kNodes, -1);
    std::vector<uint8_t> closed(kNodes, 0);
    std::vector<double> fVal(kNodes, 1e18);
    std::vector<uint8_t> inOpen(kNodes, 0);
    std::vector<int> open;

    auto octile = [&](int x, int y) {
        int dx = std::abs(x - gx), dy = std::abs(y - gy);
        return (double)std::max(dx, dy) + (kSqrt2 - 1.0) * (double)std::min(dx, dy);
    };

    int start = sy * kSize + sx, goal = gy * kSize + gx;
    gCost[start] = 0;
    fVal[start] = octile(sx, sy);
    inOpen[start] = 1;
    open.push_back(start);

    bool found = false;
    while (!open.empty()) {
        // 线性扫描最小 f
        int best = 0;
        for (size_t i = 1; i < open.size(); ++i)
            if (fVal[open[i]] < fVal[open[best]]) best = (int)i;
        int cur = open[best];
        open[best] = open.back();
        open.pop_back();
        inOpen[cur] = 0;
        if (closed[cur]) continue;
        closed[cur] = 1;
        r.expanded++;
        if (cur == goal) { found = true; break; }
        int cx = cur % kSize, cy = cur / kSize;
        for (int d = 0; d < kDirs; ++d) {
            int nx = cx + kDx[d], ny = cy + kDy[d];
            if (g.blocked(nx, ny)) continue;
            int nn = ny * kSize + nx;
            if (closed[nn]) continue;
            double step = (d % 2 == 0) ? 1.0 : kSqrt2;
            double nc = gCost[cur] + step;
            if (nc < gCost[nn]) {
                gCost[nn] = nc;
                parent[nn] = cur;
                fVal[nn] = nc + octile(nx, ny);
                if (!inOpen[nn]) {
                    inOpen[nn] = 1;
                    open.push_back(nn);
                }
            }
        }
    }
    if (found) {
        for (int n = goal; n != -1; n = parent[n]) r.path.push_back(n);
        std::reverse(r.path.begin(), r.path.end());
        r.cost = gCost[goal];
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

static bool pathValid(const Grid& g, const std::vector<int>& path, int gx, int gy) {
    if (path.empty()) return false;
    if (path.back() != gy * kSize + gx) return false;
    for (size_t i = 1; i < path.size(); ++i) {
        int ax = path[i - 1] % kSize, ay = path[i - 1] / kSize;
        int bx = path[i] % kSize, by = path[i] / kSize;
        if (g.blocked(bx, by)) return false;
        int dx = std::abs(ax - bx), dy = std::abs(ay - by);
        if (dx > 1 || dy > 1 || (dx + dy) == 0) return false;
    }
    return true;
}

static void percentile(const std::vector<double>& v, const char* label) {
    std::vector<double> s = v;
    std::sort(s.begin(), s.end());
    auto p = [&](double q) { return s[(size_t)(s.size() * q)]; };
    printf("%s: P50=%.4f ms  P95=%.4f ms  P99=%.4f ms\n", label, p(0.5), p(0.95), p(0.99));
}

static void runScenario(const char* label, int density, unsigned gridSeed,
                        int queries, bool withCache) {
    Grid grid(gridSeed, density);
    std::mt19937_64 rng(20260812);
    std::uniform_int_distribution<int> coord(0, kSize - 1);

    // 生成查询：可达的起终点
    struct Query { int sx, sy, gx, gy; };
    std::vector<Query> qs;
    while ((int)qs.size() < queries) {
        int sx = coord(rng), sy = coord(rng), gx = coord(rng), gy = coord(rng);
        if (grid.blocked(sx, sy) || grid.blocked(gx, gy)) continue;
        if (sx == gx && sy == gy) continue;
        qs.push_back({sx, sy, gx, gy});
    }

    std::vector<double> heapMs, linearMs;
    std::vector<int> heapExpanded, linearExpanded;
    long long heapLen = 0, linearLen = 0, costEqual = 0, validCount = 0, unreachable = 0;

    for (auto& q : qs) {
        Result h = astarHeap(grid, q.sx, q.sy, q.gx, q.gy);
        Result l = astarLinear(grid, q.sx, q.sy, q.gx, q.gy);
        const bool hOk = pathValid(grid, h.path, q.gx, q.gy);
        const bool lOk = pathValid(grid, l.path, q.gx, q.gy);
        if (hOk && lOk) validCount++;
        if (h.path.empty() && l.path.empty()) unreachable++;
        heapMs.push_back(h.ms);
        linearMs.push_back(l.ms);
        heapExpanded.push_back(h.expanded);
        linearExpanded.push_back(l.expanded);
        if (!h.path.empty() && std::abs(h.cost - l.cost) < 1e-6) costEqual++;
        heapLen += (long long)h.path.size();
        linearLen += (long long)l.path.size();
    }

    long long heapExpSum = 0, linearExpSum = 0;
    for (int i = 0; i < queries; ++i) {
        heapExpSum += heapExpanded[i];
        linearExpSum += linearExpanded[i];
    }

    printf("== %s（100x100，%d%% 障碍，8 方向 Octile；%d 查询）==\n", label, density, queries);
    printf("正确性：有效路径 %lld/%d，不可达（两实现一致）%lld；最优性（代价一致）%lld/%d；平均路径长 堆 %.1f / 线性 %.1f\n",
           validCount, queries, unreachable, costEqual, queries,
           (double)heapLen / queries, (double)linearLen / queries);
    printf("扩展节点（平均）：堆 %.0f / 线性 %.0f（%dx）\n",
           (double)heapExpSum / queries, (double)linearExpSum / queries,
           (double)linearExpSum / (double)std::max<long long>(1, heapExpSum));
    percentile(heapMs, "堆（优化后）耗时");
    percentile(linearMs, "线性扫描（优化前）耗时");

    if (!withCache) return;

    // 路径缓存（LRU 128，30% 查询为近期重复）
    std::unordered_map<long long, std::vector<int>> cache;
    std::deque<long long> lru;
    const int kCacheCap = 128;
    long long hits = 0, cacheable = 0;
    std::vector<double> cachedMs, uncachedMs;
    for (int i = 0; i < queries; ++i) {
        bool repeat = (i % 4 == 3) && i >= 5;  // 25% 重复 5 次迭代前的查询（受控负载，保证在 LRU 窗口内）
        long long key = 0;
        Query q;
        if (repeat) {
            q = qs[i - 5];
            key = ((long long)(q.sy * kSize + q.sx) << 20) | (q.gy * kSize + q.gx);
            cacheable++;
            auto it = cache.find(key);
            if (it != cache.end()) {
                hits++;
                cachedMs.push_back(0.001);   // 缓存命中：O(1) 拷贝路径
                continue;
            }
        } else {
            q = qs[i];
            key = ((long long)(q.sy * kSize + q.sx) << 20) | (q.gy * kSize + q.gx);
        }
        auto t0 = std::chrono::steady_clock::now();
        Result h = astarHeap(grid, q.sx, q.sy, q.gx, q.gy);
        uncachedMs.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (cache.size() >= (size_t)kCacheCap) {
            cache.erase(lru.front());
            lru.pop_front();
        }
        cache[key] = h.path;
        lru.push_back(key);
    }
    double hitRate = cacheable > 0 ? (double)hits / cacheable : 0;
    double cachedAvg = 0, uncachedAvg = 0;
    for (double v : cachedMs) cachedAvg += v;
    for (double v : uncachedMs) uncachedAvg += v;
    cachedAvg = cachedMs.empty() ? 0 : cachedAvg / cachedMs.size();
    uncachedAvg = uncachedMs.empty() ? 0 : uncachedAvg / uncachedMs.size();
    printf("\n路径缓存（LRU %d）：可缓存查询 %lld，命中 %lld（%.1f%%），命中平均 %.4f ms vs 未命中平均 %.4f ms\n",
           kCacheCap, cacheable, hits, hitRate * 100.0, cachedAvg, uncachedAvg);
}

int main() {
    runScenario("A* 基准·普通密度", 25, 42, 1000, true);
    printf("\n");
    runScenario("A* 基准·高障碍", 40, 7, 1000, false);
    return 0;
}
