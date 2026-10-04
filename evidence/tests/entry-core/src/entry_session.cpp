// 进入会话：同步、单进程、串行教学模型；完全合成的认证回调。
// 授权先于缓存；键绑定可信主体/操作/requestId，意图逐字段比较。
// 没有网络/DB/UE实体副作用，不能称持久化 exactly-once。
#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

static int passed = 0, failed = 0;
static void Check(bool ok, const std::string& name) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name.c_str());
    ok ? ++passed : ++failed;
}
struct Principal {
    std::string tenant, subject;
    bool operator<(const Principal& b) const { return std::tie(tenant, subject) < std::tie(b.tenant, b.subject); }
    bool operator==(const Principal& b) const { return tenant == b.tenant && subject == b.subject; }
};
struct Intent {
    std::string player, match, character, region;
    bool operator==(const Intent& b) const {
        return std::tie(player, match, character, region) == std::tie(b.player, b.match, b.character, b.region);
    }
};
struct Request { std::string credential, operation, requestId; Intent intent; };
struct Handle {
    Principal principal;
    std::string ds;
    uint64_t epoch = 0;
    bool operator==(const Handle& b) const { return principal == b.principal && ds == b.ds && epoch == b.epoch; }
};
struct Seat { Handle handle; Intent intent; };
enum class Acquisition { kNew, kBorrowed, kNoCapacity, kConflict, kExhausted };
struct World {
    std::map<std::string, int> load{{"ds-1", 0}, {"ds-2", 0}, {"ds-3", 0}, {"ds-4", 0}};
    std::map<Principal, Seat> seats;
    int capacity = 2;
    uint64_t lastEpoch = 0;
    bool Invariant() const {
        std::map<std::string, int> actual;
        for (const auto& ds : load) actual[ds.first] = 0;
        for (const auto& item : seats) {
            if (!(item.first == item.second.handle.principal) || !actual.count(item.second.handle.ds) ||
                !item.second.handle.epoch || item.second.handle.epoch > lastEpoch) return false;
            ++actual[item.second.handle.ds];
        }
        for (const auto& ds : load)
            if (ds.second < 0 || ds.second > capacity || ds.second != actual[ds.first]) return false;
        return true;
    }
    bool Current(const Handle& h) const {
        const auto it = seats.find(h.principal);
        return it != seats.end() && it->second.handle == h;
    }
    Acquisition AcquireSeat(const Principal& p, const Intent& intent, Handle& out) {
        const auto it = seats.find(p);
        if (it != seats.end()) {
            if (!(it->second.intent == intent)) return Acquisition::kConflict;
            out = it->second.handle;
            return Acquisition::kBorrowed;
        }
        if (lastEpoch == std::numeric_limits<uint64_t>::max()) return Acquisition::kExhausted;
        for (auto& ds : load) {
            if (ds.second < capacity) {
                Handle h{p, ds.first, lastEpoch + 1};
                seats.emplace(p, Seat{h, intent});
                ++ds.second; ++lastEpoch; out = h;
                return Acquisition::kNew;
            }
        }
        return Acquisition::kNoCapacity;
    }
    // 请求补偿与正常退出均携带完整句柄。没有按player无条件释放的入口。
    bool ReleaseExpected(const Handle& h) {
        if (!Current(h)) return false;
        --load.at(h.ds); seats.erase(h.principal);
        return true;
    }
    int Total() const { int n = 0; for (const auto& d : load) n += d.second; return n; }
    std::string Snapshot() const {
        std::string s = std::to_string(lastEpoch);
        for (const auto& d : load) s += "|" + d.first + ":" + std::to_string(d.second);
        for (const auto& item : seats) {
            const auto& seat = item.second;
            s += "|" + item.first.tenant + "/" + item.first.subject + ":" + seat.handle.ds + ":" +
                 std::to_string(seat.handle.epoch) + ":" + seat.intent.player + ":" + seat.intent.match + ":" +
                 seat.intent.character + ":" + seat.intent.region;
        }
        return s;
    }
};
enum class Outcome { kOk, kReject, kTimeout, kCancel };
enum class Step { kAuth, kAllocate, kTravel, kLoad, kSpawn };
static std::string StepName(Step s) {
    return std::array<std::string, 5>{"Auth", "Allocate", "Travel", "Load", "Spawn"}[static_cast<size_t>(s)];
}
static std::string OutcomeName(Outcome o) {
    return std::array<std::string, 4>{"ok", "reject", "timeout", "cancel"}[static_cast<size_t>(o)];
}
enum class State { kIdle, kAuthenticated, kAllocated, kTraveling, kLoaded, kReady, kFailed };
struct Session {
    Principal principal;
    Intent intent;
    std::string operation, requestId;
    State state = State::kIdle;
    std::optional<Handle> handle;
    bool acquiredNew = false, compensationDone = false;
    std::array<int, 5> attempts{};
    std::vector<std::string> trace{"Idle"};
    std::string reason;
};
struct AuthDecision { Outcome outcome; Principal principal; };
// AuthFn代表可信服务端认证+授权接口；request.player本身绝不构成可信身份。
using AuthFn = std::function<AuthDecision(const Request&, int)>;
using StepFn = std::function<Outcome(Step, int)>;
enum class Code { kReady, kFailed, kDenied, kConflict, kStale, kMalformed };
struct Reply { Code code; std::shared_ptr<Session> session; };
struct RequestKey {
    Principal principal; std::string operation, requestId;
    bool operator<(const RequestKey& b) const {
        return std::tie(principal, operation, requestId) < std::tie(b.principal, b.operation, b.requestId);
    }
};
struct EntryEngine {
    World world;
    std::map<RequestKey, std::shared_ptr<Session>> cache;
    static constexpr int kMaxAttempts = 3;
    int maxAttempts = kMaxAttempts;
    static bool Id(const std::string& x) {
        if (x.empty() || x.size() > 64) return false;
        for (char c : x) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
        return true;
    }
    void Rollback(Session& s) {
        if (s.state == State::kReady || s.compensationDone) return;
        bool released = false;
        if (s.acquiredNew && s.handle) released = world.ReleaseExpected(*s.handle);
        s.compensationDone = true; s.state = State::kFailed;
        s.trace.push_back(released ? "Rollback(released-own-generation)" : "Rollback(no-owned-current-resource)");
    }
    void Cancel(Session& s) {
        if (s.state == State::kReady || s.compensationDone) return;
        s.reason = "cancelled"; Rollback(s);
    }
    Reply Enter(const Request& r, const AuthFn& authenticate, const StepFn& step) {
        auto s = std::make_shared<Session>();
        s->intent = r.intent; s->operation = r.operation; s->requestId = r.requestId;
        if (!Id(r.requestId) || (r.operation != "enter" && r.operation != "reconnect") ||
            !Id(r.intent.player) || !Id(r.intent.match) || !Id(r.intent.character) || !Id(r.intent.region) || maxAttempts < 1 || maxAttempts > kMaxAttempts) {
            s->reason = "malformed"; Rollback(*s); return {Code::kMalformed, s};
        }
        // 每次调用都重新认证。凭据可撤销；缓存不是绕过鉴权的捷径。
        AuthDecision auth{Outcome::kReject, {}};
        for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
            s->attempts[0] = attempt; auth = authenticate(r, attempt);
            s->trace.push_back("Auth:" + OutcomeName(auth.outcome));
            if (auth.outcome != Outcome::kTimeout) break;
        }
        if (auth.outcome != Outcome::kOk || !Id(auth.principal.tenant) || !Id(auth.principal.subject) ||
            auth.principal.subject != r.intent.player) {
            s->reason = "auth_denied"; Rollback(*s); return {Code::kDenied, s};
        }
        s->principal = auth.principal; s->state = State::kAuthenticated;
        const RequestKey key{auth.principal, r.operation, r.requestId};
        const auto found = cache.find(key);
        if (found != cache.end()) {
            const auto& old = found->second;
            if (!(old->intent == r.intent)) return {Code::kConflict, nullptr};
            if (old->state == State::kReady) {
                // 旧Ready是历史结果，不重新赋权，也不偷偷重建已释放的资源。
                if (!old->handle || !world.Current(*old->handle)) return {Code::kStale, old};
                return {Code::kReady, old};
            }
            return {Code::kFailed, old};
        }
        cache.emplace(key, s);
        for (Step st : {Step::kAllocate, Step::kTravel, Step::kLoad, Step::kSpawn}) {
            const auto i = static_cast<size_t>(st);
            Outcome result = Outcome::kTimeout;
            for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
                s->attempts[i] = attempt; result = step(st, attempt);
                s->trace.push_back(StepName(st) + ":" + OutcomeName(result));
                if (result != Outcome::kTimeout) break;
            }
            if (result != Outcome::kOk) {
                s->reason = StepName(st) + ":" + OutcomeName(result);
                Rollback(*s); return {Code::kFailed, s};
            }
            if (st == Step::kAllocate) {
                Handle h;
                const auto a = world.AcquireSeat(s->principal, s->intent, h);
                if (a != Acquisition::kNew && a != Acquisition::kBorrowed) {
                    s->reason = a == Acquisition::kNoCapacity ? "no_capacity" :
                                a == Acquisition::kConflict ? "active_intent_conflict" : "epoch_exhausted";
                    Rollback(*s); return {Code::kFailed, s}; // 不进入后续业务步骤
                }
                s->handle = h; s->acquiredNew = a == Acquisition::kNew;
                s->trace.push_back(s->acquiredNew ? "Allocated(new)" : "Allocated(borrowed)");
                s->state = State::kAllocated;
            } else if (st == Step::kTravel) s->state = State::kTraveling;
            else if (st == Step::kLoad) s->state = State::kLoaded;
            else {
                if (!s->handle || !world.Current(*s->handle)) {
                    s->reason = "stale_before_ready"; Rollback(*s); return {Code::kStale, s};
                }
                s->state = State::kReady; s->trace.push_back("Ready");
            }
        }
        return {Code::kReady, s};
    }
};
static Request Req(const std::string& player = "p1", const std::string& id = "req-1", const std::string& op = "enter") {
    return {"synthetic-" + player, op, id, {player, "match-1", "char-1", "region-1"}};
}
static AuthDecision Auth(const Request& r, int) {
    // 独立白名单是测试夹具；不把任意credential后缀当成真实身份认证。
    const std::map<std::string, Principal> registry{{"synthetic-p1", {"tenant-1", "p1"}},
        {"synthetic-p2", {"tenant-1", "p2"}}, {"synthetic-p3", {"tenant-1", "p3"}}};
    const auto it = registry.find(r.credential);
    return it == registry.end() ? AuthDecision{Outcome::kReject, {}} : AuthDecision{Outcome::kOk, it->second};
}
static Outcome Ok(Step, int) { return Outcome::kOk; }
static void TestIdentity() {
    EntryEngine e;
    int authCalls = 0, businessCalls = 0;
    auto auth = [&](const Request& r, int n) { ++authCalls; return Auth(r, n); };
    auto steps = [&](Step, int) { ++businessCalls; return Outcome::kOk; };
    const auto first = e.Enter(Req(), auth, steps);
    const auto second = e.Enter(Req(), auth, steps);
    Check(first.code == Code::kReady && first.session == second.session && authCalls == 2 && businessCalls == 4 &&
          e.world.Total() == 1 && e.world.Invariant(), "same trusted principal/key/intent: reauthenticate, reuse result, no business replay");
    auto deniedReq = Req(); deniedReq.credential = "revoked";
    const auto denied = e.Enter(deniedReq, auth, steps);
    Check(denied.code == Code::kDenied && denied.session != first.session && !denied.session->handle && businessCalls == 4,
          "revoked credential cannot retrieve cached Ready or its handle");
    const auto other = e.Enter(Req("p2"), auth, steps);
    Check(other.code == Code::kReady && other.session != first.session &&
          !(other.session->handle->principal == first.session->handle->principal) && e.world.Total() == 2,
          "cross-principal identical requestId is isolated");
    auto impersonate = Req(); impersonate.credential = "synthetic-p2";
    Check(e.Enter(impersonate, Auth, Ok).code == Code::kDenied, "claimed player must match authenticated principal");
    const auto snapshot = e.world.Snapshot();
    for (int field = 0; field < 3; ++field) {
        auto changed = Req();
        if (field == 0) changed.intent.match = "match-2";
        if (field == 1) changed.intent.character = "char-2";
        if (field == 2) changed.intent.region = "region-2";
        const auto r = e.Enter(changed, Auth, Ok);
        Check(r.code == Code::kConflict && !r.session && snapshot == e.world.Snapshot(), "same key different full intent field rejected: " + std::to_string(field));
    }
    const auto reconnect = e.Enter(Req("p1", "req-2", "reconnect"), Auth, Ok);
    Check(reconnect.code == Code::kReady && !reconnect.session->acquiredNew &&
          reconnect.session->handle == first.session->handle && e.world.Total() == 2,
          "new reconnect request borrows the same ready resource without another seat");
    const auto op = e.Enter(Req("p1", "req-1", "reconnect"), Auth, Ok);
    Check(op.session != first.session && op.code == Code::kReady, "operation participates in the idempotency key");
    const auto tenantAuth = [](const Request&, int) { return AuthDecision{Outcome::kOk, {"tenant-2", "p1"}}; };
    const auto tenant = e.Enter(Req(), tenantAuth, Ok);
    Check(tenant.code == Code::kReady && tenant.session != first.session && e.world.Total() == 3,
          "tenant participates in trusted principal scope");
    e.Cancel(*first.session);
    Check(first.session->state == State::kReady && e.world.Total() == 3, "Ready cancellation is a no-op");
}
static void TestFailureMatrix() {
    for (Step failAt : {Step::kAuth, Step::kAllocate, Step::kTravel, Step::kLoad, Step::kSpawn}) {
        for (Outcome outcome : {Outcome::kReject, Outcome::kTimeout, Outcome::kCancel}) {
            for (bool existing : {false, true}) {
                EntryEngine e;
                std::shared_ptr<Session> ready;
                if (existing) ready = e.Enter(Req(), Auth, Ok).session;
                const auto before = e.world.Snapshot();
                std::array<int, 5> calls{};
                auto auth = [&](const Request& r, int n) {
                    ++calls[0]; return failAt == Step::kAuth ? AuthDecision{outcome, {}} : Auth(r, n);
                };
                auto step = [&](Step st, int) { ++calls[static_cast<size_t>(st)]; return st == failAt ? outcome : Outcome::kOk; };
                auto reply = e.Enter(Req("p1", "req-fail"), auth, step);
                bool stopped = true;
                for (size_t i = static_cast<size_t>(failAt) + 1; i < calls.size(); ++i) stopped &= calls[i] == 0;
                const auto after = e.world.Snapshot();
                e.Cancel(*reply.session); e.Cancel(*reply.session); e.Rollback(*reply.session);
                bool oldUnchanged = !existing || (e.world.Current(*ready->handle) && ready->state == State::kReady && before == after);
                const bool releasedNew = existing || e.world.Total() == 0;
                Check(reply.code == (failAt == Step::kAuth ? Code::kDenied : Code::kFailed) && stopped &&
                      oldUnchanged && releasedNew && e.world.Invariant() && after == e.world.Snapshot() &&
                      calls[static_cast<size_t>(failAt)] == (outcome == Outcome::kTimeout ? 3 : 1),
                      "failure matrix " + StepName(failAt) + "/" + OutcomeName(outcome) +
                      (existing ? " preserves old Ready handle/load/epoch" : " releases only newly acquired seat"));
            }
        }
    }
}
static void TestLateAndStale() {
    EntryEngine e;
    auto r = Req("p1", "old-denied"); r.credential = "revoked";
    auto old = e.Enter(r, Auth, Ok);
    auto ready = e.Enter(Req("p1", "new-ready"), Auth, Ok);
    const auto before = e.world.Snapshot();
    e.Cancel(*old.session);
    Check(e.world.Snapshot() == before && e.world.Current(*ready.session->handle), "late cancel of an early denied request cannot release a later Ready");
    const auto first = *ready.session->handle;
    Session delayed; delayed.state = State::kAllocated; delayed.handle = first; delayed.acquiredNew = true;
    Check(e.world.ReleaseExpected(first), "explicit normal exit releases the matching generation");
    auto newer = e.Enter(Req("p1", "newer-ready"), Auth, Ok);
    const auto newSnapshot = e.world.Snapshot();
    e.Cancel(delayed); e.Cancel(delayed);
    Check(newer.session->handle->epoch > first.epoch && newSnapshot == e.world.Snapshot() &&
          e.world.Current(*newer.session->handle), "late compensation owning an older generation cannot release newer Ready");
    const auto replay = e.Enter(Req("p1", "new-ready"), Auth, Ok);
    Check(replay.code == Code::kStale && !e.world.Current(*replay.session->handle) && newSnapshot == e.world.Snapshot(),
          "replay of historical Ready returns stale without restoring write authority");
    Handle forged = *newer.session->handle; forged.ds = "ds-4";
    Check(!e.world.ReleaseExpected(forged) && e.world.Snapshot() == newSnapshot, "release also checks resource identity, not only generation");
    EntryEngine full; full.world.capacity = 0;
    int later = 0;
    auto noRoom = full.Enter(Req(), Auth, [&](Step st, int) { if (st != Step::kAllocate) ++later; return Outcome::kOk; });
    Check(noRoom.code == Code::kFailed && noRoom.session->reason == "no_capacity" && later == 0 && full.world.Total() == 0,
          "no capacity terminates immediately, never reaches Travel/Load/Spawn");
    EntryEngine flaky;
    auto retried = flaky.Enter(Req(), Auth, [](Step st, int n) { return st == Step::kTravel && n < 3 ? Outcome::kTimeout : Outcome::kOk; });
    Check(retried.code == Code::kReady && retried.session->attempts[2] == 3 && flaky.world.Total() == 1, "bounded timeout retries reuse one acquisition");
    auto conflict = Req("p1", "other-match"); conflict.intent.match = "match-2";
    auto rejected = flaky.Enter(conflict, Auth, Ok);
    Check(rejected.code == Code::kFailed && rejected.session->reason == "active_intent_conflict" && flaky.world.Total() == 1,
          "new request cannot borrow an existing seat for a different match intent");
    EntryEngine exhausted; exhausted.world.lastEpoch = std::numeric_limits<uint64_t>::max();
    Check(exhausted.Enter(Req(), Auth, Ok).code == Code::kFailed && exhausted.world.Total() == 0, "epoch exhaustion fails without wrap or seat allocation");
    for (int attempts : {0, 4, std::numeric_limits<int>::max()}) {
        EntryEngine invalid; invalid.maxAttempts = attempts;
        int calls = 0;
        auto neverAuth = [&](const Request& request, int n) { ++calls; return Auth(request, n); };
        auto neverStep = [&](Step, int) { ++calls; return Outcome::kTimeout; };
        const auto state = invalid.world.Snapshot();
        const auto result = invalid.Enter(Req(), neverAuth, neverStep);
        Check(result.code == Code::kMalformed && calls == 0 && invalid.cache.empty() &&
              invalid.world.Snapshot() == state && invalid.world.Invariant(),
              "invalid retry bound rejects before callbacks/allocation: " + std::to_string(attempts));
    }
    // 非空正控制与故障负控制：oracle必须能发现账实不符。
    World corrupt = flaky.world; ++corrupt.load.begin()->second;
    Check(!corrupt.Invariant(), "negative control: per-DS invariant detects injected accounting corruption");
}
int main() {
    std::puts("entry_session: synthetic auth; serialized request ownership model, no UE entity lifecycle claim");
    TestIdentity(); TestFailureMatrix(); TestLateAndStale();
    std::printf("RESULT pass=%d fail=%d\n", passed, failed);
    return failed ? 1 : 0;
}
