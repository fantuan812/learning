// Bounded single-service room contracts. No benchmarks or external services.
// Public Room& calls assume service ownership and unmodified model state.
#ifndef ROOM_SOURCE
#define ROOM_SOURCE "../src/room_fsm.cpp"
#endif
#define main retained_room_main
#include ROOM_SOURCE
#undef main
#include <set>
#include <tuple>
#include <utility>

namespace contract {
using MemberView = std::tuple<std::string, Slot, int, int64_t>;
struct ExpectedRoom {
    std::string id;
    RoomState state = RoomState::kMatched;
    std::vector<MemberView> members;
    int64_t deadline = 30, created = 0, revision = 0;
    int settles = 0;
    std::vector<std::string> log = {"Matched"}, requeued;
    bool owned = false, invalidated = false;
    auto Tuple() const {
        return std::tie(id, state, members, deadline, created, revision, settles,
                        log, requeued, owned, invalidated);
    }
    bool operator==(const ExpectedRoom& other) const { return Tuple() == other.Tuple(); }
};
ExpectedRoom Initial(const std::string& id, std::initializer_list<const char*> ids) {
    ExpectedRoom e; e.id = id;
    for (const auto member : ids) e.members.emplace_back(member, Slot::kWaitConfirm, 0, -1);
    return e;
}
ExpectedRoom View(const Room& r) {
    ExpectedRoom e;
    e.id = r.id; e.state = r.state; e.deadline = r.confirmDeadline;
    e.created = r.createdTick; e.revision = r.settleRevision; e.settles = r.settles;
    e.log = r.log; e.requeued = r.requeued; e.owned = r.ownsAllocation;
    e.invalidated = r.memberSetInvalidated;
    for (const auto& m : r.members) e.members.emplace_back(m.id, m.slot, m.declinedCount, m.settledRevision);
    return e;
}
using Rooms = std::map<std::string, ExpectedRoom>;
using ServiceView = std::tuple<int, int, int, Rooms>;
ServiceView View(const MatchService& s) {
    Rooms rooms;
    for (const auto& pair : s.rooms) rooms.emplace(pair.first, View(pair.second));
    return {s.pool.capacity, s.pool.used, s.pool.allocCount, rooms};
}
void SlotIs(ExpectedRoom& e, size_t index, Slot slot) { std::get<1>(e.members.at(index)) = slot; }
int totalChecks = 0, totalFails = 0, totalCases = 0;
struct Case {
    std::string name; int checks = 0, fails = 0;
    explicit Case(const char* n) : name(n) { ++totalCases; }
    void Check(bool okay) {
        ++checks; ++totalChecks;
        if (!okay) { ++fails; ++totalFails; }
        std::printf("CHECK %s %d %s\n", name.c_str(), checks, okay ? "PASS" : "FAIL");
    }
    ~Case() { std::printf("CASE %s checks=%d fail=%d\n", name.c_str(), checks, fails); }
};
// Literal expected pool counts and independent owner IDs, never derived from
// CountAccepted/AllAccepted or the DUT's resource count. Complete ordered state
// comparison includes members, logs, requeues, revisions, flags and map keys.
void Verify(Case& c, const MatchService& s, int capacity, int used, int allocations,
            const Rooms& rooms, const std::set<std::string>& owners) {
    c.Check(View(s) == ServiceView{capacity, used, allocations, rooms});
    std::set<std::string> actual;
    for (const auto& pair : s.rooms) if (pair.second.ownsAllocation) actual.insert(pair.first);
    c.Check(actual == owners && s.pool.used == static_cast<int>(owners.size()));
}
template<class F> void Refused(Case& c, MatchService& s, F action) {
    const auto before = View(s);
    const bool result = action();
    c.Check(!result); c.Check(View(s) == before);
}

void Happy() {
    Case c("happy"); MatchService s; s.pool.capacity = 2;
    auto& a = s.CreateRoom("A", {"a1", "a2"}, 0, 30);
    auto e = Initial("A", {"a1", "a2"});
    Verify(c, s, 2, 0, 0, {{"A", e}}, {});
    bool result = s.Confirm(a, "a1", 1); c.Check(result);
    SlotIs(e, 0, Slot::kAccepted); e.state = RoomState::kAllocating;
    e.owned = true; e.log.push_back("Allocating");
    Verify(c, s, 2, 1, 1, {{"A", e}}, {"A"});
    const auto before = View(s); result = s.Confirm(a, "a1", 99);
    c.Check(result); c.Check(View(s) == before); // existing valid-live duplicate semantics
    result = s.Confirm(a, "a2", 30); c.Check(result);
    SlotIs(e, 1, Slot::kAccepted); e.state = RoomState::kReady; e.log.push_back("Ready");
    Verify(c, s, 2, 1, 1, {{"A", e}}, {"A"});
    result = s.Start(a); c.Check(result); e.state = RoomState::kInProgress; e.log.push_back("InProgress");
    Verify(c, s, 2, 1, 1, {{"A", e}}, {"A"});
    Refused(c, s, [&] { return s.Start(a); });
    const auto rev = s.Settle(a); c.Check(rev == 1);
    e.state = RoomState::kClosed; e.revision = 1; e.settles = 1; e.owned = false;
    for (auto& m : e.members) std::get<3>(m) = 1;
    e.log.push_back("Closed(settled)");
    Verify(c, s, 2, 0, 1, {{"A", e}}, {});
    const auto settled = View(s); const auto again = s.Settle(a);
    c.Check(again == 1); c.Check(View(s) == settled);
}
void Capacity() {
    Case c("capacity_retry"); MatchService s;
    auto& a = s.CreateRoom("A", {"a1", "a2"}, 0, 30); auto e = Initial("A", {"a1", "a2"});
    Refused(c, s, [&] { return s.Confirm(a, "a1", 1); });
    Verify(c, s, 0, 0, 0, {{"A", e}}, {});
    s.pool.capacity = 1;
    bool result = s.Confirm(a, "a1", 2); c.Check(result);
    SlotIs(e, 0, Slot::kAccepted); e.state = RoomState::kAllocating; e.owned = true; e.log.push_back("Allocating");
    Verify(c, s, 1, 1, 1, {{"A", e}}, {"A"});
    Refused(c, s, [&] { return s.Confirm(a, "missing", 2); });
    Refused(c, s, [&] { return s.Decline(a, "a1"); });
    Refused(c, s, [&] { return s.Start(a); });
    const auto before = View(s); const auto rev = s.Settle(a);
    c.Check(rev == -1); c.Check(View(s) == before);
}
void CapacityReleasedByOtherRoom() {
    Case c("capacity_release_retry"); MatchService s; s.pool.capacity = 1;
    auto& a = s.CreateRoom("A", {"a1", "a2"}, 0, 30); auto& b = s.CreateRoom("B", {"b1"}, 0, 30);
    auto ea = Initial("A", {"a1", "a2"}); auto eb = Initial("B", {"b1"});
    Verify(c, s, 1, 0, 0, {{"A", ea}, {"B", eb}}, {});
    bool result = s.Confirm(b, "b1", 1); c.Check(result);
    SlotIs(eb, 0, Slot::kAccepted); eb.owned = true; eb.state = RoomState::kReady;
    eb.log.insert(eb.log.end(), {"Allocating", "Ready"});
    Verify(c, s, 1, 1, 1, {{"A", ea}, {"B", eb}}, {"B"});
    Refused(c, s, [&] { return s.Confirm(a, "a1", 1); });
    Verify(c, s, 1, 1, 1, {{"A", ea}, {"B", eb}}, {"B"});
    result = s.Start(b); c.Check(result); eb.state = RoomState::kInProgress; eb.log.push_back("InProgress");
    Verify(c, s, 1, 1, 1, {{"A", ea}, {"B", eb}}, {"B"});
    const auto revision = s.Settle(b); c.Check(revision == 1);
    eb.state = RoomState::kClosed; eb.owned = false; eb.revision = 1; eb.settles = 1;
    std::get<3>(eb.members[0]) = 1; eb.log.push_back("Closed(settled)");
    Verify(c, s, 1, 0, 1, {{"A", ea}, {"B", eb}}, {});
    result = s.Confirm(a, "a1", 2); c.Check(result);
    SlotIs(ea, 0, Slot::kAccepted); ea.owned = true; ea.state = RoomState::kAllocating; ea.log.push_back("Allocating");
    Verify(c, s, 1, 1, 2, {{"A", ea}, {"B", eb}}, {"A"});
    result = s.Confirm(a, "a2", 3); c.Check(result);
    SlotIs(ea, 1, Slot::kAccepted); ea.state = RoomState::kReady; ea.log.push_back("Ready");
    Verify(c, s, 1, 1, 2, {{"A", ea}, {"B", eb}}, {"A"});
    Refused(c, s, [&] { return s.Confirm(b, "b1", 3); });
    Verify(c, s, 1, 1, 2, {{"A", ea}, {"B", eb}}, {"A"});
}
void TwoDrops() {
    Case c("two_room_drop"); MatchService s; s.pool.capacity = 2;
    auto& a = s.CreateRoom("A", {"a1", "a2"}, 0, 30); auto& b = s.CreateRoom("B", {"b1"}, 0, 30);
    auto ea = Initial("A", {"a1", "a2"}); auto eb = Initial("B", {"b1"});
    bool result = s.Confirm(a, "a1", 1); c.Check(result);
    SlotIs(ea, 0, Slot::kAccepted); ea.state = RoomState::kAllocating; ea.owned = true; ea.log.push_back("Allocating");
    Verify(c, s, 2, 1, 1, {{"A", ea}, {"B", eb}}, {"A"});
    result = s.Confirm(b, "b1", 1); c.Check(result);
    SlotIs(eb, 0, Slot::kAccepted); eb.state = RoomState::kReady; eb.owned = true; eb.log.insert(eb.log.end(), {"Allocating", "Ready"});
    Verify(c, s, 2, 2, 2, {{"A", ea}, {"B", eb}}, {"A", "B"});
    result = s.Confirm(a, "a2", 1); c.Check(result);
    SlotIs(ea, 1, Slot::kAccepted); ea.state = RoomState::kReady; ea.log.push_back("Ready");
    Verify(c, s, 2, 2, 2, {{"A", ea}, {"B", eb}}, {"A", "B"});
    result = s.Drop(a, "a1"); c.Check(result);
    SlotIs(ea, 0, Slot::kDropped); ea.state = RoomState::kMatched; ea.owned = false; ea.invalidated = true;
    ea.requeued = {"a1"}; ea.log.push_back("->Matched(drop)");
    Verify(c, s, 2, 1, 2, {{"A", ea}, {"B", eb}}, {"B"});
    result = s.Drop(a, "a2"); c.Check(result); SlotIs(ea, 1, Slot::kDropped); ea.requeued.push_back("a2");
    Verify(c, s, 2, 1, 2, {{"A", ea}, {"B", eb}}, {"B"});
    Refused(c, s, [&] { return s.Drop(a, "a2"); });
    result = s.Timeout(a, 31); c.Check(result); ea.state = RoomState::kClosed; ea.log.push_back("Closed(confirm_timeout)");
    Verify(c, s, 2, 1, 2, {{"A", ea}, {"B", eb}}, {"B"});
    Refused(c, s, [&] { return s.Timeout(a, 32); });
    result = s.Start(b); c.Check(result); eb.state = RoomState::kInProgress; eb.log.push_back("InProgress");
    Verify(c, s, 2, 1, 2, {{"A", ea}, {"B", eb}}, {"B"});
    const auto rev = s.Settle(b); c.Check(rev == 1); eb.state = RoomState::kClosed; eb.owned = false;
    eb.revision = 1; eb.settles = 1; std::get<3>(eb.members[0]) = 1; eb.log.push_back("Closed(settled)");
    Verify(c, s, 2, 0, 2, {{"A", ea}, {"B", eb}}, {});
}
void DeclineTimeout() {
    Case c("decline_timeout"); MatchService s; s.pool.capacity = 2;
    auto& a = s.CreateRoom("A", {"a1", "a2", "a3"}, 0, 30); auto& b = s.CreateRoom("B", {"b1"}, 0, 30);
    auto ea = Initial("A", {"a1", "a2", "a3"}); auto eb = Initial("B", {"b1"});
    bool result = s.Confirm(a, "a1", 1); c.Check(result); SlotIs(ea, 0, Slot::kAccepted);
    ea.state = RoomState::kAllocating; ea.owned = true; ea.log.push_back("Allocating");
    result = s.Confirm(b, "b1", 1); c.Check(result); SlotIs(eb, 0, Slot::kAccepted);
    eb.state = RoomState::kReady; eb.owned = true; eb.log.insert(eb.log.end(), {"Allocating", "Ready"});
    Verify(c, s, 2, 2, 2, {{"A", ea}, {"B", eb}}, {"A", "B"});
    result = s.Decline(a, "a2"); c.Check(result); SlotIs(ea, 1, Slot::kDeclined); std::get<2>(ea.members[1]) = 1;
    ea.state = RoomState::kMatched; ea.owned = false; ea.invalidated = true;
    ea.requeued = {"a2"}; ea.log.push_back("Allocating->Matched(decline)");
    Verify(c, s, 2, 1, 2, {{"A", ea}, {"B", eb}}, {"B"});
    Refused(c, s, [&] { return s.Confirm(a, "a3", 2); }); // original 2/3 Ready counterexample
    Refused(c, s, [&] { return s.Confirm(a, "a1", 2); });
    Refused(c, s, [&] { return s.Start(a); });
    Refused(c, s, [&] { return s.Timeout(a, 30); });
    Verify(c, s, 2, 1, 2, {{"A", ea}, {"B", eb}}, {"B"});
    result = s.Timeout(a, 31); c.Check(result);
    SlotIs(ea, 0, Slot::kDropped); SlotIs(ea, 2, Slot::kDropped); ea.requeued.push_back("a3");
    ea.state = RoomState::kClosed; ea.log.push_back("Closed(confirm_timeout)");
    Verify(c, s, 2, 1, 2, {{"A", ea}, {"B", eb}}, {"B"});
    Refused(c, s, [&] { return s.Timeout(a, 32); });
    Refused(c, s, [&] { return s.Confirm(a, "a3", 2); });
}
void WaitingDrop() {
    Case c("waiting_drop");
    for (bool allocated : {false, true}) {
        MatchService s; s.pool.capacity = 1; auto& a = s.CreateRoom("A", {"a1", "a2"}, 0, 30);
        auto e = Initial("A", {"a1", "a2"});
        if (allocated) {
            const bool result = s.Confirm(a, "a1", 1); c.Check(result);
            SlotIs(e, 0, Slot::kAccepted); e.log.push_back("Allocating");
        }
        const bool result = s.Drop(a, "a2"); c.Check(result);
        SlotIs(e, 1, Slot::kDropped); e.invalidated = true; e.requeued = {"a2"};
        if (allocated) e.log.push_back("->Matched(drop)");
        Verify(c, s, 1, 0, allocated ? 1 : 0, {{"A", e}}, {});
        Refused(c, s, [&] { return s.Confirm(a, "a1", 2); });
        Refused(c, s, [&] { return s.Confirm(a, "a2", 2); });
        Refused(c, s, [&] { return s.Start(a); });
    }
}
void CrossExit() {
    Case c("cross_exit");
    for (bool declineFirst : {false, true}) {
        MatchService s; s.pool.capacity = 1; auto& a = s.CreateRoom("A", {"a1", "a2"}, 0, 30);
        auto e = Initial("A", {"a1", "a2"});
        const bool result = declineFirst ? s.Decline(a, "a1") : s.Drop(a, "a1"); c.Check(result);
        SlotIs(e, 0, declineFirst ? Slot::kDeclined : Slot::kDropped);
        std::get<2>(e.members[0]) = declineFirst ? 1 : 0; e.invalidated = true; e.requeued = {"a1"};
        Verify(c, s, 1, 0, 0, {{"A", e}}, {});
        Refused(c, s, [&] { return s.Decline(a, "a1"); });
        Refused(c, s, [&] { return s.Drop(a, "a1"); });
        Refused(c, s, [&] { return s.Decline(a, "a1"); });
        Refused(c, s, [&] { return s.Confirm(a, "a1", 1); });
        const bool other = s.Drop(a, "a2"); c.Check(other); SlotIs(e, 1, Slot::kDropped); e.requeued.push_back("a2");
        Verify(c, s, 1, 0, 0, {{"A", e}}, {});
    }
}
void Deadlines() {
    Case c("deadlines"); MatchService s; s.pool.capacity = 2;
    auto& a = s.CreateRoom("A", {"a1", "a2"}, 0, 30); auto& b = s.CreateRoom("B", {"b1"}, 0, 30);
    auto ea = Initial("A", {"a1", "a2"}); auto eb = Initial("B", {"b1"});
    bool result = s.Confirm(a, "a1", 30); c.Check(result);
    SlotIs(ea, 0, Slot::kAccepted); ea.state = RoomState::kAllocating; ea.owned = true; ea.log.push_back("Allocating");
    result = s.Confirm(b, "b1", 30); c.Check(result);
    SlotIs(eb, 0, Slot::kAccepted); eb.state = RoomState::kReady; eb.owned = true; eb.log.insert(eb.log.end(), {"Allocating", "Ready"});
    Verify(c, s, 2, 2, 2, {{"A", ea}, {"B", eb}}, {"A", "B"});
    Refused(c, s, [&] { return s.Timeout(a, 30); });
    Refused(c, s, [&] { return s.Timeout(b, 30); });
    Refused(c, s, [&] { return s.Confirm(a, "a2", 31); });
    result = s.Timeout(a, 31); c.Check(result);
    SlotIs(ea, 0, Slot::kDropped); SlotIs(ea, 1, Slot::kDropped); ea.owned = false;
    ea.state = RoomState::kClosed; ea.log.push_back("Closed(confirm_timeout)"); ea.requeued = {"a2"};
    Verify(c, s, 2, 1, 2, {{"A", ea}, {"B", eb}}, {"B"});
    result = s.Timeout(b, 31); c.Check(result); SlotIs(eb, 0, Slot::kDropped); eb.owned = false;
    eb.state = RoomState::kClosed; eb.log.push_back("Closed(confirm_timeout)");
    Verify(c, s, 2, 0, 2, {{"A", ea}, {"B", eb}}, {});
    Refused(c, s, [&] { return s.Timeout(a, 31); });
}
void InProgress() {
    Case c("inprogress"); MatchService s; s.pool.capacity = 2;
    auto& a = s.CreateRoom("A", {"a1"}, 0, 30); auto& b = s.CreateRoom("B", {"b1"}, 0, 30);
    auto ea = Initial("A", {"a1"}); auto eb = Initial("B", {"b1"});
    bool result = s.Confirm(a, "a1", 1); c.Check(result); result = s.Confirm(b, "b1", 1); c.Check(result);
    for (auto e : {&ea, &eb}) { SlotIs(*e, 0, Slot::kAccepted); e->owned = true; e->state = RoomState::kReady; e->log.insert(e->log.end(), {"Allocating", "Ready"}); }
    result = s.Start(a); c.Check(result); ea.state = RoomState::kInProgress; ea.log.push_back("InProgress");
    Verify(c, s, 2, 2, 2, {{"A", ea}, {"B", eb}}, {"A", "B"});
    Refused(c, s, [&] { return s.Drop(a, "a1"); });
    Refused(c, s, [&] { return s.Decline(a, "a1"); });
    Refused(c, s, [&] { return s.Timeout(a, 31); });
    const auto rev = s.Settle(a); c.Check(rev == 1);
    ea.state = RoomState::kClosed; ea.owned = false; ea.revision = 1; ea.settles = 1;
    std::get<3>(ea.members[0]) = 1; ea.log.push_back("Closed(settled)");
    Verify(c, s, 2, 1, 2, {{"A", ea}, {"B", eb}}, {"B"});
    const auto before = View(s); const auto again = s.Settle(a); c.Check(again == 1); c.Check(View(s) == before);
    Refused(c, s, [&] { return s.Drop(a, "a1"); });
}
void DuplicateCreate() {
    Case c("duplicate_create");
    for (int stage = 0; stage != 5; ++stage) {
        MatchService s; s.pool.capacity = 2;
        auto& a = s.CreateRoom("A", {"a1", "a2"}, 0, 30); auto& b = s.CreateRoom("B", {"b1"}, 0, 30);
        auto ea = Initial("A", {"a1", "a2"}); auto eb = Initial("B", {"b1"});
        const bool confirmed = s.Confirm(b, "b1", 1); c.Check(confirmed);
        SlotIs(eb, 0, Slot::kAccepted); eb.owned = true; eb.state = RoomState::kReady; eb.log.insert(eb.log.end(), {"Allocating", "Ready"});
        if (stage >= 1) { const bool r = s.Confirm(a, "a1", 1); c.Check(r); SlotIs(ea, 0, Slot::kAccepted); ea.owned = true; ea.state = RoomState::kAllocating; ea.log.push_back("Allocating"); }
        if (stage >= 2) { const bool r = s.Confirm(a, "a2", 1); c.Check(r); SlotIs(ea, 1, Slot::kAccepted); ea.state = RoomState::kReady; ea.log.push_back("Ready"); }
        if (stage >= 3) { const bool r = s.Start(a); c.Check(r); ea.state = RoomState::kInProgress; ea.log.push_back("InProgress"); }
        if (stage == 4) { const auto r = s.Settle(a); c.Check(r == 1); ea.owned = false; ea.state = RoomState::kClosed; ea.revision = 1; ea.settles = 1; for (auto& m : ea.members) std::get<3>(m) = 1; ea.log.push_back("Closed(settled)"); }
        const bool owned = stage >= 1 && stage < 4;
        const std::set<std::string> owners = owned ? std::set<std::string>{"A", "B"} : std::set<std::string>{"B"};
        Verify(c, s, 2, owned ? 2 : 1, stage ? 2 : 1, {{"A", ea}, {"B", eb}}, owners);
        for (bool same : {true, false}) {
            const auto before = View(s); const auto address = &a; bool rejected = false;
            try { s.CreateRoom("A", same ? std::vector<std::string>{"a1", "a2"} : std::vector<std::string>{"new"}, same ? 0 : 50, same ? 30 : 90); }
            catch (const std::invalid_argument&) { rejected = true; }
            c.Check(rejected); c.Check(View(s) == before); c.Check(&s.rooms.at("A") == address);
            Verify(c, s, 2, owned ? 2 : 1, stage ? 2 : 1, {{"A", ea}, {"B", eb}}, owners);
        }
    }
}
void Backfill() {
    Case c("backfill"); MatchService s; s.pool.capacity = 1;
    auto& a = s.CreateRoom("A", {"a1"}, 0, 30); auto e = Initial("A", {"a1"});
    Refused(c, s, [&] { return s.Backfill(a, "a2", 2); });
    bool result = s.Confirm(a, "a1", 1); c.Check(result);
    SlotIs(e, 0, Slot::kAccepted); e.owned = true; e.state = RoomState::kReady; e.log.insert(e.log.end(), {"Allocating", "Ready"});
    Refused(c, s, [&] { return s.Backfill(a, "a2", 2); });
    result = s.Start(a); c.Check(result); e.state = RoomState::kInProgress; e.log.push_back("InProgress");
    Refused(c, s, [&] { return s.Backfill(a, "a1", 3); }); // duplicate below capacity
    result = s.Backfill(a, "a2", 2); c.Check(result);
    e.members.emplace_back("a2", Slot::kAccepted, 0, -1); e.log.push_back("Backfill(a2)");
    Verify(c, s, 1, 1, 1, {{"A", e}}, {"A"});
    Refused(c, s, [&] { return s.Backfill(a, "a3", 2); });
    const auto rev = s.Settle(a); c.Check(rev == 1);
    e.owned = false; e.state = RoomState::kClosed; e.revision = 1; e.settles = 1;
    for (auto& m : e.members) std::get<3>(m) = 1;
    e.log.push_back("Closed(settled)");
    Verify(c, s, 1, 0, 1, {{"A", e}}, {});
    Refused(c, s, [&] { return s.Backfill(a, "a3", 3); });
}
void PredicateAndSyntheticGuards() {
    Case c("predicate_synthetic_guards");
    // D1 prevents these malformed member sets from reaching Ready through valid
    // public events. These are explicit predicate/defensive guard probes, NOT
    // claims that corrupted states are valid reachable lifecycle histories.
    Room predicate;
    c.Check(!predicate.AllAccepted());
    for (Slot slot : {Slot::kWaitConfirm, Slot::kDeclined, Slot::kDropped, Slot::kSettled, Slot::kAccepted}) {
        predicate.members = {{"a1", Slot::kAccepted, 0, -1}, {"a2", slot, 0, -1}};
        const bool result = predicate.AllAccepted(); c.Check(result == (slot == Slot::kAccepted));
    }
    for (int defect = 0; defect != 3; ++defect) {
        MatchService s; s.pool.capacity = 1; auto& a = s.CreateRoom("A", {"a1"}, 0, 30);
        const bool confirmed = s.Confirm(a, "a1", 1); c.Check(confirmed);
        if (defect == 0) { a.ownsAllocation = false; s.pool.used = 0; }
        if (defect == 1) a.memberSetInvalidated = true;
        if (defect == 2) a.members[0].slot = Slot::kDeclined;
        Refused(c, s, [&] { return s.Start(a); });
    }
    MatchService s; s.pool.capacity = 1; auto& a = s.CreateRoom("A", {"a1"}, 0, 30);
    a.state = RoomState::kInProgress; // explicit missing-ownership defensive probe
    const auto before = View(s); const auto revision = s.Settle(a);
    c.Check(revision == -1); c.Check(View(s) == before);
}
} // namespace contract
int main() {
    std::puts("ROOM_CONTRACT version=1");
    contract::Happy(); contract::Capacity(); contract::CapacityReleasedByOtherRoom(); contract::TwoDrops(); contract::DeclineTimeout();
    contract::WaitingDrop(); contract::CrossExit(); contract::Deadlines(); contract::InProgress();
    contract::DuplicateCreate(); contract::Backfill(); contract::PredicateAndSyntheticGuards();
    std::printf("RESULT version=1 cases=%d checks=%d fail=%d\n", contract::totalCases, contract::totalChecks, contract::totalFails);
    return contract::totalFails == 0 ? 0 : 1;
}
