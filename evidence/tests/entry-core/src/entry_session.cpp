// evidence/tests/entry-core/src/entry_session.cpp
//
// 进入游戏 · 会话状态机证据
//   鉴权 → 分配 DS → 旅行 → 加载角色 → 生成(Spawning) → Ready
//   覆盖：幂等重入、单写者（同一请求只分配一次）、单步超时重试、失败回滚释放、
//         取消、就绪后重入复用会话、断线重连走恢复路径。
//
// 构建：g++ -std=c++17 -O2 -o build/entry_session.exe src/entry_session.cpp

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// 断言框架
// ---------------------------------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

static void Check(bool ok, const char* name, const std::string& detail = "") {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!detail.empty()) std::printf("        %s\n", detail.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

// ---------------------------------------------------------------------------
// 最小世界模型：4 个 DS，每个容量 2
// ---------------------------------------------------------------------------
enum class Acquire { kNew = 0, kReused = 1, kNoCapacity = -1 };

struct World {
    std::vector<std::string> servers = {"ds-1", "ds-2", "ds-3", "ds-4"};
    int capacityPerServer = 2;
    std::map<std::string, int> load;
    std::map<std::string, std::string> playerServer;

    World() { for (const auto& s : servers) load[s] = 0; }

    Acquire Acquire(const std::string& player, std::string* outServer) {
        const auto it = playerServer.find(player);
        if (it != playerServer.end()) { *outServer = it->second; return Acquire::kReused; }
        for (const auto& s : servers) {
            if (load[s] < capacityPerServer) {
                load[s]++;
                playerServer[player] = s;
                *outServer = s;
                return Acquire::kNew;
            }
        }
        return Acquire::kNoCapacity;
    }

    bool Release(const std::string& player) {
        const auto it = playerServer.find(player);
        if (it == playerServer.end()) return false;
        load[it->second]--;
        playerServer.erase(it);
        return true;
    }

    int TotalLoad() const { int n = 0; for (const auto& kv : load) n += kv.second; return n; }
};

// ---------------------------------------------------------------------------
// 进入游戏状态机
// ---------------------------------------------------------------------------
enum class State { kIdle, kAuthenticated, kAllocated, kTraveling, kLoaded, kReady, kFailed };
enum class Step { kAuth = 0, kAllocate, kTravel, kLoadChar, kSpawn, kCount };

static const char* StateName(State s) {
    switch (s) {
        case State::kIdle: return "Idle";
        case State::kAuthenticated: return "Authenticated";
        case State::kAllocated: return "Allocated";
        case State::kTraveling: return "Traveling";
        case State::kLoaded: return "Loaded";
        case State::kReady: return "Ready";
        case State::kFailed: return "Failed";
    }
    return "?";
}

static const char* StepName(Step s) {
    switch (s) {
        case Step::kAuth: return "Auth";
        case Step::kAllocate: return "Allocate";
        case Step::kTravel: return "Travel";
        case Step::kLoadChar: return "LoadCharacter";
        case Step::kSpawn: return "Spawn";
        default: return "?";
    }
}

enum class Outcome { kOk, kTimeout, kReject, kCancel };
static const char* OutcomeName(Outcome o) {
    switch (o) {
        case Outcome::kOk: return "ok";
        case Outcome::kTimeout: return "timeout";
        case Outcome::kReject: return "reject";
        case Outcome::kCancel: return "cancel";
    }
    return "?";
}

struct Config {
    int maxAttemptsPerStep = 3;
    bool reconnectUsesFastPath = true;
};

struct Session {
    std::string playerId;
    std::string requestId;
    State state = State::kIdle;
    std::string ds;
    int attempts[static_cast<int>(Step::kCount)] = {0, 0, 0, 0, 0};
    bool reconnect = false;
    bool rolledBack = false;
    std::vector<std::string> transitions;
    std::string failReason;
};

// 步骤结果生成器：入参 (步骤, 第几次尝试)，返回结果。返回 Reject 表示不可重试。
using StepFn = std::function<Outcome(Step, int)>;

struct EntryEngine {
    World world;
    Config cfg;
    std::unordered_map<std::string, Session> byRequest;   // 幂等表：requestId -> session
    std::vector<std::string> audit;

    Session* FindByRequest(const std::string& requestId) {
        const auto it = byRequest.find(requestId);
        return it == byRequest.end() ? nullptr : &it->second;
    }

    // 幂等入口：同一 requestId 永远返回同一个会话对象，且不重复产生副作用。
    Session& Enter(const std::string& playerId, const std::string& requestId, const StepFn& fn) {
        if (Session* existing = FindByRequest(requestId)) {
            audit.push_back("idempotent-hit " + requestId + " state=" + StateName(existing->state));
            return *existing;
        }
        Session& s = byRequest[requestId];
        s.playerId = playerId;
        s.requestId = requestId;
        s.transitions.push_back("Idle");
        audit.push_back("enter " + playerId + "/" + requestId);

        // 重连：已有 DS 归属时走快速路径，直接复用原实例
        if (cfg.reconnectUsesFastPath && world.playerServer.count(playerId)) {
            s.reconnect = true;
            s.ds = world.playerServer[playerId];
            audit.push_back("reconnect-fastpath " + playerId + " -> " + s.ds);
        }

        const Step steps[] = {Step::kAuth, Step::kAllocate, Step::kTravel, Step::kLoadChar, Step::kSpawn};
        for (Step step : steps) {
            bool done = false;
            while (!done) {
                ++s.attempts[static_cast<int>(step)];
                const Outcome o = fn(step, s.attempts[static_cast<int>(step)]);
                audit.push_back(std::string("step ") + StepName(step) + " attempt " +
                                std::to_string(s.attempts[static_cast<int>(step)]) + " -> " +
                                OutcomeName(o));

                if (o == Outcome::kOk) { done = true; break; }
                if (o == Outcome::kReject) {
                    s.failReason = std::string(StepName(step)) + ":reject";
                    Rollback(s);
                    return s;
                }
                if (o == Outcome::kCancel) {
                    s.failReason = "cancelled";
                    Rollback(s);
                    return s;
                }
                // timeout：重试到上限
                if (s.attempts[static_cast<int>(step)] >= cfg.maxAttemptsPerStep) {
                    s.failReason = std::string(StepName(step)) + ":timeout-after-" +
                                   std::to_string(s.attempts[static_cast<int>(step)]);
                    Rollback(s);
                    return s;
                }
            }
            Advance(s, step);
            // 关键：某一步判定为失败（如分配无容量）后必须立即终止，
            // 否则链路会带着空 DS 继续走完剩余步骤，产出"看起来就绪但从未连上"的会话。
            if (s.state == State::kFailed) return s;
        }
        return s;
    }

    void Advance(Session& s, Step step) {
        switch (step) {
            case Step::kAuth:     s.state = State::kAuthenticated; s.transitions.push_back("Authenticated"); break;
            case Step::kAllocate: {
                std::string ds;
                const Acquire a = world.Acquire(s.playerId, &ds);
                if (a == Acquire::kNoCapacity) {
                    // 分配失败不是异常，而是"稍后重试"的业务结果
                    s.state = State::kFailed;
                    s.failReason = "no_capacity";
                    s.transitions.push_back("Failed(no_capacity)");
                    return;
                }
                s.ds = ds;
                s.state = State::kAllocated;
                s.transitions.push_back("Allocated(" + ds + "," +
                                        (a == Acquire::kReused ? "reused" : "new") + ")");
                break;
            }
            case Step::kTravel:   s.state = State::kTraveling; s.transitions.push_back("Traveling(" + s.ds + ")"); break;
            case Step::kLoadChar: s.state = State::kLoaded;    s.transitions.push_back("Loaded"); break;
            case Step::kSpawn:    s.state = State::kReady;     s.transitions.push_back("Ready"); break;
            default: break;
        }
    }

    // 回滚：任何一步失败后，必须释放已占用的 DS 名额；释放动作必须幂等
    void Rollback(Session& s) {
        if (s.state == State::kReady) return;      // 已就绪：玩家在局内，不得清理
        bool released = false;
        if (!s.rolledBack && world.playerServer.count(s.playerId)) {
            world.Release(s.playerId);
            s.rolledBack = true;
            released = true;
        }
        s.state = State::kFailed;
        s.transitions.push_back(released ? "Rollback(released)" : "Rollback(no-op)");
    }

    // 取消：客户端在进入过程中退出，等价于回滚
    void Cancel(Session& s) {
        if (s.state == State::kReady) return;
        s.failReason = "cancelled";
        Rollback(s);
    }

    std::string Join(const Session& s) const {
        std::string out;
        for (size_t i = 0; i < s.transitions.size(); ++i) {
            if (i) out += " -> ";
            out += s.transitions[i];
        }
        return out;
    }
};

// ---------------------------------------------------------------------------
// 断言组
// ---------------------------------------------------------------------------
static void TestHappyPath() {
    EntryEngine e;
    auto ok = [](Step, int) { return Outcome::kOk; };
    Session& s = e.Enter("p1", "req-1", ok);

    Check(s.state == State::kReady,
          "E1 auth -> allocate -> travel -> load -> spawn reaches Ready",
          e.Join(s));
    Check(s.ds == "ds-1" && e.world.TotalLoad() == 1,
          "E2 exactly one DS slot is held after a successful entry",
          "ds=" + s.ds + " load=" + std::to_string(e.world.TotalLoad()));
    Check(s.transitions.size() == 6,
          "E3 transition log has 6 nodes (Idle + 5 states), no extra writes",
          std::to_string(s.transitions.size()) + " nodes");
}

static void TestIdempotency() {
    EntryEngine e;
    auto ok = [](Step, int) { return Outcome::kOk; };
    Session& first = e.Enter("p1", "req-1", ok);
    const std::string dsFirst = first.ds;

    // 同 requestId 重放（客户端超时重发）：不得再分配一个 DS
    Session& second = e.Enter("p1", "req-1", ok);
    Check(&first == &second && second.ds == dsFirst,
          "E4 replaying the same requestId returns the same session, not a new one",
          "ds=" + second.ds + " load=" + std::to_string(e.world.TotalLoad()));
    Check(e.world.TotalLoad() == 1,
          "E5 idempotent replay does not leak a DS slot",
          "load=" + std::to_string(e.world.TotalLoad()));

    // 同玩家换 requestId 重入（客户端重试生成新 id）：必须复用已有归属
    Session& third = e.Enter("p1", "req-2", ok);
    Check(third.ds == dsFirst && e.world.TotalLoad() == 1,
          "E6 re-entering with a NEW requestId reuses the existing DS assignment",
          "ds=" + third.ds + " load=" + std::to_string(e.world.TotalLoad()));

    int idempotentHits = 0;
    for (const auto& line : e.audit)
        if (line.rfind("idempotent-hit req-1", 0) == 0) ++idempotentHits;
    Check(e.audit.size() >= 2 && idempotentHits == 1,
          "E7 the idempotency table short-circuits the second identical request",
          "idempotent hits=" + std::to_string(idempotentHits) +
              " audit entries=" + std::to_string(e.audit.size()));
}

static void TestTimeoutAndRollback() {
    // 旅行阶段连续超时两次后成功：应重试并最终就绪，不产生额外 DS 占用
    {
        EntryEngine e;
        auto fn = [](Step st, int attempt) -> Outcome {
            if (st == Step::kTravel && attempt <= 2) return Outcome::kTimeout;
            return Outcome::kOk;
        };
        Session& s = e.Enter("p1", "req-1", fn);
        Check(s.state == State::kReady && s.attempts[static_cast<int>(Step::kTravel)] == 3,
              "E8 a flaky travel step retries and still reaches Ready",
              "travel attempts=" + std::to_string(s.attempts[static_cast<int>(Step::kTravel)]));
        Check(e.world.TotalLoad() == 1,
              "E9 retries do not accumulate DS slots",
              "load=" + std::to_string(e.world.TotalLoad()));
    }
    // 旅行阶段一直超时：耗尽重试后必须失败并释放 DS
    {
        EntryEngine e;
        auto fn = [](Step st, int) -> Outcome {
            return st == Step::kTravel ? Outcome::kTimeout : Outcome::kOk;
        };
        Session& s = e.Enter("p1", "req-1", fn);
        Check(s.state == State::kFailed && s.failReason == "Travel:timeout-after-3",
              "E10 exhausting retries on travel fails the entry",
              "reason=" + s.failReason);
        Check(e.world.TotalLoad() == 0 && e.world.playerServer.count("p1") == 0,
              "E11 a failed entry releases the DS slot (no leak)",
              "load=" + std::to_string(e.world.TotalLoad()));
        Check(s.rolledBack,
              "E12 the session is marked rolled back so the lease is not double-released");
    }
    // 鉴权被明确拒绝（非超时）：不得重试，且从未占用 DS
    {
        EntryEngine e;
        auto fn = [](Step st, int) -> Outcome {
            return st == Step::kAuth ? Outcome::kReject : Outcome::kOk;
        };
        Session& s = e.Enter("p1", "req-1", fn);
        Check(s.state == State::kFailed && s.attempts[static_cast<int>(Step::kAuth)] == 1,
              "E13 a rejected credential is not retried (1 attempt only)",
              "auth attempts=" + std::to_string(s.attempts[static_cast<int>(Step::kAuth)]));
        Check(e.world.TotalLoad() == 0,
              "E14 a rejected entry never touched the DS pool",
              "load=" + std::to_string(e.world.TotalLoad()));
    }
    // 生成(Spawning)阶段失败：侧效应已发生，仍必须回滚
    {
        EntryEngine e;
        auto fn = [](Step st, int) -> Outcome {
            return st == Step::kSpawn ? Outcome::kReject : Outcome::kOk;
        };
        Session& s = e.Enter("p1", "req-1", fn);
        Check(s.state == State::kFailed && e.world.TotalLoad() == 0,
              "E15 failure at the Spawn step also rolls back the allocation",
              "reason=" + s.failReason + " load=" + std::to_string(e.world.TotalLoad()));
        Check(s.transitions.back() == "Rollback(released)",
              "E16 rollback is the last transition, so the log shows the cleanup",
              e.Join(s));
    }
}

static void TestCapacityAndSharedWorld() {
    EntryEngine e;
    auto ok = [](Step, int) { return Outcome::kOk; };

    for (int i = 1; i <= 8; ++i) e.Enter("p" + std::to_string(i), "req-" + std::to_string(i), ok);
    Check(e.world.TotalLoad() == 8, "E17 four DS x capacity 2 = 8 concurrent players",
          "load=" + std::to_string(e.world.TotalLoad()));

    Session& overflow = e.Enter("p9", "req-9", ok);
    Check(overflow.state == State::kFailed && overflow.failReason == "no_capacity",
          "E18 the 9th player is refused with no_capacity instead of over-selling",
          "reason=" + overflow.failReason + " load=" + std::to_string(e.world.TotalLoad()));
    Check(overflow.transitions.back() == "Failed(no_capacity)",
          "E19 capacity refusal is a business result, not a crash",
          e.Join(overflow));

    // 释放一个名额后，第 9 个玩家可以进入
    e.world.Release("p1");
    Session& retry = e.Enter("p9", "req-9b", ok);
    Check(retry.state == State::kReady,
          "E20 after a slot frees up the same player can enter on a retry",
          "ds=" + retry.ds + " load=" + std::to_string(e.world.TotalLoad()));
}

static void TestCancelAndReconnect() {
    EntryEngine e;
    auto ok = [](Step, int) { return Outcome::kOk; };
    Session& s = e.Enter("p1", "req-1", ok);
    const std::string ds = s.ds;

    // 就绪后取消：不得释放（玩家已在局内）
    e.Cancel(s);
    Check(s.state == State::kReady && e.world.TotalLoad() == 1,
          "E21 cancelling an already-Ready session is a no-op (player is in the match)",
          std::string("state=") + StateName(s.state));

    // 断线后重入：走快速路径复用原 DS
    Session& again = e.Enter("p1", "req-2", ok);
    Check(again.reconnect && again.ds == ds,
          "E22 a player with an existing assignment takes the reconnect fast path",
          "ds=" + again.ds + " reconnect=" + (again.reconnect ? "true" : "false"));

    // 中途取消（尚未就绪）：客户端在"加载角色"阶段断开连接
    EntryEngine e2;
    auto cancelAtLoad = [](Step st, int) -> Outcome {
        return st == Step::kLoadChar ? Outcome::kCancel : Outcome::kOk;
    };
    Session& mid = e2.Enter("p2", "req-1", cancelAtLoad);
    Check(mid.state == State::kFailed && mid.failReason == "cancelled" &&
              e2.world.TotalLoad() == 0,
          "E23 aborting mid-entry rolls back and frees the DS slot",
          "reason=" + mid.failReason + " load=" + std::to_string(e2.world.TotalLoad()));
    Check(mid.transitions.back() == "Rollback(released)",
          "E24 the abort is visible in the transition log", e2.Join(mid));
    Check(mid.attempts[static_cast<int>(Step::kSpawn)] == 0,
          "E25 an aborted entry never reaches the Spawn step",
          "spawn attempts=" + std::to_string(mid.attempts[static_cast<int>(Step::kSpawn)]));
}

static void TestAuditTrace() {
    EntryEngine e;
    auto fn = [](Step st, int attempt) -> Outcome {
        if (st == Step::kLoadChar && attempt == 1) return Outcome::kTimeout;
        return Outcome::kOk;
    };
    Session& s = e.Enter("p1", "req-1", fn);

    int timeouts = 0;
    for (const auto& line : e.audit)
        if (line.find("-> timeout") != std::string::npos) ++timeouts;

    Check(s.state == State::kReady && timeouts == 1,
          "E26 the audit log records each step attempt with its outcome",
          "timeout entries=" + std::to_string(timeouts) + ", audit=" + std::to_string(e.audit.size()));
    Check(std::count(e.audit.begin(), e.audit.end(), "enter p1/req-1") == 1,
          "E27 one logical entry produces exactly one 'enter' record (no duplicate sessions)");
}

int main() {
    std::printf("== entry_session: 进入游戏·会话状态机证据 ==\n");
    std::printf("   auth -> allocate -> travel -> load -> spawn -> ready\n\n");

    TestHappyPath();
    TestIdempotency();
    TestTimeoutAndRollback();
    TestCapacityAndSharedWorld();
    TestCancelAndReconnect();
    TestAuditTrace();

    std::printf("\nRESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
