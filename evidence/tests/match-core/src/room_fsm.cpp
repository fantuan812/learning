// evidence/tests/match-core/src/room_fsm.cpp
//
// 匹配到对局 · 房间/对局生命周期证据
//   Queued -> Matched -> Allocating -> Ready -> InProgress -> Settling -> Closed
//   覆盖：全员确认、拒绝后回队列（且不可重复拒绝）、确认超时解散、名额释放、
//         重复确认/重复结算的幂等、满员后的中途加入（backfill）边界。
//
// 构建：g++ -std=c++17 -O2 -Wall -o build/room_fsm.exe src/room_fsm.cpp

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name, const std::string& detail = "") {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!detail.empty()) std::printf("        %s\n", detail.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

enum class RoomState { kQueued, kMatched, kAllocating, kReady, kInProgress, kSettling, kClosed };
static const char* RoomName(RoomState s) {
    switch (s) {
        case RoomState::kQueued: return "Queued";
        case RoomState::kMatched: return "Matched";
        case RoomState::kAllocating: return "Allocating";
        case RoomState::kReady: return "Ready";
        case RoomState::kInProgress: return "InProgress";
        case RoomState::kSettling: return "Settling";
        case RoomState::kClosed: return "Closed";
    }
    return "?";
}

enum class Slot { kWaitConfirm, kAccepted, kDeclined, kDropped, kSettled };

struct Member {
    std::string id;
    Slot slot = Slot::kWaitConfirm;
    int declinedCount = 0;
    int64_t settledRevision = -1;
};

// 单服务、单线程内存名额池；不是真实 DS 租约、持久化去重或跨进程 fencing。
struct AllocatorPool {
    int capacity = 0;
    int used = 0;
    int allocCount = 0;
    bool Acquire() {
        if (used >= capacity) return false;
        ++used;
        ++allocCount;
        return true;
    }
    bool Release() {
        if (used <= 0) return false;
        --used;
        return true;
    }
};

struct Room {
    std::string id;
    RoomState state = RoomState::kQueued;
    std::vector<Member> members;
    int64_t confirmDeadline = 0;
    int64_t createdTick = 0;
    int64_t settleRevision = 0;
    int settles = 0;
    std::vector<std::string> log;
    std::vector<std::string> requeued;   // 指定退出者，以及超时仍未确认者
    bool ownsAllocation = false;       // 资源事实：不能从 Accepted 人数推断
    bool memberSetInvalidated = false; // D1：本生命周期不可重组或再次 Ready

    bool AllAccepted() const {
        if (members.empty()) return false;
        for (const Member& m : members)
            if (m.slot != Slot::kAccepted) return false;
        return true;
    }
    int CountAccepted() const {
        int n = 0;
        for (const Member& m : members) if (m.slot == Slot::kAccepted) ++n;
        return n;
    }
    Member* Find(const std::string& id) {
        for (Member& m : members) if (m.id == id) return &m;
        return nullptr;
    }
    void Push(const std::string& s) { log.push_back(s); }
    std::string Trace() const {
        std::string out;
        for (size_t i = 0; i < log.size(); ++i) { if (i) out += " -> "; out += log[i]; }
        return out;
    }
};

// API 前置：Room& 必须来自本服务 CreateRoom，且调用者不直接改写公开状态；
// 成员 ID 非空且唯一；时间 tick/window 均非负，now + confirmWindow 可由 int64_t 表示。
// pool.capacity 非负；成员/名额/事件计数及容器人数须在相应 int/int64_t 范围内可表示。
// 不提供任意输入验证或分配异常的强事务保证；所有动作由调用者串行执行。
struct MatchService {
    AllocatorPool pool;
    std::map<std::string, Room> rooms;

    Room& CreateRoom(const std::string& id, const std::vector<std::string>& ids,
                     int64_t now, int64_t confirmWindow) {
        // D3 新失败合同：即使 payload 相同或旧房 Closed，也拒绝 ID 重用。
        // 调用者须处理 invalid_argument；异常拒绝不等于幂等创建成功。
        if (rooms.find(id) != rooms.end()) throw std::invalid_argument("room ID already exists");
        Room& r = rooms[id];
        r.id = id;
        r.state = RoomState::kMatched;
        r.createdTick = now;
        r.confirmDeadline = now + confirmWindow;
        for (const std::string& s : ids) {
            Member m;
            m.id = s;
            r.members.push_back(m);
        }
        r.Push("Matched");
        return r;
    }

    // 只消费本房仍持有的一份名额；成员 slot 与资源所有权彼此独立。
    void ReleaseAllocation(Room& r) {
        if (!r.ownsAllocation) return;
        pool.Release();
        r.ownsAllocation = false;
    }

    static bool PreStart(const Room& r) {
        return r.state == RoomState::kMatched || r.state == RoomState::kAllocating ||
               r.state == RoomState::kReady;
    }

    // 确认进服：有效生命周期内重复确认不推进状态；失效旧房全部拒绝。
    bool Confirm(Room& r, const std::string& id, int64_t now) {
        Member* m = r.Find(id);
        if (!m) return false;
        if (r.state == RoomState::kClosed || r.memberSetInvalidated) return false;
        if (m->slot == Slot::kAccepted) return true;               // 幂等
        if (m->slot != Slot::kWaitConfirm) return false;           // 已拒绝/已掉线
        if (now > r.confirmDeadline) return false;                 // 迟到的确认无效

        m->slot = Slot::kAccepted;
        if (r.state == RoomState::kMatched) {
            if (!pool.Acquire()) {
                m->slot = Slot::kWaitConfirm;                       // 回滚，保持可重试
                return false;
            }
            r.ownsAllocation = true;
            r.state = RoomState::kAllocating;
            r.Push("Allocating");
        }
        if (r.state == RoomState::kAllocating && r.ownsAllocation && r.AllAccepted()) {
            r.state = RoomState::kReady;
            r.Push("Ready");
        }
        return true;
    }

    // D1：只允许待确认者拒绝；首次退出使原成员集合失效，其他成员不自动回队。
    bool Decline(Room& r, const std::string& id) {
        Member* m = r.Find(id);
        if (!m || !PreStart(r)) return false;
        if (m->slot != Slot::kWaitConfirm) return false;
        m->slot = Slot::kDeclined;
        ++m->declinedCount;
        r.requeued.push_back(id);
        r.memberSetInvalidated = true;
        ReleaseAllocation(r);
        if (r.state == RoomState::kAllocating) {
            r.state = RoomState::kMatched;
            r.Push("Allocating->Matched(decline)");
        }
        return true;
    }

    // D2：只处理开局前 Drop；赛中拒绝且无副作用，Settle 负责赛中释放。
    bool Drop(Room& r, const std::string& id) {
        Member* m = r.Find(id);
        if (!m || !PreStart(r)) return false;
        if (m->slot != Slot::kWaitConfirm && m->slot != Slot::kAccepted) return false;
        m->slot = Slot::kDropped;
        r.requeued.push_back(id);
        r.memberSetInvalidated = true;
        ReleaseAllocation(r);
        if (r.state == RoomState::kReady || r.state == RoomState::kAllocating) {
            r.state = RoomState::kMatched;
            r.Push("->Matched(drop)");
        }
        return true;
    }

    // 确认超时（或有人拒绝导致无法开局）：解散并释放名额
    bool Timeout(Room& r, int64_t now) {
        if (r.state == RoomState::kClosed || r.state == RoomState::kInProgress) return false;
        if (now <= r.confirmDeadline) return false;
        for (Member& m : r.members) {
            if (m.slot == Slot::kAccepted) m.slot = Slot::kDropped;
            if (m.slot == Slot::kWaitConfirm) { m.slot = Slot::kDropped; r.requeued.push_back(m.id); }
        }
        ReleaseAllocation(r);
        r.state = RoomState::kClosed;
        r.Push("Closed(confirm_timeout)");
        return true;
    }

    bool Start(Room& r) {
        if (r.state != RoomState::kReady || r.memberSetInvalidated ||
            !r.ownsAllocation || !r.AllAccepted()) return false;
        r.state = RoomState::kInProgress;
        r.Push("InProgress");
        return true;
    }

    // 仅本进程内重复结算返回同一版本；不证明崩溃/跨服务 exactly-once。
    int64_t Settle(Room& r) {
        if (r.settles > 0) return r.settleRevision;      // 幂等：已结算，直接回同一版本
        if (r.state != RoomState::kInProgress && r.state != RoomState::kSettling) return -1;
        if (!r.ownsAllocation) return -1;
        r.state = RoomState::kSettling;
        ++r.settleRevision;
        ++r.settles;
        for (Member& m : r.members) m.settledRevision = r.settleRevision;
        r.state = RoomState::kClosed;
        ReleaseAllocation(r);
        r.Push("Closed(settled)");
        return r.settleRevision;
    }

    // Backfill 仅检查进行中、容量和重复 ID；没有 MMR/party/断线重连策略。
    bool Backfill(Room& r, const std::string& id, int capacity) {
        if (r.state != RoomState::kInProgress) return false;
        if (static_cast<int>(r.members.size()) >= capacity) return false;
        for (const Member& m : r.members) if (m.id == id) return false;
        Member m;
        m.id = id;
        m.slot = Slot::kAccepted;
        r.members.push_back(m);
        r.Push("Backfill(" + id + ")");
        return true;
    }
};

static std::vector<std::string> Ids(int n, const char* prefix = "p") {
    std::vector<std::string> v;
    for (int i = 1; i <= n; ++i) v.push_back(std::string(prefix) + std::to_string(i));
    return v;
}

// ---------------------------------------------------------------------------
// 断言组
// ---------------------------------------------------------------------------
static void TestHappyPath() {
    MatchService svc;
    svc.pool.capacity = 4;
    Room& r = svc.CreateRoom("room-1", Ids(4), 1000, 30);

    Check(r.state == RoomState::kMatched && r.members.size() == 4,
          "R1 a formed room starts in Matched with all members pending confirmation",
          "state=" + std::string(RoomName(r.state)));

    for (int i = 1; i <= 4; ++i) svc.Confirm(r, "p" + std::to_string(i), 1005);

    Check(r.state == RoomState::kReady && r.CountAccepted() == 4,
          "R2 all members confirmed -> room is Ready",
          "state=" + std::string(RoomName(r.state)));
    Check(svc.pool.used == 1 && svc.pool.allocCount == 1,
          "R3 exactly ONE allocation is made for the whole room (first confirm triggers it)",
          "used=" + std::to_string(svc.pool.used) + " allocCount=" + std::to_string(svc.pool.allocCount));
    Check(svc.Start(r) && r.state == RoomState::kInProgress,
          "R4 the match starts only after Ready");
    Check(r.Trace() == "Matched -> Allocating -> Ready -> InProgress",
          "R5 the room transition log is exactly the expected path", r.Trace());
}

static void TestIdempotency() {
    MatchService svc;
    svc.pool.capacity = 4;
    Room& r = svc.CreateRoom("room-2", Ids(3), 1000, 30);

    svc.Confirm(r, "p1", 1001);
    svc.Confirm(r, "p1", 1002);
    svc.Confirm(r, "p1", 1003);
    Check(svc.pool.allocCount == 1 && r.CountAccepted() == 1,
          "R6 repeated confirm from the same player is idempotent (no extra allocation)",
          "allocCount=" + std::to_string(svc.pool.allocCount));

    svc.Confirm(r, "p2", 1004);
    svc.Confirm(r, "p3", 1005);
    Check(r.state == RoomState::kReady, "R7 the room reaches Ready exactly once all are in");
    svc.Start(r);

    const int64_t first = svc.Settle(r);
    const int64_t second = svc.Settle(r);
    Check(first > 0 && second == first && r.settles == 1,
          "R8 settling twice returns the same revision and settles exactly once",
          "revision=" + std::to_string(second) + " settles=" + std::to_string(r.settles));
    Check(svc.pool.used == 0,
          "R9 settlement releases every slot exactly once (no leak, no double free)",
          "used=" + std::to_string(svc.pool.used));
}

static void TestDeclineAndTimeout() {
    // 拒绝后回队列，并释放已占名额
    {
        MatchService svc;
        svc.pool.capacity = 2;
        Room& r = svc.CreateRoom("room-3", Ids(3), 1000, 30);
        svc.Confirm(r, "p1", 1001);
        Check(svc.pool.used == 1, "R10 the first confirm holds the slot");

        Check(svc.Decline(r, "p2"), "R11 a pending member can decline");
        Check(!svc.Decline(r, "p2"),
              "R12 declining twice by the same player is rejected (no free re-roll)");
        Check(svc.pool.used == 0 && r.state == RoomState::kMatched,
              "R13 a decline during allocating releases the slot and rewinds the room",
              "state=" + std::string(RoomName(r.state)) + " used=" + std::to_string(svc.pool.used));
        Check(std::count(r.requeued.begin(), r.requeued.end(), "p2") == 1,
              "R14 the declining player is returned to the queue exactly once");
        Check(!svc.Confirm(r, "p2", 1005),
              "R15 a member who already declined cannot re-confirm into this room");
    }
    // 已确认者不得反悔
    {
        MatchService svc;
        svc.pool.capacity = 2;
        Room& r = svc.CreateRoom("room-4", Ids(2), 1000, 30);
        svc.Confirm(r, "p1", 1001);
        Check(!svc.Decline(r, "p1"),
              "R16 an already-accepted member cannot decline (no late bail-out)");
    }
    // 确认超时：解散 + 名额释放 + 未确认者回队列
    {
        MatchService svc;
        svc.pool.capacity = 4;
        Room& r = svc.CreateRoom("room-5", Ids(4), 1000, 30);
        svc.Confirm(r, "p1", 1001);
        svc.Confirm(r, "p2", 1002);
        const bool closed = svc.Timeout(r, 1000 + 30 + 1);
        Check(closed && r.state == RoomState::kClosed,
              "R17 missing the confirmation deadline closes the room",
              "state=" + std::string(RoomName(r.state)));
        Check(svc.pool.used == 0,
              "R18 closing a half-confirmed room releases the slot it had taken",
              "used=" + std::to_string(svc.pool.used));
        Check(std::count(r.requeued.begin(), r.requeued.end(), "p3") == 1 &&
                  std::count(r.requeued.begin(), r.requeued.end(), "p4") == 1,
              "R19 players who never confirmed are returned to the queue",
              "requeued=" + std::to_string(r.requeued.size()));
        Check(!svc.Confirm(r, "p3", 1004),
              "R20 a late confirm after the deadline is refused");
    }
}

static void TestDropAndAllocationFailure() {
    // 掉线释放名额
    {
        MatchService svc;
        svc.pool.capacity = 3;
        Room& r = svc.CreateRoom("room-6", Ids(3), 1000, 30);
        for (int i = 1; i <= 3; ++i) svc.Confirm(r, "p" + std::to_string(i), 1001);
        Check(r.state == RoomState::kReady && svc.pool.used == 1, "R21 room is Ready and holds one slot");
        svc.Drop(r, "p2");
        Check(svc.pool.used == 0 && r.state == RoomState::kMatched,
              "R22 dropping a member after Ready rewinds the room and frees the slot",
              "state=" + std::string(RoomName(r.state)));
        Check(!svc.Drop(r, "p2"), "R23 dropping twice is a no-op");
    }
    // 名额不足：确认失败且不留下半占用状态
    {
        MatchService svc;
        svc.pool.capacity = 0;
        Room& r = svc.CreateRoom("room-7", Ids(2), 1000, 30);
        Check(!svc.Confirm(r, "p1", 1001),
              "R24 with no capacity left the confirm fails instead of over-selling");
        Check(r.state == RoomState::kMatched && svc.pool.used == 0,
              "R25 a failed allocation leaves the room and the pool untouched",
              "state=" + std::string(RoomName(r.state)) + " used=" + std::to_string(svc.pool.used));
    }
}

static void TestBackfill() {
    MatchService svc;
    svc.pool.capacity = 4;
    Room& r = svc.CreateRoom("room-8", Ids(3), 1000, 30);   // 3 人开局，容量 4
    for (int i = 1; i <= 3; ++i) svc.Confirm(r, "p" + std::to_string(i), 1001);
    svc.Start(r);

    Check(svc.Backfill(r, "p9", 4),
          "R26 a player can join an in-progress room that is not yet full");
    Check(!svc.Backfill(r, "p10", 4),
          "R27 once the room is full, backfill is refused");
    Check(!svc.Backfill(r, "p9", 4),
          "R28 backfilling the same player twice is refused");
    Check(r.Trace().find("Backfill(p9)") != std::string::npos,
          "R29 the backfill is recorded in the room log", r.Trace());
}

static void TestSlotLeakUnderChurn() {
    // 混合序列后：名额占用数必须等于"已确认未结算/未掉线"的房间数
    MatchService svc;
    svc.pool.capacity = 10;
    std::vector<std::string> open;

    for (int i = 0; i < 20; ++i) {
        const std::string rid = "r" + std::to_string(i);
        Room& r = svc.CreateRoom(rid, Ids(3, "m"), 1000 + i, 30);
        svc.Confirm(r, "m1", 1000 + i);
        svc.Confirm(r, "m2", 1000 + i);
        svc.Confirm(r, "m3", 1000 + i);
        if (i % 4 == 0) {
            svc.Start(r);
            svc.Settle(r);                                  // 正常打完
        } else if (i % 4 == 1) {
            svc.Drop(r, "m2");                              // 掉线
            svc.Confirm(r, "m2", 1001 + i);                 // 掉线后回不来（slot=kDropped）
        } else if (i % 4 == 2) {
            svc.Timeout(r, 1000 + i + 31);                  // 超时
        } else {
            open.push_back(rid);                            // 仍占着名额
        }
    }

    int expected = 0;
    for (const std::string& rid : open)
        if (svc.rooms[rid].state == RoomState::kReady) ++expected;

    Check(svc.pool.used == expected,
          "R30 after a churny mixed sequence the pool holds exactly the live rooms",
          "used=" + std::to_string(svc.pool.used) + " expected=" + std::to_string(expected));
    Check(svc.pool.used <= svc.pool.capacity,
          "R31 final pool usage is within capacity (not a whole-sequence monitor)",
          "used=" + std::to_string(svc.pool.used) + "/" + std::to_string(svc.pool.capacity));
}

int main() {
    std::printf("== room_fsm: 房间/对局生命周期证据 ==\n");
    std::printf("   Queued -> Matched -> Allocating -> Ready -> InProgress -> Settling -> Closed\n\n");

    TestHappyPath();
    TestIdempotency();
    TestDeclineAndTimeout();
    TestDropAndAllocationFailure();
    TestBackfill();
    TestSlotLeakUnderChurn();

    std::printf("\nRESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
