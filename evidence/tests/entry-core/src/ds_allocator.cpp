// DS座位/写权分离：单进程串行教学模型，完全合成玩家与DS。
// {player, ds, epoch}绑定授权；全局epoch仅在本进程生命周期内单调。
// 没有持久化/跨进程锁/真实资源写入，Commit只是判定示范。
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

static int passed = 0, failed = 0;
static void Check(bool ok, const std::string& name) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name.c_str());
    ok ? ++passed : ++failed;
}
struct Handle {
    std::string player, ds;
    uint64_t epoch = 0;
    bool operator==(const Handle& b) const { return std::tie(player, ds, epoch) == std::tie(b.player, b.ds, b.epoch); }
};
struct Lease {
    Handle handle;
    int64_t expiresAt = 0, holdUntil = 0;
    bool operator==(const Lease& b) const { return handle == b.handle && expiresAt == b.expiresAt && holdUntil == b.holdUntil; }
};
struct Server { int capacity = 0, load = 0; };
struct Allocator {
    std::map<std::string, Server> servers;
    std::map<std::string, Lease> seats; // 有账记录不等于仍有写权限。
    uint64_t lastEpoch = 0;
    int64_t ttl = 30, grace = 10, observedTime = -1;
    enum class Status { kNew, kReused, kReconnected, kNoCapacity, kBadInput, kClockRollback, kOverflow, kExhausted };
    bool Invariant() const {
        std::map<std::string, int> counts;
        std::set<uint64_t> epochs;
        for (const auto& item : servers) counts[item.first] = 0;
        for (const auto& item : seats) {
            const auto& l = item.second;
            if (item.first != l.handle.player || !counts.count(l.handle.ds) || !l.handle.epoch ||
                l.handle.epoch > lastEpoch || !epochs.insert(l.handle.epoch).second ||
                l.expiresAt < 0 || l.holdUntil < l.expiresAt) return false;
            ++counts[l.handle.ds];
        }
        for (const auto& item : servers)
            if (item.second.capacity < 0 || item.second.load < 0 || item.second.load > item.second.capacity ||
                item.second.load != counts[item.first]) return false;
        return true;
    }
    bool ConfigValid() const { return ttl > 0 && grace >= 0 && Invariant(); }
    bool Observe(int64_t now) {
        if (now < 0 || now < observedTime) return false;
        // 只有通过各API输入/配置前检的调用才到达Observe；不是进程全局时钟服务。
        // now是可信调度器时钟输入，不来自客户端。到达此处即使句柄被拒也记录时间，
        // 防止“2000判过期 -> 时钟回退1000 -> 旧租约复活”。失败只允许这项水位推进。
        observedTime = now;
        return true;
    }
    bool Deadlines(int64_t now, int64_t& expires, int64_t& hold) const {
        const auto max = std::numeric_limits<int64_t>::max();
        if (now < 0 || ttl <= 0 || grace < 0 || now > max - ttl) return false;
        expires = now + ttl;
        if (expires > max - grace) return false;
        hold = expires + grace;
        return true;
    }
    bool MatchesLive(const Handle& h, int64_t now) const {
        const auto it = seats.find(h.player);
        return it != seats.end() && it->second.handle == h && now < it->second.expiresAt;
    }
    // 受保护副作用的真实实现必须在同一事务/CAS提交点再校验，不能先检查后无条件写。
    bool Commit(const Handle& h, int64_t now) {
        return ConfigValid() && Observe(now) && MatchesLive(h, now);
    }
    bool Heartbeat(const Handle& h, int64_t now) {
        if (!ConfigValid() || !Observe(now) || !MatchesLive(h, now)) return false;
        int64_t expires = 0, hold = 0;
        if (!Deadlines(now, expires, hold)) return false;
        Lease& l = seats.at(h.player);
        if (expires < l.expiresAt || hold < l.holdUntil) return false;
        l.expiresAt = expires; l.holdUntil = hold;
        return Invariant();
    }
    // 请求Release同样只接受Live句柄。过期owner连宽限保留座位也无权清除。
    bool Release(const Handle& h, int64_t now) {
        if (!ConfigValid() || !Observe(now) || !MatchesLive(h, now)) return false;
        --servers.at(h.ds).load; seats.erase(h.player);
        return Invariant();
    }
    Status Allocate(const std::string& player, int64_t now, Lease* out) {
        if (!out || player.empty() || player.size() > 64 || !ConfigValid() || now < 0) return Status::kBadInput;
        if (now < observedTime) return Status::kClockRollback;
        Observe(now);
        const auto old = seats.find(player);
        if (old != seats.end() && now < old->second.expiresAt) {
            *out = old->second; return Status::kReused;
        }
        int64_t expires = 0, hold = 0;
        if (!Deadlines(now, expires, hold)) return Status::kOverflow;
        if (lastEpoch == std::numeric_limits<uint64_t>::max()) return Status::kExhausted;
        if (old != seats.end() && now < old->second.holdUntil) {
            // 原DS满额包含本人的保留位：原位换代，不递增load，不迁走。
            const Lease next{{player, old->second.handle.ds, lastEpoch + 1}, expires, hold};
            old->second = next; ++lastEpoch; *out = next;
            return Status::kReconnected;
        }
        // 先在副本计算Expired座位清理后的容量；失败不清旧账、不改输出/epoch。
        auto nextSeats = seats;
        auto nextServers = servers;
        for (auto it = nextSeats.begin(); it != nextSeats.end();) {
            if (now >= it->second.holdUntil) {
                --nextServers.at(it->second.handle.ds).load; it = nextSeats.erase(it);
            } else ++it;
        }
        for (auto& item : nextServers) {
            if (item.second.load < item.second.capacity) {
                const Lease next{{player, item.first, lastEpoch + 1}, expires, hold};
                nextSeats.emplace(player, next); ++item.second.load;
                seats.swap(nextSeats); servers.swap(nextServers); ++lastEpoch; *out = next;
                return Status::kNew;
            }
        }
        return Status::kNoCapacity;
    }
    int Reap(int64_t now) {
        if (!ConfigValid() || !Observe(now)) return -1;
        int n = 0;
        for (auto it = seats.begin(); it != seats.end();) {
            if (now >= it->second.holdUntil) {
                --servers.at(it->second.handle.ds).load; it = seats.erase(it); ++n;
            } else ++it;
        }
        return Invariant() ? n : -1;
    }
    int TotalLoad() const { int n = 0; for (const auto& s : servers) n += s.second.load; return n; }
    // 授权失败允许时钟水位推进，但不得改这些资源/配置/代次状态。
    std::string ResourceState() const {
        std::string s = std::to_string(lastEpoch) + ":" + std::to_string(ttl) + ":" + std::to_string(grace);
        for (const auto& ds : servers) s += "|" + ds.first + ":" + std::to_string(ds.second.capacity) + ":" + std::to_string(ds.second.load);
        for (const auto& seat : seats) s += "|" + seat.first + ":" + seat.second.handle.ds + ":" +
            std::to_string(seat.second.handle.epoch) + ":" + std::to_string(seat.second.expiresAt) + ":" + std::to_string(seat.second.holdUntil);
        return s;
    }
};
static Allocator Make(int n = 3, int capacity = 2) {
    Allocator a;
    for (int i = 1; i <= n; ++i) a.servers.emplace("ds-" + std::to_string(i), Server{capacity, 0});
    return a;
}
static void TestBoundaries() {
    using S = Allocator::Status;
    for (bool reap : {false, true}) for (int64_t now : {1029, 1030, 1031, 1039, 1040, 1041, 2000}) {
        for (int op = 0; op < 4; ++op) {
            auto a = Make(1, 1); Lease old, current;
            Check(a.Allocate("p1", 1000, &old) == S::kNew && a.Invariant(), "boundary setup counted exactly one seat");
            if (reap) Check(a.Reap(now) == (now >= 1040 ? 1 : 0) && a.Invariant(), "Reap changes capacity only at holdUntil");
            const auto before = a.ResourceState();
            bool correct = false;
            if (op == 0) correct = a.Commit(old.handle, now) == (now < 1030) && before == a.ResourceState();
            if (op == 1) { const auto ok = a.Heartbeat(old.handle, now); correct = ok == (now < 1030) && (ok || before == a.ResourceState()); }
            if (op == 2) { const auto ok = a.Release(old.handle, now); correct = ok == (now < 1030) && (ok || before == a.ResourceState()); }
            if (op == 3) {
                const auto s = a.Allocate("p1", now, &current);
                correct = s == (now < 1030 ? S::kReused : now < 1040 ? S::kReconnected : S::kNew) &&
                          a.TotalLoad() == 1 && current.handle.ds == "ds-1" &&
                          (now < 1030 ? current.handle.epoch == old.handle.epoch : current.handle.epoch > old.handle.epoch);
            }
            Check(correct && a.Invariant(), "boundary now=" + std::to_string(now) + " op=" + std::to_string(op) + (reap ? " with Reap" : " without Reap"));
        }
    }
}
static void TestIsolation() {
    using S = Allocator::Status;
    auto a = Make(2, 1); Lease old, again;
    a.Allocate("p1", 1000, &old);
    Check(a.Invariant() && a.Allocate("p1", 1035, &again) == S::kReconnected && again.handle.ds == old.handle.ds &&
          again.handle.epoch > old.handle.epoch && a.TotalLoad() == 1 && a.Invariant(), "grace reconnect on a full original DS reuses its counted seat and rotates epoch");
    const auto state = a.ResourceState();
    Check(!a.Commit(old.handle, 1035) && a.Invariant(), "old generation commit rejected after grace reconnect");
    Check(!a.Heartbeat(old.handle, 1035) && a.Invariant(), "old generation heartbeat rejected after grace reconnect");
    Check(!a.Release(old.handle, 1035) && state == a.ResourceState() && a.Invariant(), "old generation release cannot clear new reservation");
    Check(a.Commit(again.handle, 1035) && a.Invariant(), "new generation can commit");
    Check(a.Heartbeat(again.handle, 1036) && a.Invariant(), "new generation heartbeat maintains one seat");
    const auto identityState = a.ResourceState();
    for (int field = 0; field < 3; ++field) {
        auto forged = again.handle;
        if (field == 0) forged.player = "other";
        if (field == 1) forged.ds = "ds-2";
        if (field == 2) ++forged.epoch;
        Check(!a.Commit(forged, 1036) && a.Invariant(), "forged handle commit rejected field=" + std::to_string(field));
        Check(!a.Heartbeat(forged, 1036) && a.Invariant(), "forged handle heartbeat rejected field=" + std::to_string(field));
        Check(!a.Release(forged, 1036) && a.Invariant() && identityState == a.ResourceState(), "forged handle release rejected without resource changes field=" + std::to_string(field));
    }
    Check(a.Release(again.handle, 1036) && a.TotalLoad() == 0 && a.Invariant(), "matching live release decrements exactly one seat");
    Check(!a.Release(again.handle, 1036) && a.Invariant(), "duplicate release cannot double decrement");
    // 真实跨DS旧句柄反例：旧玩家的原位已给另一个玩家。
    auto b = Make(2, 1); Lease first, second, moved;
    b.Allocate("p1", 1000, &first); b.Reap(1040); b.Allocate("p2", 1040, &second); b.Allocate("p1", 1040, &moved);
    Check(b.Invariant() && moved.handle.ds == "ds-2" && moved.handle.epoch > first.handle.epoch, "cross-DS allocation increases allocator-wide epoch");
    const auto before = b.ResourceState();
    Check(!b.Commit(first.handle, 1040) && !b.Heartbeat(first.handle, 1040) && !b.Release(first.handle, 1040) && b.Invariant() && before == b.ResourceState(),
          "old ds-1 handle cannot commit/renew/release p1 now on ds-2");
    auto sameNumberWrongResource = moved.handle; sameNumberWrongResource.ds = "ds-1";
    Check(!b.Commit(sameNumberWrongResource, 1040) && !b.Heartbeat(sameNumberWrongResource, 1040) &&
          !b.Release(sameNumberWrongResource, 1040) && before == b.ResourceState(), "even equal epoch numeric value cannot authorize another DS");
    Lease overflow{{"sentinel", "ds-sentinel", 999}, 7, 8}; const auto saved = overflow;
    Check(b.Allocate("p3", 1040, &overflow) == S::kNoCapacity && overflow == saved && before == b.ResourceState(), "no capacity leaves output and resource state unchanged");
    auto late = Make(1, 1); Lease l; late.Allocate("p", 1000, &l);
    const auto lateState = late.ResourceState();
    Check(!late.Heartbeat(l.handle, 2000) && late.Invariant() && late.ResourceState() == lateState, "expired heartbeat without Reap cannot revive owner");
    Check(!late.Commit(l.handle, 1000) && late.observedTime == 2000 && late.ResourceState() == lateState, "clock rollback after expired rejection cannot restore authority");
}
static void TestEdgesAndSequences() {
    using S = Allocator::Status;
    const auto max = std::numeric_limits<int64_t>::max();
    for (int which = 0; which < 5; ++which) {
        auto a = Make(); Lease out{{"sentinel", "sentinel", 7}, 8, 9}; const auto saved = out;
        if (which == 0) a.ttl = -1;
        if (which == 1) a.grace = -1;
        if (which == 2) a.ttl = max;
        if (which == 3) a.grace = max;
        if (which == 4) a.ttl = 0;
        const auto before = a.ResourceState();
        const auto st = a.Allocate("p", 1, &out);
        Check(st == ((which < 2 || which == 4) ? S::kBadInput : S::kOverflow) && before == a.ResourceState() && out == saved && a.Invariant(),
              "invalid TTL/grace or deadline overflow rejects without resource effects case=" + std::to_string(which));
    }
    auto a = Make(); Lease old, out;
    a.Allocate("p", 1000, &old);
    const auto before = a.ResourceState();
    Check(a.Allocate("p2", 999, &out) == S::kClockRollback && a.ResourceState() == before && a.Invariant(), "allocation rejects backwards trusted clock");
    Check(!a.Commit(old.handle, 999) && !a.Heartbeat(old.handle, 999) && !a.Release(old.handle, 999) && a.Reap(999) == -1 &&
          a.ResourceState() == before && a.Invariant(), "all operations reject backwards clock without resource effects");
    auto preflight = Make(); Lease live, unused;
    preflight.Allocate("p", 1000, &live);
    const auto preflightState = preflight.ResourceState();
    Check(preflight.Allocate("", 2000, &unused) == S::kBadInput && preflight.observedTime == 1000 &&
          preflight.Allocate("p", 2000, nullptr) == S::kBadInput && preflight.observedTime == 1000 &&
          preflight.ResourceState() == preflightState && preflight.Commit(live.handle, 1020),
          "input preflight failure is not a clock observation; valid later call uses last observed watermark");
    auto limit = Make(); limit.ttl = 1; limit.grace = 0;
    Check(limit.Allocate("p", max - 1, &old) == S::kNew && limit.Invariant(), "exact INT64_MAX deadline is representable");
    const auto atLimit = limit.ResourceState();
    Check(!limit.Heartbeat(old.handle, max) && !limit.Commit(old.handle, max) && limit.ResourceState() == atLimit, "expiry at INT64_MAX still loses authority");
    Check(limit.Allocate("q", max, &out) == S::kOverflow && limit.ResourceState() == atLimit, "overflow allocation does not reap or overwrite old accounting");
    Check(limit.Reap(max) == 1 && limit.Invariant(), "cleanup uses stored deadline without overflow");
    auto heartbeatOverflow = Make(); heartbeatOverflow.ttl = 5; heartbeatOverflow.grace = 0;
    heartbeatOverflow.Allocate("p", max - 5, &old); const auto hb = heartbeatOverflow.ResourceState();
    Check(!heartbeatOverflow.Heartbeat(old.handle, max - 1) && heartbeatOverflow.ResourceState() == hb, "live heartbeat overflow leaves expiry unchanged");
    auto exhausted = Make(1, 1); exhausted.lastEpoch = std::numeric_limits<uint64_t>::max() - 1;
    Check(exhausted.Allocate("p", 1000, &old) == S::kNew && old.handle.epoch == std::numeric_limits<uint64_t>::max(), "last representable epoch can be issued once");
    const auto finalEpoch = exhausted.ResourceState();
    Check(exhausted.Allocate("p", 1035, &out) == S::kExhausted && finalEpoch == exhausted.ResourceState() && exhausted.Invariant(), "grace reacquire refuses epoch exhaustion without wrapping or leaking seat");
    Check(exhausted.Reap(1040) == 1 && exhausted.Invariant(), "expired last epoch can still be reaped");
    Check(exhausted.Allocate("q", 1040, &out) == S::kExhausted && exhausted.TotalLoad() == 0, "reap does not reset epoch high water mark");
    // 固定事件序列；每一步检查逐DS账实相等/唯一归属，而非只比较总数。
    auto seq = Make(2, 1); Lease p, q, r;
    Check(seq.Allocate("p", 0, &p) == S::kNew && seq.Invariant(), "sequence 1 allocate p");
    Check(seq.Allocate("q", 0, &q) == S::kNew && seq.Invariant(), "sequence 2 allocate q");
    Check(seq.Heartbeat(p.handle, 20) && seq.Invariant(), "sequence 3 renew p");
    Check(!seq.Commit(q.handle, 30) && seq.Invariant(), "sequence 4 q becomes read-only at TTL");
    Check(seq.Allocate("q", 35, &r) == S::kReconnected && seq.Invariant(), "sequence 5 q reconnect retains full DS seat");
    Check(!seq.Release(q.handle, 35) && seq.Invariant(), "sequence 6 delayed old q release");
    Check(seq.Reap(60) == 1 && seq.Invariant(), "sequence 7 reap only expired p");
    Check(seq.Release(r.handle, 60) && seq.Invariant(), "sequence 8 release new q");
    Check(seq.Reap(100) == 0 && seq.TotalLoad() == 0 && seq.Invariant(), "sequence 9 repeated cleanup stays empty");
    auto corrupt = Make(2, 1); corrupt.Allocate("p", 0, &p); ++corrupt.servers.at("ds-2").load;
    Check(!corrupt.Invariant(), "negative control: invariant detects phantom seat on another DS");
    auto misplaced = Make(2, 1); misplaced.Allocate("p", 0, &p); misplaced.seats.at("p").handle.player = "q";
    Check(!misplaced.Invariant(), "negative control: invariant detects wrong player ownership");
}
int main() {
    std::puts("ds_allocator: half-open live/grace windows; serialized model, no persistent fencing claim");
    TestBoundaries(); TestIsolation(); TestEdgesAndSequences();
    std::printf("RESULT pass=%d fail=%d\n", passed, failed);
    return failed ? 1 : 0;
}
