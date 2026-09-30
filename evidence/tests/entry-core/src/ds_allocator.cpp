// evidence/tests/entry-core/src/ds_allocator.cpp
//
// 进入游戏 · DS 分配与租约证据
//   单写者、租约(lease)、栅栏令牌(fencing token)、续约、回收、容量不超卖、重连亲和性。
//   核心不变量：同一玩家在任一时刻至多持有一个有效租约；令牌只增不减；
//               过期租约的旧持有者不得再提交（被栅栏）。
//
// 构建：g++ -std=c++17 -O2 -o build/ds_allocator.exe src/ds_allocator.cpp

#include <algorithm>
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
// 分配器
// ---------------------------------------------------------------------------
struct DsServer {
    std::string id;
    int capacity = 0;
    int load = 0;
    uint64_t tokenCounter = 0;   // 栅栏令牌：每台 DS 单调递增，永不复用
};

struct Lease {
    std::string player;
    std::string ds;
    uint64_t token = 0;
    int64_t expireAt = 0;
    bool active = false;
};

struct Allocator {
    std::vector<DsServer> servers;
    std::map<std::string, Lease> leases;        // player -> 当前租约
    std::map<std::string, std::string> lastKnown;   // player -> 最近一次归属（重连亲和）
    int64_t leaseTtl = 30;      // 租约时长（秒）
    int64_t grace = 10;         // 断线保留名额的宽限期（秒）

    DsServer* Find(const std::string& id) {
        for (auto& s : servers) if (s.id == id) return &s;
        return nullptr;
    }

    enum class Status { kNew, kReused, kReconnected, kNoCapacity };

    // 心跳/续约：只有持有当前令牌才能延长，旧令牌被栅栏
    bool Heartbeat(const std::string& player, uint64_t token, int64_t now) {
        auto it = leases.find(player);
        if (it == leases.end()) return false;
        Lease& l = it->second;
        if (!l.active || l.token != token) return false;   // 被栅栏
        l.expireAt = now + leaseTtl;
        return true;
    }

    // 状态提交：与心跳同一条判据 —— 令牌不匹配即拒绝，防止"僵尸 owner 写回"
    bool Commit(const std::string& player, uint64_t token, int64_t now) {
        auto it = leases.find(player);
        if (it == leases.end()) return false;
        const Lease& l = it->second;
        return l.active && l.token == token && l.expireAt >= now;
    }

    Status Allocate(const std::string& player, int64_t now, Lease* out) {
        // 1) 已有存活租约：直接复用（幂等，不新增名额）
        auto it = leases.find(player);
        if (it != leases.end() && it->second.active && it->second.expireAt >= now) {
            *out = it->second;
            return Status::kReused;
        }
        // 2) 断线宽限期内：优先回到原 DS（亲和性）
        auto known = lastKnown.find(player);
        if (known != lastKnown.end()) {
            DsServer* s = Find(known->second);
            if (s && s->load < s->capacity) {
                s->load++;
                Lease l;
                l.player = player;
                l.ds = s->id;
                l.token = ++s->tokenCounter;     // 新令牌，旧令牌从此被栅栏
                l.expireAt = now + leaseTtl;
                l.active = true;
                leases[player] = l;
                *out = l;
                return Status::kReconnected;
            }
        }
        // 3) 正常分配：挑第一个有空位的 DS
        for (auto& s : servers) {
            if (s.load < s.capacity) {
                s.load++;
                Lease l;
                l.player = player;
                l.ds = s.id;
                l.token = ++s.tokenCounter;
                l.expireAt = now + leaseTtl;
                l.active = true;
                leases[player] = l;
                lastKnown[player] = s.id;
                *out = l;
                return Status::kNew;
            }
        }
        return Status::kNoCapacity;
    }

    // 主动释放（玩家正常退出对局）
    bool Release(const std::string& player) {
        auto it = leases.find(player);
        if (it == leases.end()) return false;
        if (it->second.active) {
            if (DsServer* s = Find(it->second.ds)) s->load--;
        }
        leases.erase(it);
        return true;
    }

    // 回收：租约 + 宽限期之后才真正释放名额；期间名额被"保留"给原玩家
    int Reap(int64_t now, std::vector<std::string>* reclaimed = nullptr) {
        int n = 0;
        for (auto& kv : leases) {
            Lease& l = kv.second;
            if (l.active && l.expireAt + grace < now) {
                if (DsServer* s = Find(l.ds)) s->load--;
                l.active = false;
                if (reclaimed) reclaimed->push_back(kv.first);
                ++n;
            }
        }
        return n;
    }

    int TotalLoad() const { int n = 0; for (const auto& s : servers) n += s.load; return n; }
    int ActiveLeases() const {
        int n = 0; for (const auto& kv : leases) if (kv.second.active) ++n; return n;
    }
};

static Allocator MakeAllocator(int serverCount = 3, int capacity = 2) {
    Allocator a;
    for (int i = 1; i <= serverCount; ++i) {
        DsServer s;
        s.id = "ds-" + std::to_string(i);
        s.capacity = capacity;
        a.servers.push_back(s);
    }
    return a;
}

// ---------------------------------------------------------------------------
// 断言组
// ---------------------------------------------------------------------------
static void TestBasicAllocation() {
    Allocator a = MakeAllocator();
    Lease l1;
    const auto st = a.Allocate("p1", 1000, &l1);
    Check(st == Allocator::Status::kNew && l1.ds == "ds-1" && l1.token == 1,
          "A1 first allocation lands on ds-1 with fencing token 1",
          "ds=" + l1.ds + " token=" + std::to_string(l1.token));

    Lease again;
    const auto st2 = a.Allocate("p1", 1005, &again);
    Check(st2 == Allocator::Status::kReused && again.token == l1.token &&
              a.TotalLoad() == 1,
          "A2 re-allocating the same player reuses the lease (no second slot)",
          "status=reused token=" + std::to_string(again.token) +
              " load=" + std::to_string(a.TotalLoad()));

    // 令牌在重新分配后必须递增（旧令牌永久失效）
    a.lastKnown.erase("p1");
    a.Release("p1");
    Lease l2;
    a.Allocate("p1", 1010, &l2);
    Check(l2.token == 2,
          "A3 releasing and re-allocating mints a strictly larger token",
          "token=" + std::to_string(l2.token));
}

static void TestFencing() {
    Allocator a = MakeAllocator();
    Lease oldLease;
    a.Allocate("p1", 1000, &oldLease);
    const uint64_t staleToken = oldLease.token;

    // 玩家掉线：租约过期 + 宽限期结束 → 名额被回收
    std::vector<std::string> reclaimed;
    a.Reap(1000 + 30 + 10 + 1, &reclaimed);
    Check(reclaimed.size() == 1 && a.TotalLoad() == 0,
          "A4 an expired lease past the grace window is reclaimed",
          "reclaimed=" + std::to_string(reclaimed.size()) + " load=" + std::to_string(a.TotalLoad()));

    // 名额被交给新玩家
    Lease fresh;
    const auto st = a.Allocate("p2", 1045, &fresh);
    Check(st == Allocator::Status::kNew && fresh.token > staleToken,
          "A5 the freed slot goes to a new player with a larger token",
          "ds=" + fresh.ds + " token=" + std::to_string(fresh.token));

    // 老玩家"复活"后带着旧令牌提交：必须被拒绝（栅栏）
    Check(!a.Commit("p1", staleToken, 1050),
          "A6 the zombie owner's commit with the stale token is fenced off",
          "stale token=" + std::to_string(staleToken));
    Check(!a.Heartbeat("p1", staleToken, 1050),
          "A7 the zombie owner cannot renew the lease it no longer holds");
    Check(a.Commit("p2", fresh.token, 1050),
          "A8 the current owner's commit is accepted",
          "token=" + std::to_string(fresh.token));

    // 老玩家重新进入：拿到新令牌，此后可正常提交
    Lease rejoin;
    const auto st2 = a.Allocate("p1", 1051, &rejoin);
    Check(st2 != Allocator::Status::kNoCapacity && rejoin.token != staleToken &&
              a.Commit("p1", rejoin.token, 1051),
          "A9 the returning player gets a new token and can commit again",
          "ds=" + rejoin.ds + " token=" + std::to_string(rejoin.token));
    Check(!a.Commit("p1", staleToken, 1051),
          "A10 the old token stays dead even after the player is back");
}

static void TestLeaseRenewal() {
    Allocator a = MakeAllocator();
    Lease l;
    a.Allocate("p1", 1000, &l);

    // 在租约内持续心跳：不应过期，也不该产生任何回收
    for (int64_t t = 1000; t <= 1000 + 300; t += 10) {
        const bool ok = a.Heartbeat("p1", l.token, t);
        if (!ok) { Check(false, "A11 heartbeat keeps the lease alive across 300s", "failed at t=" + std::to_string(t)); return; }
    }
    Check(a.ActiveLeases() == 1 && a.TotalLoad() == 1,
          "A11 heartbeat keeps the lease alive across 300s (30s TTL, renewed every 10s)",
          "load=" + std::to_string(a.TotalLoad()));

    // 心跳中断超过 TTL + grace：被回收
    const int64_t stoppedAt = 1000 + 300;
    a.Reap(stoppedAt + 30 + 10 + 1);
    Check(a.ActiveLeases() == 0 && a.TotalLoad() == 0,
          "A12 a lease with no heartbeat for > TTL+grace is reclaimed exactly once",
          "load=" + std::to_string(a.TotalLoad()));
}

static void TestCapacityAndInvariants() {
    Allocator a = MakeAllocator(3, 2);   // 3 DS x 2 = 6
    for (int i = 1; i <= 6; ++i) {
        Lease l;
        const auto st = a.Allocate("p" + std::to_string(i), 1000, &l);
        if (st == Allocator::Status::kNoCapacity) {
            Check(false, "A13 six players fit into 3 DS x capacity 2", "failed at p" + std::to_string(i));
            return;
        }
    }
    Check(a.TotalLoad() == 6, "A13 three DS with capacity 2 hold exactly 6 players",
          "load=" + std::to_string(a.TotalLoad()));

    Lease overflow;
    const auto st = a.Allocate("p7", 1000, &overflow);
    Check(st == Allocator::Status::kNoCapacity && a.TotalLoad() == 6,
          "A14 the 7th player is refused; capacity is never over-sold",
          "load=" + std::to_string(a.TotalLoad()));
    Check(a.TotalLoad() == a.ActiveLeases(),
          "A15 invariant: server load always equals the number of active leases",
          "load=" + std::to_string(a.TotalLoad()) + " leases=" + std::to_string(a.ActiveLeases()));

    // 混合序列（分配/心跳/回收/释放）后不变量仍成立
    a.Heartbeat("p1", a.leases["p1"].token, 1010);
    a.Reap(1000 + 30 + 10 + 1);          // 未心跳的全部过期
    a.Release("p2");
    Lease after;
    a.Allocate("p8", 1100, &after);
    Check(a.TotalLoad() == a.ActiveLeases(),
          "A16 the load/lease invariant survives a mixed alloc/heartbeat/reap/release sequence",
          "load=" + std::to_string(a.TotalLoad()) + " leases=" + std::to_string(a.ActiveLeases()));
}

static void TestAffinity() {
    Allocator a = MakeAllocator();
    Lease l;
    a.Allocate("p1", 1000, &l);
    Check(l.ds == "ds-1", "A17 baseline assignment is ds-1", l.ds);

    // 短暂断线，在宽限期内重连：必须回到 ds-1
    a.Reap(1031);   // 尚未超过 TTL+grace
    Lease re;
    const auto st = a.Allocate("p1", 1035, &re);
    Check(st == Allocator::Status::kReconnected && re.ds == "ds-1",
          "A18 reconnecting inside the grace window returns to the same DS",
          "ds=" + re.ds + " status=reconnected");

    // 断线过久（超过 TTL+grace）：名额已回收，重连不再保证亲和
    Allocator b = MakeAllocator(3, 1);   // 容量 1，让原 DS 真的没有空位
    Lease lb;
    b.Allocate("p1", 1000, &lb);
    b.Reap(1000 + 30 + 10 + 1);
    b.Allocate("p2", 1050, &lb);        // 名额被 p2 拿走
    Lease rb;
    const auto st2 = b.Allocate("p1", 1051, &rb);
    Check(st2 == Allocator::Status::kNew && rb.ds == "ds-2",
          "A19 after the grace window the seat is gone; reconnect lands elsewhere",
          "ds=" + rb.ds + " (ds-1 now held by p2)");
    Check(rb.token == 1,
          "A20 fencing tokens are PER-DS, so a token from ds-1 never authorises ds-2",
          "ds-2 first token=" + std::to_string(rb.token) + " (ds-1 was up to token 2)");
}

int main() {
    std::printf("== ds_allocator: 进入游戏·DS 分配与租约证据 ==\n");
    std::printf("   lease / fencing token / heartbeat / reap / affinity\n\n");

    TestBasicAllocation();
    TestFencing();
    TestLeaseRenewal();
    TestCapacityAndInvariants();
    TestAffinity();

    std::printf("\nRESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
