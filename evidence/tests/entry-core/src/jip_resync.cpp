// Finite serial C++17 teaching model: a replication scope, not a whole-world revision.
// Trusted application code supplies Context/Plan; comparing fields is NOT authentication.
// No transport, serialization, persistence, UE adapter, concurrency or CPU benchmark.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

using Seq = std::uint64_t;
constexpr Seq kMax = std::numeric_limits<Seq>::max();
constexpr std::size_t kEntities = 16, kPending = 8, kRecent = 8, kLog = 64;
constexpr Seq kSpan = 32, kWait = 10;
constexpr double kValueLimit = 1000000.0;

struct Stream {
    Seq world = 0, incarnation = 0, scope = 0;
    std::uint32_t schema = 1;
};
static bool operator==(const Stream& a, const Stream& b) {
    return a.world == b.world && a.incarnation == b.incarnation &&
           a.scope == b.scope && a.schema == b.schema;
}
struct Context { Stream stream; Seq session = 0; };
static bool operator==(const Context& a, const Context& b) {
    return a.stream == b.stream && a.session == b.session;
}
struct Identity { Context context; Seq transfer = 0; };
static bool operator==(const Identity& a, const Identity& b) {
    return a.context == b.context && a.transfer == b.transfer;
}
struct Entity {
    int id = 0;
    Seq generation = 1;
    double x = 0, y = 0, hp = 100;
    bool alive = true;
};
static bool operator==(const Entity& a, const Entity& b) {
    return a.id == b.id && a.generation == b.generation && a.x == b.x &&
           a.y == b.y && a.hp == b.hp && a.alive == b.alive;
}
struct State {
    bool installed = false;
    Context context;
    Seq seq = 0;                       // only committed cursor; no second World.rev
    std::vector<Entity> entities;      // fixed slots, including dead records
};
static bool SameState(const State& a, const State& b) {
    return a.installed == b.installed && a.context == b.context && a.seq == b.seq &&
           a.entities == b.entities;  // all schema fields; numeric +0 and -0 are equal
}
struct Snapshot { Identity identity; Seq seq = 0; std::vector<Entity> entities; };
static bool SameSnapshot(const Snapshot& a, const Snapshot& b) {
    return a.identity == b.identity && a.seq == b.seq && a.entities == b.entities;
}
enum class Field : std::uint8_t { Hp, Position, Spawn, Despawn };
struct Delta {
    Identity identity;
    Seq seq = 0;
    int id = 0;
    Seq generation = 1;
    Field field = Field::Hp;
    double a = 0, b = 0; // Hp: a; Position/Spawn: x,y; Spawn resets hp to 100
};
static bool operator==(const Delta& a, const Delta& b) {
    return a.identity == b.identity && a.seq == b.seq && a.id == b.id &&
           a.generation == b.generation && a.field == b.field && a.a == b.a && a.b == b.b;
}
struct Plan { Identity identity; Seq base = 0, target = 0; };
static bool operator==(const Plan& a, const Plan& b) {
    return a.identity == b.identity && a.base == b.base && a.target == b.target;
}
struct Ack { Identity identity; Seq target = 0; };
enum class Phase { Idle, ReceivingSnapshot, WaitingForGap, CaughtUp, Live, NeedSnapshot };
enum class Result {
    Ok, Buffered, Duplicate, Stale, CaughtUp, Ready, NeedSnapshot,
    WrongIdentity, Invalid, Conflict, Capacity, Span, Expired, ClockBackwards,
    Exhausted, WrongPhase
};
static bool ValidContext(const Context& c) {
    return c.stream.world != 0 && c.stream.incarnation != 0 && c.stream.scope != 0 &&
           c.stream.schema == 1 && c.session != 0;
}
static bool Position(double v) { return std::isfinite(v) && std::abs(v) <= kValueLimit; }
static bool Health(double v) { return std::isfinite(v) && v >= 0 && v <= kValueLimit; }
static bool ValidEntities(const std::vector<Entity>& entities) {
    if (entities.size() > kEntities) return false;
    for (std::size_t i = 0; i < entities.size(); ++i) {
        const auto& e = entities[i];
        if (e.id != static_cast<int>(i) || e.generation == 0 ||
            !Position(e.x) || !Position(e.y) || !Health(e.hp)) return false;
    }
    return true;
}
static bool ValidDelta(const Delta& d) {
    if (d.seq == 0 || d.id < 0 || static_cast<std::size_t>(d.id) >= kEntities ||
        d.generation == 0 || !std::isfinite(d.a) || !std::isfinite(d.b)) return false;
    switch (d.field) {
        case Field::Hp: return Health(d.a) && d.b == 0;
        case Field::Position: case Field::Spawn: return Position(d.a) && Position(d.b);
        case Field::Despawn: return d.a == 0 && d.b == 0;
    }
    return false;
}
static Result NextSequence(Seq current, Seq& out) {
    if (current == kMax) return Result::Exhausted;
    out = current + 1;
    return Result::Ok;
}
// Only called on a candidate. A malformed/lifecycle-invalid delta never consumes seq.
static Result Apply(State& state, const Delta& d) {
    if (!ValidDelta(d) || static_cast<std::size_t>(d.id) >= state.entities.size()) return Result::Invalid;
    Seq next = 0;
    if (NextSequence(state.seq, next) != Result::Ok) return Result::Exhausted;
    if (d.seq != next) return Result::Invalid;
    Entity& e = state.entities[static_cast<std::size_t>(d.id)];
    if (d.field == Field::Spawn) {
        if (e.generation == kMax) return Result::Exhausted;
        if (e.alive || d.generation != e.generation + 1) return Result::Invalid;
        e = Entity{e.id, d.generation, d.a, d.b, 100, true};
    } else {
        if (!e.alive || d.generation != e.generation) return Result::Invalid;
        switch (d.field) {
            case Field::Hp: e.hp = d.a; break;
            case Field::Position: e.x = d.a; e.y = d.b; break;
            case Field::Despawn: e.alive = false; break;
            case Field::Spawn: return Result::Invalid;
        }
    }
    state.seq = d.seq;
    return Result::Ok;
}

struct Client {
    Context expected;                 // supplied by trusted application session
    State state;
    Plan plan;
    bool active = false, hasBaseline = false, waiting = false, handedOff = false;
    Snapshot baseline;
    Phase phase = Phase::Idle;
    Seq lastTransfer = 0, lastNow = 0, deadline = 0;
    std::map<Seq, Delta> pending, recent;

    explicit Client(Context context) : expected(context) {}
    // Trusted control-plane rebind requires a different, never-reused Context.
    // Only current equality is enforced here; lifetime uniqueness belongs to the application.
    Result Rebind(Context context) {
        if (!ValidContext(context)) return Result::Invalid;
        if (context == expected) return Result::Conflict;
        *this = Client(context);
        return Result::Ok;
    }
    bool Ready() const { return phase == Phase::Live; }
    Result Fail(Result r) { phase = Phase::NeedSnapshot; return r; }
    Result Guard(const Identity& identity, Seq now) {
        if (!active || !(identity == plan.identity)) return Result::WrongIdentity;
        if (phase == Phase::NeedSnapshot) return Result::NeedSnapshot;
        if (now < lastNow) return Result::ClockBackwards;
        lastNow = now; // observe trusted time, including duplicates/stale/data rejection; no deadline extension
        if (waiting && now >= deadline) return Fail(Result::Expired);
        return Result::Ok;
    }
    Result Begin(const Plan& request, Seq now) {
        if (!ValidContext(expected) || !(request.identity.context == expected) || request.identity.transfer == 0)
            return Result::WrongIdentity;
        if (now < lastNow) return Result::ClockBackwards;
        if (request.base > request.target || request.target - request.base > kSpan) return Result::Span;
        if (now > kMax - kWait) return Result::Exhausted;
        lastNow = now; // valid control-plane request observes time even if stale/conflicting
        if (active && request.identity.transfer == lastTransfer)
            return request == plan ? Result::Duplicate : Result::Conflict;
        if (request.identity.transfer <= lastTransfer) return Result::Stale;
        if (state.installed && request.base < state.seq) return Result::Stale;
        plan = request; active = true; hasBaseline = false; handedOff = false;
        baseline = Snapshot{}; pending.clear(); recent.clear();
        lastTransfer = request.identity.transfer; lastNow = now; deadline = now + kWait;
        waiting = true; phase = Phase::ReceivingSnapshot;
        return Result::Ok;
    }
    // Drain a whole contiguous run on local copies. Error preserves ALL prior state/pending.
    Result Drain(State& candidate, std::map<Seq, Delta>& queued,
                 std::map<Seq, Delta>& remembered, bool live) const {
        while (candidate.seq != kMax && (live || candidate.seq < plan.target)) {
            const auto found = queued.find(candidate.seq + 1);
            if (found == queued.end()) break;
            const Result result = Apply(candidate, found->second);
            if (result != Result::Ok) return result;
            remembered[found->first] = found->second;
            queued.erase(found);
            if (remembered.size() > kRecent) remembered.erase(remembered.begin());
        }
        return Result::Ok;
    }
    void Publish(State candidate, std::map<Seq, Delta> queued,
                 std::map<Seq, Delta> remembered, bool live, Seq now) {
        state = std::move(candidate); pending = std::move(queued); recent = std::move(remembered);
        lastNow = now;
        phase = live ? (pending.empty() ? Phase::Live : Phase::WaitingForGap)
                     : (state.seq == plan.target ? Phase::CaughtUp : Phase::WaitingForGap);
    }
    Result Install(const Snapshot& snapshot, Seq now) {
        const Result guard = Guard(snapshot.identity, now);
        if (guard != Result::Ok) return guard;
        if (snapshot.seq != plan.base) return Result::Stale;
        if (!ValidEntities(snapshot.entities)) return Fail(Result::Invalid);
        if (hasBaseline) return SameSnapshot(snapshot, baseline) ? Result::Duplicate : Fail(Result::Conflict);
        State candidate{true, expected, snapshot.seq, snapshot.entities};
        auto queued = pending; auto remembered = recent;
        const Result drained = Drain(candidate, queued, remembered, false);
        if (drained != Result::Ok) return Fail(drained);
        baseline = snapshot; hasBaseline = true;
        Publish(std::move(candidate), std::move(queued), std::move(remembered), false, now);
        return phase == Phase::CaughtUp ? Result::CaughtUp : Result::Ok;
    }
    Result Deliver(const Delta& delta, Seq now) {
        const Result guard = Guard(delta.identity, now);
        if (guard != Result::Ok) return guard;
        if (!ValidDelta(delta)) return Fail(Result::Invalid);
        const Seq cursor = hasBaseline ? state.seq : plan.base;
        if (delta.seq <= cursor) {
            const auto old = recent.find(delta.seq);
            if (old == recent.end()) return Result::Stale; // bounded history; no forever-conflict claim
            return old->second == delta ? Result::Duplicate : Fail(Result::Conflict);
        }
        const auto old = pending.find(delta.seq);
        if (old != pending.end()) return old->second == delta ? Result::Duplicate : Fail(Result::Conflict);
        if (delta.seq - cursor > kSpan) return Fail(Result::Span);
        // Contiguous delivery may drain an already full queue; it needs no extra retained slot.
        const bool immediate = hasBaseline && delta.seq == cursor + 1 &&
                               (handedOff || cursor < plan.target);
        if (!immediate && pending.size() >= kPending) return Fail(Result::Capacity);
        auto queued = pending; auto remembered = recent;
        State candidate = state;
        queued.emplace(delta.seq, delta);
        if (!hasBaseline) { pending = std::move(queued); lastNow = now; return Result::Buffered; }
        const bool live = handedOff;
        const Result drained = Drain(candidate, queued, remembered, live);
        if (drained != Result::Ok) return Fail(drained);
        if (live && !queued.empty() && !waiting) {
            if (now > kMax - kWait) return Fail(Result::Exhausted);
            deadline = now + kWait;
        }
        if (live) waiting = !queued.empty();
        Publish(std::move(candidate), std::move(queued), std::move(remembered), live, now);
        return phase == Phase::CaughtUp ? Result::CaughtUp :
               (phase == Phase::WaitingForGap ? Result::Buffered : Result::Ok);
    }
    Result Tick(Seq now) {
        if (!active) return Result::WrongPhase;
        const Result guard = Guard(plan.identity, now);
        if (guard == Result::Ok) lastNow = now;
        return guard;
    }
    Result AcceptAck(const Ack& ack, Seq now) {
        const Result guard = Guard(ack.identity, now);
        if (guard != Result::Ok) return guard;
        if (ack.target != plan.target) return Result::WrongIdentity;
        if (handedOff) return Result::Duplicate;
        if (phase != Phase::CaughtUp || state.seq != plan.target) return Result::WrongPhase;
        State candidate = state; auto queued = pending; auto remembered = recent;
        const Result drained = Drain(candidate, queued, remembered, true);
        if (drained != Result::Ok) return Fail(drained);
        if (!queued.empty() && now > kMax - kWait) return Fail(Result::Exhausted);
        handedOff = true; waiting = !queued.empty(); if (waiting) deadline = now + kWait;
        Publish(std::move(candidate), std::move(queued), std::move(remembered), true, now);
        return Ready() ? Result::Ready : Result::Buffered;
    }
    // Explicit end-of-transfer check. A missing tail can have an EMPTY pending map.
    Result FinishInput() {
        if (phase == Phase::NeedSnapshot) return Result::NeedSnapshot;
        if (!active || handedOff) return Result::WrongPhase;
        if (!hasBaseline || state.seq != plan.target) return Fail(Result::NeedSnapshot);
        return Result::CaughtUp;
    }
};

// first is the inclusive retained lower bound only for a nonempty window.
// Empty window: first is ignored (head+1 might not be representable); require R==T and head>=T.
struct Window { Identity identity; Seq first = 0, head = 0; std::vector<Delta> deltas; };
// Deliberately prefix-committing batch helper, NOT an all-or-nothing world transaction.
// Each successful Install/Deliver is committed; any gap/error locks Ready until a new transfer.
static Result CatchUp(Client& client, const Plan& plan, const Snapshot& snapshot,
                      const Window& window, Seq now) {
    if (!(window.identity == plan.identity) || !(snapshot.identity == plan.identity)) return Result::WrongIdentity;
    const Result begin = client.Begin(plan, now);
    if (begin != Result::Ok) return begin;
    if (window.deltas.size() > kLog || window.head < plan.target ||
        (!window.deltas.empty() && window.first > window.head))
        return client.Fail(Result::NeedSnapshot);
    if (plan.base < plan.target && (window.first > plan.base + 1 || window.deltas.empty()))
        return client.Fail(Result::NeedSnapshot);
    const Result install = client.Install(snapshot, now);
    if (install != Result::Ok && install != Result::CaughtUp) return install;
    for (const auto& delta : window.deltas) {
        if (!(delta.identity == plan.identity) || delta.seq < window.first || delta.seq > window.head)
            return client.Fail(Result::Invalid);
        if (delta.seq <= plan.base) continue; // T+1 and later remain bounded until ACK, including batch input
        const Result delivered = client.Deliver(delta, now);
        if (delivered != Result::Ok && delivered != Result::CaughtUp && delivered != Result::Duplicate && delivered != Result::Buffered)
            return delivered;
    }
    return client.FinishInput();
}

static int passed = 0, failed = 0;
static void Check(bool ok, const std::string& name) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if (ok) ++passed; else ++failed;
}
static Context TestContext() { return {{7, 3, 11, 1}, 19}; }
static Identity Key(Seq transfer = 1) { return {TestContext(), transfer}; }
static Snapshot Base(Seq transfer = 1, Seq seq = 10) {
    return {Key(transfer), seq, {{0, 1, 0, 0, 100, true}, {1, 4, 2, -3, 80, true}, {2, 9, 7, 8, 0, false}}};
}
static Delta D(Seq seq, int id, Seq generation, Field field, double a = 0, double b = 0, Seq transfer = 1) {
    return {Key(transfer), seq, id, generation, field, a, b};
}
static std::vector<Delta> Events(Seq transfer = 1) {
    return {D(11, 0, 1, Field::Hp, 73.5, 0, transfer),
            D(12, 1, 4, Field::Position, 12, -4.5, transfer),
            D(13, 0, 1, Field::Despawn, 0, 0, transfer),
            D(14, 0, 2, Field::Spawn, 3, 4, transfer),
            D(15, 0, 2, Field::Hp, 42, 0, transfer),
            D(16, 1, 4, Field::Despawn, 0, 0, transfer)};
}
// Hand-written expected vectors. Never call Apply/Deliver to derive the oracle.
static State Expected(Seq seq = 16) {
    return {true, TestContext(), seq, {{0, 2, 3, 4, 42, true}, {1, 4, 12, -4.5, 80, false}, {2, 9, 7, 8, 0, false}}};
}
static Plan P(Seq target = 16, Seq transfer = 1, Seq base = 10) { return {Key(transfer), base, target}; }
static Window W(std::vector<Delta> deltas, Seq first = 11, Seq head = 16, Seq transfer = 1) {
    return {Key(transfer), first, head, std::move(deltas)};
}
static bool InitialInstalled(const Client& c) {
    return SameState(c.state, State{true, TestContext(), 10, Base().entities});
}
static void TestInstallationAndWindow() {
    Client zero(TestContext());
    Check(CatchUp(zero, P(10), Base(), W({}, 10, 10), 0) == Result::CaughtUp &&
          InitialInstalled(zero) && !zero.Ready(), "J01 nonempty snapshot with zero deltas installs before CaughtUp");
    Check(zero.AcceptAck({Key(), 10}, 1) == Result::Ready && zero.Ready(), "J02 only matching completion ACK opens input");
    Snapshot empty{Key(), 10, {}}; Client e(TestContext());
    Check(CatchUp(e, P(10), empty, W({}, 10, 10), 0) == Result::CaughtUp && e.state.installed &&
          e.state.entities.empty() && e.state.seq == 10, "J03 empty snapshot is also an installed baseline");
    Client oldLog(TestContext());
    Check(CatchUp(oldLog, P(10), Base(), W({D(9, 0, 1, Field::Hp, 99)}, 9, 10), 0) == Result::CaughtUp &&
          InitialInstalled(oldLog), "J04 historical log entirely covered by snapshot still installs");
    Client exact(TestContext());
    Check(CatchUp(exact, P(), Base(), W(Events()), 0) == Result::CaughtUp && SameState(exact.state, Expected()),
          "J05 exact R+1 window converges with HP position generation and dead records");
    auto missingHead = Events(); missingHead.erase(missingHead.begin()); Client fallback(TestContext());
    Check(CatchUp(fallback, P(), Base(), W(missingHead, 12), 0) == Result::NeedSnapshot && !fallback.Ready(),
          "J06 missing retained head requires a fresh snapshot");
    Snapshot fresh{Key(2), 16, Expected().entities};
    Check(CatchUp(fallback, P(16, 2, 16), fresh, W({}, 16, 16, 2), 1) == Result::CaughtUp &&
          SameState(fallback.state, Expected()), "J07 same client full-snapshot fallback installs latest zero-delta state");
    for (const auto missing : {0, 2, 5}) {
        auto log = Events(); log.erase(log.begin() + missing); Client gap(TestContext());
        Check(CatchUp(gap, P(), Base(), W(log), 0) == Result::NeedSnapshot && !gap.Ready() && gap.state.seq < 16,
              "J08 missing head/middle/tail cannot silently complete index=" + std::to_string(missing));
        Check(gap.AcceptAck({Key(), 16}, 1) == Result::NeedSnapshot, "J09 gap failure blocks ACK");
    }
    Client truncated(TestContext());
    Check(CatchUp(truncated, P(), Base(), W({}, 16, 16), 0) == Result::NeedSnapshot, "J10 truncated empty window with old base fails");
    Client badHead(TestContext());
    Check(CatchUp(badHead, P(), Base(), W(Events(), 11, 15), 0) == Result::NeedSnapshot, "J11 advertised head below target fails");
    Client future(TestContext());
    Check(future.Begin(P(9), 0) == Result::Span && !future.state.installed, "J12 future base above target is rejected");
}
static void TestOrderingAndHandoff() {
    for (unsigned seed = 0; seed != 12; ++seed) {
        auto events = Events();
        if (seed == 0) std::reverse(events.begin(), events.end());
        else { std::mt19937 random(seed); std::shuffle(events.begin(), events.end(), random); }
        Client c(TestContext()); c.Begin(P(), 0); c.Install(Base(), 0);
        for (const auto& d : events) c.Deliver(d, 1);
        Check(c.FinishInput() == Result::CaughtUp && SameState(c.state, Expected()) && c.pending.empty(),
              "J13 reverse/seeded permutation full-state convergence seed=" + std::to_string(seed));
    }
    Client staged(TestContext()); staged.Begin(P(), 0);
    staged.Deliver(Events()[1], 1); staged.Deliver(Events()[0], 1);
    Check(!staged.state.installed && staged.pending.size() == 2 && !staged.Ready(), "J14 arriving R+1/R+2 retained before snapshot install");
    Check(staged.Install(Base(), 2) == Result::Ok && staged.state.seq == 12 && staged.state.entities[0].hp == 73.5 &&
          staged.state.entities[1].x == 12 && staged.pending.empty(), "J15 installation drains already received contiguous deltas");
    staged.Deliver(Events()[5], 2); const auto buffered = staged.pending;
    Check(staged.Install(Base(), 2) == Result::Duplicate && staged.pending == buffered && staged.state.seq == 12,
          "J16 identical reinstallation preserves buffered work and committed prefix");
    Check(staged.Begin(P(), 2) == Result::Duplicate && staged.pending == buffered,
          "J17 identical Begin does not clear buffers or extend deadline");
    Check(staged.AcceptAck({Key(), 16}, 2) == Result::WrongPhase && !staged.Ready(), "J18 ACK before target completion rejected");
    for (std::size_t i = 2; i < 5; ++i) staged.Deliver(Events()[i], 3);
    Check(SameState(staged.state, Expected()) && staged.phase == Phase::CaughtUp, "J19 filling hole commits all contiguous events");
    const auto later = D(17, 0, 2, Field::Position, 0.0001, -0.0002);
    staged.Deliver(later, 3);
    Check(staged.state.seq == 16 && staged.pending.size() == 1 && !staged.Ready(), "J20 target T fixed while T+1 waits for ACK");
    Ack wrong{Key(), 17};
    Check(staged.AcceptAck(wrong, 3) == Result::WrongIdentity && staged.state.seq == 16, "J21 ACK target cannot follow moving head");
    Check(staged.AcceptAck({Key(2), 16}, 3) == Result::WrongIdentity, "J22 old/new transfer ACK mismatch rejected");
    State after = Expected(17); after.entities[0].x = 0.0001; after.entities[0].y = -0.0002;
    Check(staged.AcceptAck({Key(), 16}, 4) == Result::Ready && SameState(staged.state, after) && staged.pending.empty(),
          "J23 matching ACK hands off and drains T+1 without losing sub-millimeter values");
    Check(staged.AcceptAck({Key(), 16}, 4) == Result::Duplicate && SameState(staged.state, after), "J24 repeated ACK is idempotent");
    auto realtime = D(18, 0, 2, Field::Hp, 41); after.seq = 18; after.entities[0].hp = 41;
    Check(staged.Deliver(realtime, 4) == Result::Ok && SameState(staged.state, after), "J25 live updates continue after handoff");
    Check(staged.Deliver(realtime, 4) == Result::Duplicate && SameState(staged.state, after), "J26 exact applied duplicate is a no-op");
}
static void TestIdentityAndDuplicates() {
    for (int mismatch = 0; mismatch != 6; ++mismatch) {
        Client c(TestContext()); c.Begin(P(), 0); c.Install(Base(), 0); c.Deliver(Events()[2], 1);
        auto bad = Events()[0];
        if (mismatch == 0) ++bad.identity.context.stream.world;
        if (mismatch == 1) ++bad.identity.context.stream.incarnation;
        if (mismatch == 2) ++bad.identity.context.stream.scope;
        if (mismatch == 3) ++bad.identity.context.stream.schema;
        if (mismatch == 4) ++bad.identity.context.session;
        if (mismatch == 5) ++bad.identity.transfer;
        const State before = c.state; const auto queued = c.pending;
        Check(c.Deliver(bad, 1) == Result::WrongIdentity && SameState(c.state, before) && c.pending == queued &&
              c.phase == Phase::WaitingForGap, "J27 wrong world/epoch/scope/schema/session/transfer preserves state index=" + std::to_string(mismatch));
        auto badSnapshot = Base(); badSnapshot.identity = bad.identity;
        Check(c.Install(badSnapshot, 1) == Result::WrongIdentity && SameState(c.state, before) && c.pending == queued,
              "J28 wrong snapshot identity preserves buffered work index=" + std::to_string(mismatch));
        Check(c.AcceptAck({bad.identity, 16}, 1) == Result::WrongIdentity, "J29 wrong ACK complete identity rejected");
        for (const auto& d : Events()) c.Deliver(d, 2);
        Check(SameState(c.state, Expected()), "J30 valid transfer can continue after foreign packet");
    }
    Client duplicate(TestContext()); duplicate.Begin(P(), 0); duplicate.Install(Base(), 0);
    duplicate.Deliver(Events()[2], 1); const auto queued = duplicate.pending; const State before = duplicate.state;
    Check(duplicate.Deliver(Events()[2], 1) == Result::Duplicate && duplicate.pending == queued, "J31 exact pending duplicate remains singular");
    auto conflict = Events()[2]; conflict.field = Field::Hp; conflict.a = 999;
    Check(duplicate.Deliver(conflict, 1) == Result::Conflict && duplicate.pending == queued && SameState(duplicate.state, before) &&
          !duplicate.Ready(), "J32 conflicting pending duplicate never replaces original");
    Client recent(TestContext()); CatchUp(recent, P(), Base(), W(Events()), 0);
    auto appliedConflict = Events()[4]; appliedConflict.a = 999; const State done = recent.state;
    Check(recent.Deliver(appliedConflict, 1) == Result::Conflict && SameState(recent.state, done) && !recent.Ready(),
          "J33 recent applied conflict locks handoff without changing world");
    Client stale(TestContext()); CatchUp(stale, P(), Base(), W(Events()), 0);
    Check(stale.Begin(P(16, 2, 10), 1) == Result::Stale && SameState(stale.state, Expected()), "J34 new transfer cannot roll back same-context committed sequence");
    auto old = Base(); old.seq = 9;
    Check(stale.Install(old, 1) == Result::Stale && SameState(stale.state, Expected()), "J35 late old baseline rejected");
    auto corrupted = Base(); corrupted.entities[0].hp = 99;
    Check(stale.Install(corrupted, 1) == Result::Conflict && SameState(stale.state, Expected()), "J36 same baseline identity with different content conflicts");
    Context next = TestContext(); ++next.stream.incarnation;
    Check(stale.Rebind(next) == Result::Ok && !stale.state.installed && stale.pending.empty(), "J37 explicit trusted rebind clears unrelated old context");
    Plan nextPlan{{next, 1}, 1, 1}; Snapshot nextSnapshot{{next, 1}, 1, Base().entities};
    Check(stale.Begin(nextPlan, 0) == Result::Ok && stale.Install(nextSnapshot, 0) == Result::CaughtUp &&
          stale.state.context == next && stale.state.seq == 1, "J38 new epoch restarts sequence only after trusted rebind");
    Check(stale.Deliver(Events()[0], 1) == Result::WrongIdentity, "J39 old-context packet cannot switch receiver back");
}
static void TestInvalidAndAtomicity() {
    std::vector<Delta> invalid = {D(11, -1, 1, Field::Hp, 1), D(11, 3, 1, Field::Hp, 1),
        D(11, 0, 1, static_cast<Field>(255), 1), D(11, 0, 0, Field::Hp, 1),
        D(11, 0, 2, Field::Hp, 1), D(11, 2, 9, Field::Hp, 1), D(11, 0, 1, Field::Spawn),
        D(11, 0, 1, Field::Hp, -1), D(11, 0, 1, Field::Hp, kValueLimit + 1),
        D(11, 0, 1, Field::Position, std::numeric_limits<double>::infinity()),
        D(11, 0, 1, Field::Hp, std::numeric_limits<double>::quiet_NaN()),
        D(11, 0, 1, Field::Position, kValueLimit + 1), D(11, 0, 1, Field::Hp, 1, 2),
        D(0, 0, 1, Field::Hp, 1)};
    for (std::size_t i = 0; i < invalid.size(); ++i) {
        Client c(TestContext()); c.Begin(P(), 0); c.Install(Base(), 0); c.Deliver(Events()[2], 1);
        const State before = c.state; const auto queued = c.pending;
        Check(c.Deliver(invalid[i], 1) == Result::Invalid && SameState(c.state, before) && c.pending == queued && !c.Ready(),
              "J40 invalid delta preserves all fields/cursor/other pending index=" + std::to_string(i));
    }
    std::vector<Snapshot> invalidSnapshots;
    auto s = Base(); s.entities[1].id = 0; invalidSnapshots.push_back(s);
    s = Base(); s.entities[0].generation = 0; invalidSnapshots.push_back(s);
    s = Base(); s.entities[2].x = std::numeric_limits<double>::infinity(); invalidSnapshots.push_back(s);
    s = Base(); s.entities[2].hp = -1; invalidSnapshots.push_back(s);
    s = Base(); s.entities.resize(kEntities + 1); invalidSnapshots.push_back(s);
    s = Base(); s.entities[0].y = std::numeric_limits<double>::quiet_NaN(); invalidSnapshots.push_back(s);
    for (std::size_t i = 0; i < invalidSnapshots.size(); ++i) {
        Client c(TestContext()); c.Begin(P(), 0); c.Deliver(Events()[0], 1); const auto queued = c.pending;
        Check(c.Install(invalidSnapshots[i], 1) == Result::Invalid && !c.state.installed && c.pending == queued,
              "J41 invalid snapshot installs no partial world index=" + std::to_string(i));
    }
    Client atomic(TestContext()); atomic.Begin(P(12), 0); atomic.Install(Base(), 0);
    atomic.Deliver(D(12, 0, 2, Field::Hp, 1), 1); const State before = atomic.state; const auto queued = atomic.pending;
    Check(atomic.Deliver(Events()[0], 1) == Result::Invalid && SameState(atomic.state, before) && atomic.pending == queued,
          "J42 invalid buffered successor rolls back entire current drain call");
    Client prefix(TestContext()); auto broken = Events(); broken[2].generation = 99;
    Check(CatchUp(prefix, P(), Base(), W(broken), 0) == Result::Invalid && prefix.state.seq == 12 &&
          prefix.state.entities[0].hp == 73.5 && !prefix.Ready(), "J43 batch failure retains only earlier successful prefix; no whole-batch atomic claim");
    Client oldGeneration(TestContext()); CatchUp(oldGeneration, P(), Base(), W(Events()), 0); oldGeneration.AcceptAck({Key(), 16}, 1);
    Check(oldGeneration.Deliver(D(17, 0, 1, Field::Hp, 999), 1) == Result::Invalid && SameState(oldGeneration.state, Expected()) &&
          !oldGeneration.Ready(), "J44 old entity generation cannot write respawned slot");
}
static void TestBoundsTimeAndOracle() {
    Client count(TestContext()); count.Begin(P(20), 0); count.Install(Base(), 0);
    for (Seq seq = 12; seq <= 19; ++seq) count.Deliver(D(seq, 0, 1, Field::Hp, 1), 1);
    Check(count.pending.size() == kPending && count.state.seq == 10, "J45 pending count exact limit allowed");
    const auto queued = count.pending;
    Check(count.Deliver(D(20, 0, 1, Field::Hp, 1), 1) == Result::Capacity && count.pending == queued && !count.Ready(),
          "J46 pending limit plus one requests resync without eviction");
    Client fullDrain(TestContext()); fullDrain.Begin(P(19), 0); fullDrain.Install(Base(), 0);
    for (Seq seq = 12; seq <= 19; ++seq) fullDrain.Deliver(D(seq, 0, 1, Field::Hp, 1), 1);
    Check(fullDrain.Deliver(D(11, 0, 1, Field::Hp, 1), 1) == Result::CaughtUp && fullDrain.state.seq == 19 && fullDrain.pending.empty(),
          "J47 missing next delta can drain a full buffer");
    Check(fullDrain.recent.size() == kRecent && fullDrain.Deliver(D(11, 0, 1, Field::Hp, 999), 1) == Result::Stale,
          "J48 applied history is bounded; older payload conflict detection is explicitly unavailable");
    Client span(TestContext()); span.Begin(P(), 0); span.Install(Base(), 0);
    Check(span.Deliver(D(42, 0, 1, Field::Hp, 1), 1) == Result::Buffered && span.pending.size() == 1, "J49 exact future sequence span allowed");
    Check(span.Deliver(D(43, 0, 1, Field::Hp, 1), 1) == Result::Span && span.pending.size() == 1, "J50 excessive future span fails boundedly");
    Client time(TestContext()); time.Begin(P(), 5); time.Install(Base(), 5);
    Check(time.Tick(14) == Result::Ok && time.Tick(13) == Result::ClockBackwards && time.lastNow == 14,
          "J51 trusted time rollback rejected");
    Check(time.Tick(15) == Result::Expired && !time.Ready() && InitialInstalled(time), "J52 waiting deadline equality fails closed");
    Client ackTimeout(TestContext()); ackTimeout.Begin(P(10), 0); ackTimeout.Install(Base(), 0);
    Check(ackTimeout.AcceptAck({Key(), 10}, 10) == Result::Expired && !ackTimeout.Ready(), "J53 CaughtUp still needs ACK before transfer deadline");
    Client live(TestContext()); CatchUp(live, P(10), Base(), W({}, 10, 10), 0); live.AcceptAck({Key(), 10}, 1);
    Check(live.Tick(100) == Result::Ok, "J54 live idle client has no transfer timeout");
    live.Deliver(D(12, 0, 1, Field::Hp, 1), 100);
    Check(!live.Ready() && live.Tick(109) == Result::Ok && live.Tick(110) == Result::Expired && !live.Ready(), "J55 live gap opens bounded wait and expires at equality");
    Seq out = 123;
    Check(NextSequence(kMax, out) == Result::Exhausted && out == 123 &&
          NextSequence(kMax - 1, out) == Result::Ok && out == kMax, "J56 sequence exhaustion rejects wrap and preserves output");
    Client max(TestContext()); auto maxSnapshot = Base(1, kMax);
    Check(max.Begin(P(kMax, 1, kMax), 0) == Result::Ok && max.Install(maxSnapshot, 0) == Result::CaughtUp &&
          max.state.seq == kMax, "J57 snapshot at maximum sequence installs without R+1 overflow");
    Client overflow(TestContext());
    Check(overflow.Begin(P(), kMax - kWait + 1) == Result::Exhausted && !overflow.active, "J58 deadline overflow rejected before Begin");
    auto exhausted = Base(); exhausted.entities[2].generation = kMax; Client generation(TestContext());
    generation.Begin(P(11), 0); generation.Install(exhausted, 0);
    Check(generation.Deliver(D(11, 2, kMax, Field::Spawn), 1) == Result::Exhausted && generation.state.seq == 10 &&
          generation.state.entities[2].generation == kMax, "J59 generation exhaustion cannot wrap on spawn");
    Client planBound(TestContext());
    Check(planBound.Begin(P(43), 0) == Result::Span && !planBound.active, "J60 catchup target span is bounded");
    Client liveRecovery(TestContext()); CatchUp(liveRecovery, P(10), Base(), W({}, 10, 10), 0);
    liveRecovery.AcceptAck({Key(), 10}, 1); liveRecovery.Deliver(D(12, 0, 1, Field::Hp, 22), 2);
    Check(!liveRecovery.Ready() && liveRecovery.Deliver(D(11, 0, 1, Field::Hp, 11), 3) == Result::Ok &&
          liveRecovery.Ready() && liveRecovery.state.seq == 12 && liveRecovery.state.entities[0].hp == 22,
          "J64 live gap temporarily blocks input then resumes on valid contiguous fill");
    Client newTransfer(TestContext()); newTransfer.Begin(P(), 0); newTransfer.Deliver(Events()[2], 1);
    Check(newTransfer.Begin(P(10, 2), 2) == Result::Ok && newTransfer.pending.empty(), "J65 new trusted transfer explicitly abandons bounded old queue");
    newTransfer.Install(Base(2), 2); const State retained = newTransfer.state;
    Check(newTransfer.Deliver(Events()[0], 3) == Result::WrongIdentity &&
          newTransfer.AcceptAck({Key(), 10}, 3) == Result::WrongIdentity && SameState(newTransfer.state, retained),
          "J66 old transfer delta and ACK cannot disturb new baseline");
    Check(newTransfer.Begin(P(), 3) == Result::Stale && newTransfer.AcceptAck({Key(2), 10}, 3) == Result::Ready,
          "J67 old Begin rejected while new transfer still completes");
    Client during(TestContext()); during.Begin(P(), 0);
    during.Deliver(D(11, 0, 1, Field::Hp, 73.5), 1); during.Deliver(D(12, 0, 99, Field::Hp, 1), 1);
    const auto duringQueued = during.pending;
    Check(during.Install(Base(), 2) == Result::Invalid && !during.state.installed && during.pending == duringQueued,
          "J68 invalid pre-install buffered suffix prevents entire installation commit");
    Client windowBound(TestContext()); std::vector<Delta> oversized(kLog + 1, Events()[0]);
    Check(CatchUp(windowBound, P(), Base(), W(oversized), 0) == Result::NeedSnapshot && !windowBound.state.installed,
          "J69 source window itself has a finite record cap");
    Client validEdges(TestContext()); auto edges = Base(); edges.entities[0].x = -kValueLimit;
    edges.entities[0].y = kValueLimit; edges.entities[0].hp = kValueLimit;
    validEdges.Begin(P(10), 0);
    Check(validEdges.Install(edges, 0) == Result::CaughtUp && validEdges.state.entities == edges.entities,
          "J70 finite inclusive data boundaries are accepted");
    Client ackGap(TestContext()); CatchUp(ackGap, P(10), Base(), W({}, 10, 10), 0);
    ackGap.Deliver(D(12, 0, 1, Field::Hp, 22), 1);
    Check(ackGap.AcceptAck({Key(), 10}, 2) == Result::Buffered && !ackGap.Ready() && ackGap.state.seq == 10 &&
          ackGap.Deliver(D(11, 0, 1, Field::Hp, 11), 3) == Result::Ok && ackGap.Ready() && ackGap.state.seq == 12,
          "J71 ACK with buffered live gap never falsely returns Ready");
    Client duplicateTime(TestContext()); duplicateTime.Begin(P(), 0); duplicateTime.Install(Base(), 0);
    duplicateTime.Deliver(Events()[0], 2);
    Check(duplicateTime.Deliver(Events()[0], 9) == Result::Duplicate && duplicateTime.lastNow == 9 &&
          duplicateTime.Deliver(Events()[1], 8) == Result::ClockBackwards && duplicateTime.deadline == 10,
          "J72 duplicate observes trusted clock without extending deadline");
    Check(duplicateTime.Deliver(D(9, 0, 1, Field::Hp, 1), 9) == Result::Stale && duplicateTime.lastNow == 9 &&
          duplicateTime.Tick(10) == Result::Expired, "J73 covered stale event neither rewinds clock nor extends wait");
    Client batchFuture(TestContext()); auto movingLog = Events();
    movingLog.push_back(D(17, 0, 2, Field::Hp, 17));
    Check(CatchUp(batchFuture, P(), Base(), W(movingLog, 11, 17), 0) == Result::CaughtUp &&
          SameState(batchFuture.state, Expected()) && batchFuture.pending.size() == 1,
          "J74 batch helper also retains received T+1 without moving target");
    State futureExpected = Expected(17); futureExpected.entities[0].hp = 17;
    Check(batchFuture.AcceptAck({Key(), 16}, 1) == Result::Ready && SameState(batchFuture.state, futureExpected),
          "J75 batch T+1 survives until matching ACK handoff");
    Client sameContext(TestContext()); CatchUp(sameContext, P(), Base(), W(Events()), 0);
    const Plan originalPlan = sameContext.plan; const auto remembered = sameContext.recent;
    Check(sameContext.Rebind(TestContext()) == Result::Conflict && SameState(sameContext.state, Expected()) &&
          sameContext.plan == originalPlan && sameContext.recent == remembered && sameContext.lastTransfer == 1,
          "J76 same-context rebind cannot clear committed state or transfer history");
    Client emptyOnePast(TestContext());
    Check(CatchUp(emptyOnePast, P(10), Base(), W({}, 11, 10), 0) == Result::CaughtUp && InitialInstalled(emptyOnePast),
          "J77 empty window ignores first=head+1 when R equals T");
    Client emptyOld(TestContext());
    Check(CatchUp(emptyOld, P(), Base(), W({}, 17, 16), 0) == Result::NeedSnapshot && !emptyOld.state.installed,
          "J78 empty one-past window still rejects an uncovered R-to-T gap");
    Client emptyMax(TestContext());
    Check(CatchUp(emptyMax, P(kMax, 1, kMax), Base(1, kMax), W({}, kMax, kMax), 0) == Result::CaughtUp &&
          emptyMax.state.seq == kMax, "J79 empty window at maximum head never computes unrepresentable head+1");
    const State oracle = Expected();
    for (int field = 0; field != 12; ++field) {
        State changed = oracle;
        if (field == 0) changed.installed = false;
        if (field == 1) ++changed.seq;
        if (field == 2) ++changed.context.stream.world;
        if (field == 3) ++changed.context.stream.incarnation;
        if (field == 4) ++changed.context.stream.scope;
        if (field == 5) ++changed.context.stream.schema;
        if (field == 6) ++changed.context.session;
        if (field == 7) changed.entities.pop_back();
        if (field == 8) ++changed.entities[2].generation;
        if (field == 9) changed.entities[2].x += 0.0001;
        if (field == 10) changed.entities[2].hp += 0.0001;
        if (field == 11) changed.entities[2].alive = true;
        Check(!SameState(oracle, changed), "J61 full-state oracle detects dead/numeric/identity/count/seq changes field=" + std::to_string(field));
    }
    auto changed = oracle; ++changed.entities[2].id;
    Check(!SameState(oracle, changed), "J62 comparator also includes entity ID");
    changed = oracle; changed.entities[2].y += 0.0001;
    Check(!SameState(oracle, changed), "J63 comparator also includes sub-millimeter Y on dead record");
}
int main() {
    std::puts("== jip_resync: finite serial scope/transfer snapshot + delta contract ==");
    std::puts("No CPU benchmark, wire-byte estimate, transport, authentication or UE claim.");
    TestInstallationAndWindow(); TestOrderingAndHandoff(); TestIdentityAndDuplicates();
    TestInvalidAndAtomicity(); TestBoundsTimeAndOracle();
    std::printf("RESULT pass=%d fail=%d\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
