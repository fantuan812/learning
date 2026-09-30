// evidence/tests/entry-core/src/jip_resync.cpp
//
// 进入游戏 · JIP（中途加入）与断线重连状态追赶证据
//   权威快照 + 增量日志 → 客户端状态收敛；覆盖乱序、重复、日志窗口不足的降级路径；
//   并给出不同实体规模下的追赶耗时 P50/P95/P99 与吞吐。
//
// 构建：g++ -std=c++17 -O2 -o build/jip_resync.exe src/jip_resync.cpp

#include <algorithm>
#include <chrono>
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

// ---------------------------------------------------------------------------
// 世界模型
// ---------------------------------------------------------------------------
enum Field : uint8_t { kPos = 0, kHp = 1, kSpawn = 2, kDespawn = 3 };

struct Delta {
    uint64_t rev = 0;
    int id = 0;
    uint8_t field = kHp;
    double a = 0.0;   // hp / x
    double b = 0.0;   // y
};

struct Entity {
    int id = 0;
    double x = 0.0, y = 0.0;
    double hp = 0.0;
    bool alive = false;
};

struct Snapshot {
    uint64_t rev = 0;
    std::vector<Entity> ents;
};

struct World {
    std::vector<Entity> ents;
    uint64_t rev = 0;
    std::vector<Delta> log;
    size_t retainFrom = 0;   // 日志保留窗口的左边界（模拟日志截断）

    explicit World(int n = 0) {
        ents.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            ents[i].id = i;
            ents[i].x = 0.0;
            ents[i].y = 0.0;
            ents[i].hp = 100.0;
            ents[i].alive = true;
        }
        rev = 1;
    }

    void Push(const Delta& d) { log.push_back(d); }

    void SetHp(int id, double hp) {
        ++rev;
        ents[id].hp = hp;
        Push(Delta{rev, id, kHp, hp, 0.0});
    }
    void SetPos(int id, double x, double y) {
        ++rev;
        ents[id].x = x;
        ents[id].y = y;
        Push(Delta{rev, id, kPos, x, y});
    }
    void Despawn(int id) {
        ++rev;
        ents[id].alive = false;
        Push(Delta{rev, id, kDespawn, 0.0, 0.0});
    }
    void Spawn(int id) {
        ++rev;
        ents[id].alive = true;
        ents[id].hp = 100.0;
        ents[id].x = 0.0;
        ents[id].y = 0.0;
        Push(Delta{rev, id, kSpawn, 0.0, 0.0});
    }

    Snapshot TakeSnapshot() const { return Snapshot{rev, ents}; }

    // 日志窗口：丢弃 retainFrom 之前的增量
    void TruncateLogBefore(uint64_t keepFromRev) {
        size_t i = 0;
        while (i < log.size() && log[i].rev < keepFromRev) ++i;
        log.erase(log.begin(), log.begin() + static_cast<long>(i));
        retainFrom = log.empty() ? rev + 1 : log.front().rev;
    }

    // FNV-1a 状态指纹：只覆盖存活实体，按 id 升序，保证跨路径可比
    uint64_t Hash() const {
        uint64_t h = 1469598103934665603ULL;
        auto mix = [&h](uint64_t v) {
            for (int i = 0; i < 8; ++i) {
                h ^= (v >> (i * 8)) & 0xFFULL;
                h *= 1099511628211ULL;
            }
        };
        for (const Entity& e : ents) {
            if (!e.alive) continue;
            mix(static_cast<uint64_t>(e.id));
            mix(static_cast<uint64_t>(static_cast<int64_t>(e.x * 1000.0)));
            mix(static_cast<uint64_t>(static_cast<int64_t>(e.y * 1000.0)));
            mix(static_cast<uint64_t>(static_cast<int64_t>(e.hp * 1000.0)));
        }
        return h;
    }
};

// ---------------------------------------------------------------------------
// 客户端状态与追赶
// ---------------------------------------------------------------------------
struct ClientState {
    World world;      // 复用同一套结构，便于直接比哈希
    uint64_t rev = 0; // 已应用到的版本（连续前缀的右端）
    std::map<uint64_t, Delta> pending;   // 乱序重排缓冲：rev -> delta

    void ApplySnapshot(const Snapshot& s) {
        world.ents = s.ents;
        rev = s.rev;
        pending.clear();
    }

    // 无条件写入一条增量（调用方必须保证 rev == rev + 1）
    void ApplyDirect(const Delta& d) {
        if (d.id >= 0 && static_cast<size_t>(d.id) < world.ents.size()) {
            Entity& e = world.ents[static_cast<size_t>(d.id)];
            switch (d.field) {
                case kHp: e.hp = d.a; break;
                case kPos: e.x = d.a; e.y = d.b; break;
                case kSpawn: e.alive = true; e.hp = 100.0; e.x = 0.0; e.y = 0.0; break;
                case kDespawn: e.alive = false; break;
            }
        }
        rev = d.rev;
    }

    // 把缓冲里"接得上"的增量连续吐出来
    bool DrainContiguous() {
        bool progressed = false;
        for (auto it = pending.find(rev + 1); it != pending.end(); it = pending.find(rev + 1)) {
            ApplyDirect(it->second);
            pending.erase(it);
            progressed = true;
        }
        return progressed;
    }

    // 投递一条增量。乱序（中间有洞）时只缓冲、不推进版本——
    // 直接丢弃会永久丢失那次写，是 JIP 追赶最常见的状态不一致来源。
    // 返回 true 表示状态被推进。
    bool Deliver(const Delta& d) {
        if (d.rev <= rev) return false;        // 重复或过期：幂等丢弃
        if (d.rev == rev + 1) {
            ApplyDirect(d);
            DrainContiguous();
            return true;
        }
        pending[d.rev] = d;                    // 有洞：先缓冲，等缺口补齐
        return false;
    }

    size_t Buffered() const { return pending.size(); }
};

enum class CatchUpResult { kOk, kNeedFullSnapshot };

// 追赶：从快照开始，按版本升序应用窗口内的增量。
// 注意：追加式日志天然按 rev 升序，这里刻意不做排序、也不构造中间数组——
// 这两步在"每个连接只做一次"的路径上是纯开销，实测会把追赶成本放大数十倍。
CatchUpResult CatchUp(ClientState* c, const Snapshot& snap, const World& auth) {
    if (auth.log.empty()) return auth.rev == snap.rev ? CatchUpResult::kOk
                                                      : CatchUpResult::kNeedFullSnapshot;
    // 窗口左边界晚于快照版本 + 1，说明中间有缺口，增量无法补齐
    if (auth.log.front().rev > snap.rev + 1) return CatchUpResult::kNeedFullSnapshot;

    c->ApplySnapshot(snap);
    size_t i = 0;
    while (i < auth.log.size() && auth.log[i].rev <= snap.rev) ++i;   // 跳过快照已覆盖的部分
    for (; i < auth.log.size(); ++i) c->Deliver(auth.log[i]);
    return CatchUpResult::kOk;
}

// ---------------------------------------------------------------------------
// 断言组
// ---------------------------------------------------------------------------
static void TestCatchUpConvergence() {
    World auth(8);
    const Snapshot snap = auth.TakeSnapshot();

    auth.SetHp(1, 73.5);
    auth.SetPos(2, 12.0, -4.5);
    auth.SetHp(3, 0.0);
    auth.Despawn(3);
    auth.SetPos(1, 99.0, 1.0);
    auth.Spawn(5);
    auth.SetHp(5, 55.5);

    ClientState c;
    const auto r = CatchUp(&c, snap, auth);
    Check(r == CatchUpResult::kOk && c.rev == auth.rev,
          "J1 catch-up consumes the gap and lands on the authoritative revision",
          "client_rev=" + std::to_string(c.rev) + " auth_rev=" + std::to_string(auth.rev));
    Check(c.world.Hash() == auth.Hash(),
          "J2 the converged client state is byte-identical to the authority (same fingerprint)",
          "hash=" + std::to_string(c.world.Hash()));
}

static void TestIdempotentAndReordered() {
    World auth(4);
    const Snapshot snap = auth.TakeSnapshot();
    auth.SetHp(1, 10.0);
    auth.SetHp(2, 20.0);
    auth.SetHp(3, 30.0);

    // 重复投递：第二次投递必须被丢弃，但状态不变
    ClientState c;
    CatchUp(&c, snap, auth);
    const uint64_t before = c.world.Hash();
    bool changed = false;
    for (const Delta& d : auth.log) if (c.Deliver(d)) changed = true;
    Check(!changed && c.world.Hash() == before,
          "J3 replaying the whole delta log is a no-op (revision guard)",
          "no state change, hash stable");

    // 完全逆序投递：必须靠重排缓冲收敛，不能直接丢弃
    ClientState shuffled;
    shuffled.ApplySnapshot(snap);
    std::vector<Delta> reordered(auth.log.begin(), auth.log.end());
    std::reverse(reordered.begin(), reordered.end());
    for (const Delta& d : reordered) shuffled.Deliver(d);
    Check(shuffled.world.Hash() == auth.Hash() && shuffled.Buffered() == 0,
          "J4 deltas delivered in reverse order are resequenced and converge",
          "hash=" + std::to_string(shuffled.world.Hash()) +
              " buffered=" + std::to_string(shuffled.Buffered()));

    // 中间有洞：缺口未补齐前不得推进版本，否则会永久丢掉那次写
    World a2(4);
    const Snapshot s2 = a2.TakeSnapshot();
    a2.SetHp(1, 11.0);   // rev+1
    a2.SetHp(2, 22.0);   // rev+2
    a2.SetHp(3, 33.0);   // rev+3

    ClientState hole;
    hole.ApplySnapshot(s2);
    hole.Deliver(a2.log[2]);                       // 先到 rev+3
    const bool stalled = (hole.rev == s2.rev && hole.Buffered() == 1);
    hole.Deliver(a2.log[1]);                       // 再到 rev+2，仍缺 rev+1
    const bool stillStalled = (hole.rev == s2.rev && hole.Buffered() == 2);
    hole.Deliver(a2.log[0]);                       // 缺口补齐，三包一起落地
    Check(stalled && stillStalled && hole.rev == a2.rev &&
              hole.world.Hash() == a2.Hash() && hole.Buffered() == 0,
          "J5 out-of-order deltas wait in the resequencing buffer until the hole is filled",
          "stalled_then_flushed, buffered=" + std::to_string(hole.Buffered()));
}

static void TestLogWindowFallback() {
    World auth(4);
    const Snapshot snap = auth.TakeSnapshot();

    for (int i = 0; i < 10; ++i) auth.SetHp(1, 100.0 - i);

    // 只保留最后 3 条：快照与窗口之间有缺口，必须回退到全量快照
    auth.TruncateLogBefore(auth.rev - 2);
    ClientState c;
    const auto r = CatchUp(&c, snap, auth);
    Check(r == CatchUpResult::kNeedFullSnapshot,
          "J6 a snapshot older than the retained delta window requires a full snapshot",
          "log_front_rev=" + std::to_string(auth.log.front().rev) +
              " snapshot_rev=" + std::to_string(snap.rev));

    // 回退路径：重新发全量快照后必须精确收敛
    const Snapshot fresh = auth.TakeSnapshot();
    ClientState c2;
    c2.ApplySnapshot(fresh);
    Check(c2.world.Hash() == auth.Hash() && c2.rev == auth.rev,
          "J7 the full-snapshot fallback converges exactly",
          "hash=" + std::to_string(c2.world.Hash()));

    // 窗口刚好覆盖：仍然走增量路径
    World auth2(4);
    const Snapshot s3 = auth2.TakeSnapshot();
    for (int i = 0; i < 5; ++i) auth2.SetHp(1, 50.0 + i);
    ClientState c3;
    Check(CatchUp(&c3, s3, auth2) == CatchUpResult::kOk && c3.world.Hash() == auth2.Hash(),
          "J8 a gap fully covered by the window uses the incremental path");
}

static void TestLifecycleInGap() {
    World auth(6);
    const Snapshot snap = auth.TakeSnapshot();

    auth.Despawn(2);
    auth.Spawn(2);          // 同一 id 在缺口内先死后生
    auth.SetHp(2, 42.0);
    auth.Despawn(4);

    ClientState c;
    CatchUp(&c, snap, auth);
    Check(c.world.Hash() == auth.Hash(),
          "J9 spawn/despawn inside the gap is replayed with correct final state",
          "hash=" + std::to_string(c.world.Hash()));
    Check(c.world.ents[2].alive && c.world.ents[2].hp == 42.0,
          "J10 an entity that died and respawned in the gap ends up alive with the new hp",
          "hp=" + std::to_string(c.world.ents[2].hp));
    Check(!c.world.ents[4].alive,
          "J11 an entity despawned in the gap ends up gone on the client");
}

static void TestContinuesAfterCatchUp() {
    World auth(4);
    const Snapshot snap = auth.TakeSnapshot();
    auth.SetHp(1, 90.0);
    auth.SetPos(2, 5.0, 5.0);

    ClientState c;
    CatchUp(&c, snap, auth);

    // 追赶之后再来的增量必须继续生效（客户端已跟上权威版本）
    auth.SetHp(3, 33.0);
    const bool applied = c.Deliver(auth.log.back());
    Check(applied && c.world.Hash() == auth.Hash(),
          "J12 incremental updates keep working right after a catch-up",
          "hash=" + std::to_string(c.world.Hash()));
}

// ---------------------------------------------------------------------------
// 基准
// ---------------------------------------------------------------------------
struct Percentiles { double p50, p95, p99, max; };

static Percentiles Pct(std::vector<double> v) {
    if (v.empty()) return {0, 0, 0, 0};
    std::sort(v.begin(), v.end());
    auto at = [&](double p) { return v[static_cast<size_t>(p * (v.size() - 1))]; };
    return {at(0.50), at(0.95), at(0.99), v.back()};
}

static void BenchCatchUp(int entityCount, int gapDeltas, int rounds) {
    World auth(entityCount);
    const Snapshot snap = auth.TakeSnapshot();

    // 构造缺口：把 gapDeltas 次修改撒到不同实体上
    uint64_t seed = 88172645463325252ULL;
    auto next = [&seed]() { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; };
    for (int i = 0; i < gapDeltas; ++i) {
        const int id = static_cast<int>(next() % static_cast<uint64_t>(entityCount));
        if (i % 7 == 0) auth.SetPos(id, static_cast<double>(i), static_cast<double>(i) * 0.5);
        else auth.SetHp(id, 100.0 - (i % 90));
    }

    // 快照拷贝成本单独量一次（全量快照 vs 增量的对比基线）
    std::vector<double> snapshotUs;
    snapshotUs.reserve(40);
    for (int r = 0; r < 40; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        ClientState c;
        c.ApplySnapshot(snap);
        const auto t1 = std::chrono::steady_clock::now();
        snapshotUs.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    // 批量计时再归一：单次追赶可能低于时钟分辨率
    constexpr int kBatch = 20;
    std::vector<double> us;
    us.reserve(static_cast<size_t>(rounds));
    for (int r = 0; r < rounds; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < kBatch; ++b) {
            ClientState c;
            CatchUp(&c, snap, auth);
        }
        const auto t1 = std::chrono::steady_clock::now();
        us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count() / kBatch);
    }

    const Percentiles p = Pct(us);
    const Percentiles sp = Pct(snapshotUs);
    std::printf("\n[bench] catch_up entities=%d gap_deltas=%d\n", entityCount, gapDeltas);
    std::printf("[bench] catch_up_us      p50=%.3f p95=%.3f p99=%.3f max=%.3f\n",
                p.p50, p.p95, p.p99, p.max);
    std::printf("[bench] full_snapshot_us p50=%.3f p95=%.3f p99=%.3f\n", sp.p50, sp.p95, sp.p99);
    std::printf("[bench] deltas_per_sec=%.0f  entities_per_sec=%.0f\n",
                gapDeltas / (p.p50 / 1e6), entityCount / (p.p50 / 1e6));

    // 追赶 = 快照拷贝 + 重放，所以 CPU 上它永远不可能比"直接取最新快照"更便宜；
    // 增量的价值在带宽。按 Entity 32B、Delta 32B 计：
    const double snapBytes = static_cast<double>(entityCount) * 32.0;
    const double deltaBytes = static_cast<double>(gapDeltas) * 32.0;
    std::printf("[bench] bytes_snapshot=%.0f bytes_deltas=%.0f bandwidth_saving=%.2fx\n",
                snapBytes, deltaBytes, snapBytes / deltaBytes);
    std::printf("[bench] cpu_overhead_of_incremental_us=%.3f (p50 catch_up - p50 snapshot)\n",
                p.p50 - sp.p50);
}

int main() {
    std::printf("== jip_resync: 进入游戏·JIP/重连状态追赶证据 ==\n");
    std::printf("   snapshot + delta log -> client convergence\n\n");

    TestCatchUpConvergence();
    TestIdempotentAndReordered();
    TestLogWindowFallback();
    TestLifecycleInGap();
    TestContinuesAfterCatchUp();

    std::printf("\n-- benchmark --\n");
    BenchCatchUp(2000, 2000, 60);
    BenchCatchUp(20000, 20000, 40);
    BenchCatchUp(200000, 20000, 20);

    std::printf("\nRESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
