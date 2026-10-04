// Compile this file twice: normal test TU and -DINVENTORY_ALLOCATOR_TRANSLATION_UNIT.
// Separate TU prevents replacement new/delete inlining from hiding allocation calls.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>
namespace fault {
extern bool armed;
extern std::size_t target, calls;
extern std::size_t sizes[128];
extern bool unchanged[128];
extern bool (*observe)() noexcept;
void Begin(std::size_t at) noexcept;
void End() noexcept;
}
#ifdef INVENTORY_ALLOCATOR_TRANSLATION_UNIT
namespace fault {
bool armed = false;
std::size_t target = 0, calls = 0;
std::size_t sizes[128]{};
bool unchanged[128]{};
bool (*observe)() noexcept = nullptr;
void Begin(std::size_t at) noexcept { target = at; calls = 0; armed = true; }
void End() noexcept { armed = false; }
}
void* operator new(std::size_t size) {
    if (fault::armed) {
        const auto index = fault::calls++;
        if (index >= 128 || size > 65536) std::abort(); // Explicitly bounded, no host exhaustion.
        fault::sizes[index] = size;
        fault::unchanged[index] = !fault::observe || fault::observe();
        if (fault::target != 0 && fault::calls == fault::target) throw std::bad_alloc();
    }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#else
#include <cstdio>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>
#include <climits>
#define INVENTORY_MODEL_ONLY
#ifndef INVENTORY_SOURCE
#define INVENTORY_SOURCE "../src/inventory_txn.cpp"
#endif
#include INVENTORY_SOURCE

namespace {
int failures = 0;
int checks = 0;
void Require(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
Result Success(uint64_t key, int item, int count, int64_t before, int64_t after) {
    return {Code::Ok, key, item, count, before, after};
}
const Result sentinel{Code::NotEnough, UINT64_MAX, -9, -8, -7, -6};
using Records = std::vector<std::pair<uint64_t, SuccessRecord>>;
Records ReadRecords(const Inventory& inv) {
    Records result;
    for (const auto& row : inv.SuccessfulRequests()) result.emplace_back(row.first, row.second);
    return result;
}
bool RecordsEqual(const Inventory& inv, const Records& expected) noexcept {
    if (inv.SuccessfulRequests().size() != expected.size()) return false;
    for (const auto& row : expected) {
        const auto found = inv.SuccessfulRequests().find(row.first);
        if (found == inv.SuccessfulRequests().end() || !(found->second == row.second)) return false;
    }
    return true;
}
void ExpectRecord(Records& records, uint64_t key, int item, int count, int64_t before, int64_t after) {
    records.push_back({key, {item, count, Success(key, item, count, before, after)}});
}
void State(const Inventory& inv, const std::vector<Slot>& slots, const Records& records, const char* label) {
    Require(inv.Slots() == slots && RecordsEqual(inv, records), label);
    for (const auto& s : inv.Slots())
        Require((s.itemId == 0 && s.count == 0) || (s.itemId > 0 && s.count > 0 && s.count <= 99), "slot invariant");
}
void BasicAndReplay() {
    Inventory inv(4); Result r;
    std::vector<Slot> expected(4); Records records;
    Require(inv.Add(1001, 150, 1, r) && r == Success(1, 1001, 150, 0, 150), "literal 150 result");
    expected[0] = {1001, 99}; expected[1] = {1001, 51}; ExpectRecord(records, 1, 1001, 150, 0, 150);
    State(inv, expected, records, "all slots split 99+51");
    Require(inv.Remove(1001, 120, r) && r.after == 30, "remove 120 result");
    expected[0] = {}; expected[1] = {1001, 30}; State(inv, expected, records, "remove frees first slot, leaves30");
    Require(!inv.Remove(1001, 31, r) && r.code == Code::NotEnough, "over-owned Remove guard");
    State(inv, expected, records, "over-owned Remove unchanged");
    Require(inv.Add(2002, 10, 2, r), "other item intervenes"); expected[0] = {2002, 10}; ExpectRecord(records, 2, 2002, 10, 0, 10);
    Require(inv.Add(1001, 150, 1, r) && r == Success(1, 1001, 150, 0, 150), "replay exact first result despite later state");
    State(inv, expected, records, "replay no regrant");
    const int inputs[][2] = {{1002,150},{1001,149},{1002,149},{-1,-1},{0,150},{1001,INT_MAX}};
    for (const auto& input : inputs) {
        Require(!inv.Add(input[0], input[1], 1, r) && r == Result{Code::Conflict,1,input[0],input[1],0,0}, "stored key conflicts including invalid params");
        State(inv, expected, records, "conflict preserves original success and every slot");
    }
    Require(inv.Add(1001, 150, 1, r) && r == Success(1,1001,150,0,150), "original still replays after conflicts");
    Inventory other(4); Require(other.Add(1001,150,1,r), "key domain is Inventory instance");
    Inventory top(4); Require(top.Add(1001,40,10,r) && top.Add(1001,100,11,r), "topup accepted");
    expected.assign(4, {}); expected[0]={1001,99}; expected[1]={1001,41}; records.clear();
    ExpectRecord(records,10,1001,40,0,40); ExpectRecord(records,11,1001,100,40,140);
    State(top, expected, records, "top up prior40 before opening41");
    std::printf("PASS_GROUP independent_stacking_remove_original_result_conflict\n");
}
void InputsAndRetry() {
    Result r; Inventory inv(1); Records records; std::vector<Slot> empty(1);
    const int inputs[][2]={{0,1},{-1,1},{1001,0},{1001,-1},{1001,1000},{1001,INT_MAX}};
    const Code codes[]={Code::InvalidItem,Code::InvalidItem,Code::InvalidCount,Code::InvalidCount,Code::BatchLimit,Code::BatchLimit};
    for (size_t i=0; i<6; ++i) {
        Require(!inv.Add(inputs[i][0],inputs[i][1],9,r) && r==Result{codes[i],9,inputs[i][0],inputs[i][1],0,0}, "input result exact");
        State(inv,empty,records,"invalid input unremembered");
    }
    Require(inv.Add(INT_MAX,99,9,r), "rejected key can later bind; INT_MAX item valid");
    ExpectRecord(records,9,INT_MAX,99,0,99); std::vector<Slot> full{{INT_MAX,99}};
    Require(!inv.Add(INT_MAX,1,10,r) && r.code==Code::Capacity,"capacity clean rejection");
    State(inv,full,records,"capacity unremembered");
    Require(inv.Remove(INT_MAX,1,r) && inv.Add(INT_MAX,1,10,r),"same capacity-rejected key succeeds after Remove");
    ExpectRecord(records,10,INT_MAX,1,98,99); State(inv,full,records,"capacity retry exactly once");
    for (const auto& input : inputs) {
        Require(!inv.Remove(input[0],input[1],r),"invalid/over-owned Remove fails");
        State(inv,full,records,"Remove guard full state");
    }
    Inventory zero(0); Require(!zero.Add(1,1,0,r) && r.code==Code::Capacity,"zero capacity legal empty bag");
    bool negative=false; try { Inventory bad(-1); } catch(const std::invalid_argument&) { negative=true; }
    Require(negative,"negative capacity rejected before size conversion");
    Require(MaxItemsForCapacity(21691755)==2147483745LL && MaxItemsForCapacity(INT_MAX)==212600881053LL,
            "arithmetic proof crosses INT_MAX without allocating giant bag");
    Inventory retained(1); Require(retained.Add(3,10,0,r),"zero requestId allowed in instance scope");
    Require(retained.Remove(3,10,r),"clear retained fixture");
    for(int i=0;i<80;++i) { Require(retained.Add(4,1,100+i,r) && retained.Remove(4,1,r),"intervening grant/remove"); }
    Require(retained.Add(3,10,0,r) && r==Success(0,3,10,0,10) && retained.UsedSlots()==0,"retained old key no regrant");
    std::printf("PASS_GROUP inputs_retry_retained_memory_wide_totals\n");
}

const Inventory* observed=nullptr;
const std::vector<Slot>* oldSlots=nullptr;
const Result* observedOut=nullptr;
// Never query the map during its own emplace/rehash. Standard exception guarantees
// concern post-call state; its internal allocation-time representation is not API.
bool ObserveUnchanged() noexcept { return observed->Slots()==*oldSlots && *observedOut==sentinel; }
void Watch(const Inventory& inv, const std::vector<Slot>& slots, const Result& out) noexcept {
    observed=&inv; oldSlots=&slots; observedOut=&out; fault::observe=ObserveUnchanged;
}
void Unwatch() noexcept { fault::observe=nullptr; }
struct Spec { const char* name; int capacity, item, count; uint64_t key; bool accepted; };
void Seed(const Spec& s, Inventory& inv, std::vector<Slot>& slots, Records& records) {
    Result r; slots.assign(static_cast<size_t>(s.capacity),{});
    if (std::string(s.name)=="topup_and_new_slots" || std::string(s.name)=="capacity_rejection_plan") {
        Require(inv.Add(1001,50,1,r) && inv.Add(1002,99,2,r),"seed topup");
        slots[0]={1001,50}; slots[1]={1002,99}; ExpectRecord(records,1,1001,50,0,50); ExpectRecord(records,2,1002,99,0,99);
    } else if (std::string(s.name)=="tight_prefilled_max_batch") {
        Require(inv.Add(9,57,1,r) && inv.Add(7,98,2,r), "seed tight mixed state");
        slots[0]={9,57}; slots[1]={7,98}; ExpectRecord(records,1,9,57,0,57); ExpectRecord(records,2,7,98,0,98);
    } else if (std::string(s.name)=="map_growth") {
        uint64_t key=100;
        do {
            Require(inv.Add(7,1,key,r) && inv.Remove(7,1,r),"seed successful record");
            ExpectRecord(records,key,7,1,0,1); ++key;
            if (key>228) { Require(false,"bounded rehash fixture"); break; }
        } while (inv.SuccessfulRequests().size()+1 <= inv.SuccessfulRequests().bucket_count()*inv.SuccessfulRequests().max_load_factor());
    }
    State(inv,slots,records,"seed independent literal oracle");
}
void ExpectedAfter(const Spec& s, std::vector<Slot>& slots, Records& records, Result& result) {
    if (!s.accepted) { result={Code::Capacity,s.key,s.item,s.count,0,0}; return; }
    if (std::string(s.name)=="topup_and_new_slots") {
        slots[0]={1001,99}; slots[2]={1001,99}; slots[3]={1001,52};
        result=Success(s.key,1001,200,50,250); ExpectRecord(records,s.key,1001,200,50,250);
    } else if (std::string(s.name)=="tight_prefilled_max_batch") {
        slots[1]={7,99}; for(size_t i=2;i<12;++i) slots[i]={7,99}; slots[12]={7,8};
        result=Success(s.key,7,999,98,1097); ExpectRecord(records,s.key,7,999,98,1097);
    } else {
        if (s.count==999) { for(size_t i=0;i<10;++i) slots[i]={s.item,99}; slots[10]={s.item,9}; }
        else slots[0]={s.item,10};
        result=Success(s.key,s.item,s.count,0,s.count); ExpectRecord(records,s.key,s.item,s.count,0,s.count);
    }
}
void AllocationSweep(const Spec& s) {
    size_t calls=0;
    {
        Inventory inv(s.capacity); std::vector<Slot> before; Records prior; Seed(s,inv,before,prior);
        Result out=sentinel; Watch(inv,before,out); const auto buckets=inv.SuccessfulRequests().bucket_count();
        fault::Begin(0); const bool ok=inv.Add(s.item,s.count,s.key,out); fault::End(); Unwatch(); calls=fault::calls;
        Require(ok==s.accepted,"discovery status");
        Result expected; ExpectedAfter(s,before,prior,expected); State(inv,before,prior,"discovery independent result state"); Require(out==expected,"discovery exact result");
        Require(calls>0 && calls<128,"discovered bounded real allocations");
        if(std::string(s.name)=="map_growth") Require(inv.SuccessfulRequests().bucket_count()>buckets,"actual rehash exercised");
        std::printf("ALLOCATION_DISCOVERY scenario=%s calls=%zu buckets_before=%zu buckets_after=%zu\n",s.name,calls,buckets,inv.SuccessfulRequests().bucket_count());
        for(size_t i=0;i<calls;++i) { Require(fault::unchanged[i],"no allocation after slots/out publication"); std::printf("ALLOCATION call=%zu bytes=%zu old_slots_and_out=%d\n",i+1,fault::sizes[i],fault::unchanged[i]); }
    }
    for(size_t at=1;at<=calls+1;++at) {
        Inventory inv(s.capacity); std::vector<Slot> before; Records prior; Seed(s,inv,before,prior);
        Result out=sentinel; Watch(inv,before,out); bool caught=false,ok=false;
        fault::Begin(at); try { ok=inv.Add(s.item,s.count,s.key,out); } catch(const std::bad_alloc&) { caught=true; } fault::End(); Unwatch();
        if(at<=calls) {
            Require(caught,"every discovered allocation throws when selected"); State(inv,before,prior,"bad_alloc preserves full slots and success memory"); Require(out==sentinel,"bad_alloc preserves out result");
            if (!(inv.Slots()==before && RecordsEqual(inv,prior) && out==sentinel)) return; // mutation detected; avoid compounding corrupted state
            Require(inv.Add(s.item,s.count,s.key,out)==s.accepted,"restored allocator retries original request");
        } else { Require(!caught && ok==s.accepted && fault::calls==calls,"armed next allocation never occurs in publication/result transfer"); }
        Result expected; ExpectedAfter(s,before,prior,expected); State(inv,before,prior,"retry result matches literal oracle"); Require(out==expected,"retry exact result");
        if(s.accepted) {
            fault::Begin(1); const bool replay=inv.Add(s.item,s.count,s.key,out); fault::End();
            Require(replay && fault::calls==0 && out==expected,"replay fixed result allocates nothing"); State(inv,before,prior,"retry then replay grants once");
        }
        std::printf("FAULT_CASE scenario=%s fail_on=%zu caught=%d final_status=%d\n",s.name,at,caught,s.accepted);
    }
}
void NoAllocationResultsAndWireFailure() {
    Inventory inv(2); Result result; Require(inv.Add(1,10,1,result),"wire fixture grant");
    const Result first=result; const auto slots=inv.Slots(); const auto records=ReadRecords(inv);
    const int items[]={0,1,1,2}; const int counts[]={1,0,1000,10}; const uint64_t keys[]={2,2,2,1};
    const Code codes[]={Code::InvalidItem,Code::InvalidCount,Code::BatchLimit,Code::Conflict};
    for(size_t i=0;i<4;++i) {
        fault::Begin(1); const bool ok=inv.Add(items[i],counts[i],keys[i],result); fault::End();
        Require(!ok && fault::calls==0 && result.code==codes[i],"fixed rejection/conflict result cannot allocate"); State(inv,slots,records,"no allocation rejection state");
    }
    // Real external string serialization allocates AFTER Add returned. This is a
    // lost/unknown response example, not a throw from original short SSO 'ok'.
    bool caught=false; fault::Begin(1);
    try { std::string wire(128,'x'); Require(wire.size()==128,"wire size"); } catch(const std::bad_alloc&) { caught=true; }
    fault::End(); Require(caught,"external response serialization failure injected"); State(inv,slots,records,"external response failure is already committed");
    Require(inv.Add(1,10,1,result) && result==first,"retry after wire failure replays first snapshot"); State(inv,slots,records,"wire retry no regrant");
    std::printf("PASS_GROUP no_throw_results_and_external_serialization_boundary\n");
}
}
int main() {
    std::printf("inventory_contract compiler=%s NDEBUG=%d allocation_limit=65536 calls_limit=128\n",__VERSION__,
#ifdef NDEBUG
        1
#else
        0
#endif
    );
    Require(std::strcmp(Reason(Code::Ok), "ok") == 0, "fixed reason text");
    BasicAndReplay(); InputsAndRetry();
    const Spec specs[]={{"empty_small",4,1003,10,42,true},{"empty_max_batch",30,1003,999,42,true},
        {"topup_and_new_slots",8,1001,200,42,true},{"map_growth",4,1003,10,42,true},
        {"capacity_rejection_plan",2,1001,200,42,false},{"tight_prefilled_max_batch",16,7,999,42,true}};
    for(const auto& spec:specs) AllocationSweep(spec);
    NoAllocationResultsAndWireFailure();
    std::printf("RESULT checks=%d fail=%d (explicit checks remain active with NDEBUG)\n",checks,failures);
    return failures ? 1 : 0;
}
#endif
