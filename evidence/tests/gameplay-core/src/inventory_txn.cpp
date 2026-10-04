// Gameplay core evidence: inventory transaction semantics.
// Build: g++ -std=c++17 -O2 -o inventory_txn.exe inventory_txn.cpp
// Output is ASCII-only so it can be captured on any Windows console/CI.
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <stdexcept>
#include <type_traits>
#include <memory>
#include <limits>
#include <climits>
#include <cstring>

namespace {

constexpr int kMaxStack = 99;
constexpr int kMaxBatch = 999;

// This bounded teaching model assumes an int with at most 31 value bits.
// It accepts every nonnegative int capacity (subject to vector allocation limits).
static_assert(std::numeric_limits<int>::digits <= 31, "widen capacity proof for this ABI");
constexpr int64_t MaxItemsForCapacity(int capacity) noexcept {
    return static_cast<int64_t>(capacity) * kMaxStack;
}
static_assert(MaxItemsForCapacity(INT_MAX) <= INT64_MAX, "all slot totals fit int64_t");

struct Slot {
    int itemId = 0;   // Empty iff itemId == 0 and count == 0; occupied count in [1,99].
    int count = 0;
    bool operator==(const Slot& o) const noexcept { return itemId == o.itemId && count == o.count; }
};

enum class Code { Ok, InvalidItem, InvalidCount, BatchLimit, Capacity, Conflict, NotEnough };
const char* Reason(Code code) noexcept {
    switch (code) {
    case Code::Ok: return "ok";
    case Code::InvalidItem: return "invalid-item";
    case Code::InvalidCount: return "invalid-count";
    case Code::BatchLimit: return "batch-exceeds-limit";
    case Code::Capacity: return "capacity-exhausted-no-change";
    case Code::Conflict: return "request-id-conflict";
    case Code::NotEnough: return "not-enough-items";
    }
    return "unknown-code";
}

// Fixed-size result: no string allocation, callback, or serialization in publication.
// before/after describe the FIRST successful Add, not the current bag on replay.
struct Result {
    Code code = Code::Ok;
    uint64_t requestId = 0;
    int itemId = 0;
    int requested = 0;
    int64_t before = 0;
    int64_t after = 0;
    bool operator==(const Result& r) const noexcept {
        return code == r.code && requestId == r.requestId && itemId == r.itemId &&
               requested == r.requested && before == r.before && after == r.after;
    }
};
struct SuccessRecord {
    int itemId;
    int count;
    Result result;
    bool operator==(const SuccessRecord& r) const noexcept {
        return itemId == r.itemId && count == r.count && result == r.result;
    }
};
struct KeyHash {
    size_t operator()(uint64_t key) const noexcept { return static_cast<size_t>(key); }
};
struct KeyEqual {
    bool operator()(uint64_t a, uint64_t b) const noexcept { return a == b; }
};
using Successes = std::unordered_map<uint64_t, SuccessRecord, KeyHash, KeyEqual>;
static_assert(std::is_nothrow_copy_constructible<Result>::value &&
              std::is_nothrow_copy_assignable<Result>::value &&
              std::is_nothrow_destructible<Result>::value, "result publication must not throw");
static_assert(std::is_nothrow_copy_constructible<SuccessRecord>::value, "record copy must not throw");
static_assert(noexcept(KeyHash{}(0)) && noexcept(KeyEqual{}(0, 0)), "no throwing hash/equality");
static_assert(std::allocator_traits<Successes::allocator_type>::is_always_equal::value,
              "only the standard always-equal allocator is used");
static_assert(std::is_nothrow_destructible<Successes::value_type>::value,
              "record destruction must not throw");

class Inventory {
public:
    explicit Inventory(int capacity) : slots_(CheckedCapacity(capacity)) {}

    // Single-threaded, non-reentrant, in-memory only. Only successful Add binds a key.
    // Every exception before publication leaves slots, successful records, and out unchanged.
    bool Add(int itemId, int count, uint64_t requestId, Result& out) {
        const auto found = processed_.find(requestId);
        if (found != processed_.end()) {
            if (found->second.itemId != itemId || found->second.count != count) {
                out = {Code::Conflict, requestId, itemId, count, 0, 0};
                return false;
            }
            out = found->second.result;
            return true;
        }
        if (itemId <= 0) { out = {Code::InvalidItem, requestId, itemId, count, 0, 0}; return false; }
        if (count <= 0) { out = {Code::InvalidCount, requestId, itemId, count, 0, 0}; return false; }
        if (count > kMaxBatch) { out = {Code::BatchLimit, requestId, itemId, count, 0, 0}; return false; }

        // All potentially allocating planning precedes any visible domain change.
        int remaining = count;
        std::vector<std::pair<size_t, int>> plan;
        for (size_t i = 0; i < slots_.size() && remaining > 0; ++i) {
            if (slots_[i].itemId != itemId) continue;
            const int room = kMaxStack - slots_[i].count;
            if (room <= 0) continue;
            const int put = std::min(room, remaining);
            plan.emplace_back(i, put);
            remaining -= put;
        }
        for (size_t i = 0; i < slots_.size() && remaining > 0; ++i) {
            if (slots_[i].itemId != 0) continue;
            const int put = std::min(kMaxStack, remaining);
            plan.emplace_back(i, put);
            remaining -= put;
        }
        if (remaining > 0) { out = {Code::Capacity, requestId, itemId, count, 0, 0}; return false; }

        const int64_t before = Count(itemId);
        const Result prepared{Code::Ok, requestId, itemId, count, before, before + count};
        // FINAL potentially throwing operation: node allocation and possible rehash.
        // std::allocator allocation may throw; single-element unordered_map emplace
        // has no effect on exception with these nonthrowing hash/equality/value types.
        // Reserve alone would not prepare a node. No custom allocator/hook is allowed.
        const auto inserted = processed_.emplace(requestId, SuccessRecord{itemId, count, prepared});
        if (!inserted.second) { out = {Code::Conflict, requestId, itemId, count, 0, 0}; return false; }
        // No observer/concurrent call may see record-before-slots. This is NOT a CPU
        // atomic operation, a durable commit, or a multi-threaded transaction.
        Publish(plan, itemId);
        out = prepared;
        return true; // plan destruction only deallocates standard-allocator storage.
    }

    // Local removal primitive; intentionally no requestId/deduplication API.
    bool Remove(int itemId, int count, Result& out) noexcept {
        if (itemId <= 0) { out = {Code::InvalidItem, 0, itemId, count, 0, 0}; return false; }
        if (count <= 0) { out = {Code::InvalidCount, 0, itemId, count, 0, 0}; return false; }
        const int64_t have = Count(itemId);
        if (have < count) { out = {Code::NotEnough, 0, itemId, count, 0, 0}; return false; }
        int left = count;
        for (Slot& s : slots_) {
            if (left == 0) break;
            if (s.itemId != itemId) continue;
            const int take = std::min(s.count, left);
            s.count -= take;
            left -= take;
            if (s.count == 0) s.itemId = 0;
        }
        out = {Code::Ok, 0, itemId, count, have, have - count};
        return true;
    }

    int64_t Count(int itemId) const noexcept {
        int64_t total = 0;
        for (const Slot& s : slots_) if (s.itemId == itemId) total += s.count;
        return total;
    }
    int UsedSlots() const noexcept {
        int n = 0;
        for (const Slot& s : slots_) if (s.itemId != 0) ++n;
        return n; // slots_.size() <= INT_MAX from constructor.
    }
    int StackCount(int itemId) const noexcept {
        int n = 0;
        for (const Slot& s : slots_) if (s.itemId == itemId && s.count > 0) ++n;
        return n;
    }
    const std::vector<Slot>& Slots() const noexcept { return slots_; }
    const Successes& SuccessfulRequests() const noexcept { return processed_; }

private:
    static size_t CheckedCapacity(int capacity) {
        if (capacity < 0) throw std::invalid_argument("negative inventory capacity");
        return static_cast<size_t>(capacity);
    }
    void Publish(const std::vector<std::pair<size_t, int>>& plan, int itemId) noexcept {
        // Valid indices; all resulting counts in [1,99]. Only bounded scalar writes.
        for (const auto& step : plan) {
            Slot& s = slots_[step.first];
            if (s.itemId == 0) { s.itemId = itemId; s.count = step.second; }
            else s.count += step.second;
        }
    }
    std::vector<Slot> slots_;
    Successes processed_;
};

#ifndef INVENTORY_MODEL_ONLY

int gPass = 0;
int gFail = 0;

void Check(bool ok, const char* name, const std::string& detail) {
    if (ok) { ++gPass; std::printf("PASS  %-46s %s\n", name, detail.c_str()); }
    else    { ++gFail; std::printf("FAIL  %-46s %s\n", name, detail.c_str()); }
}

std::string Fmt(const char* fmt, ...) {
    char buf[240];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return std::string(buf);
}

// ---- functional cases -------------------------------------------------------
void TestStacking() {
    Inventory inv(20);
    Result r;
    bool ok = inv.Add(1001, 150, 1, r);
    Check(ok && inv.Count(1001) == 150 && inv.StackCount(1001) == 2 && inv.UsedSlots() == 2,
          "T1 basic stacking splits into new slot",
          Fmt("count=%lld stacks=%d used=%d", static_cast<long long>(inv.Count(1001)), inv.StackCount(1001), inv.UsedSlots()));
}

void TestTopUpFirst() {
    Inventory inv(20);
    Result r;
    inv.Add(1001, 40, 1, r);
    inv.Add(1001, 100, 2, r);
    const auto& s = inv.Slots();
    const bool firstFull = s[0].count == kMaxStack;
    const bool secondRest = s[1].count == 41;
    Check(firstFull && secondRest && inv.Count(1001) == 140,
          "T2 tops up existing stack before new slot",
          Fmt("slot0=%d slot1=%d total=%lld", s[0].count, s[1].count, static_cast<long long>(inv.Count(1001))));
}

void TestCapacityRollback() {
    Inventory inv(2);
    Result r;
    inv.Add(1001, 50, 1, r);
    inv.Add(1002, 99, 2, r);
    const std::vector<Slot> before = inv.Slots();
    bool ok = inv.Add(1001, 200, 3, r);
    const bool unchanged = before == inv.Slots();
    Check(!ok && unchanged && r.code == Code::Capacity,
          "T3 capacity exhaustion is atomic (no partial write)",
          Fmt("ok=%d unchanged=%d reason=%s", ok ? 1 : 0, unchanged ? 1 : 0, Reason(r.code)));
}

void TestIdempotency() {
    Inventory inv(20);
    Result r1, r2;
    inv.Add(1003, 10, 42, r1);
    bool second = inv.Add(1003, 10, 42, r2);
    Check(second && inv.Count(1003) == 10 && r1 == r2,
          "T4 duplicate requestId is deduplicated",
          Fmt("total=%lld secondReason=%s", static_cast<long long>(inv.Count(1003)), Reason(r2.code)));
}

void TestInvalidArguments() {
    Inventory inv(20);
    Result r;
    const bool zero = inv.Add(1004, 0, 1, r);
    const Code zeroReason = r.code;
    const bool neg = inv.Add(1004, -5, 2, r);
    const bool big = inv.Add(1004, kMaxBatch + 1, 3, r);
    const bool badItem = inv.Add(-1, 10, 4, r);
    Check(!zero && !neg && !big && !badItem && inv.UsedSlots() == 0,
          "T5 invalid item/count/batch rejected without mutation",
          Fmt("zero=%d(%s) neg=%d big=%d badItem=%d",
              zero ? 1 : 0, Reason(zeroReason), neg ? 1 : 0, big ? 1 : 0, badItem ? 1 : 0));
}

void TestRemovePartial() {
    Inventory inv(20);
    Result r;
    inv.Add(1001, 150, 1, r);           // [99, 51]
    bool ok = inv.Remove(1001, 120, r); // -> [0, 30]
    const auto& s = inv.Slots();
    Check(ok && inv.Count(1001) == 30 && s[0].itemId == 0 && s[1].count == 30 && inv.UsedSlots() == 1,
          "T6 partial removal frees emptied slot",
          Fmt("left=%lld used=%d", static_cast<long long>(inv.Count(1001)), inv.UsedSlots()));
}

void TestRemoveGuard() {
    Inventory inv(20);
    Result r;
    inv.Add(1001, 10, 1, r);
    bool ok = inv.Remove(1001, 11, r);
    Check(!ok && inv.Count(1001) == 10 && r.code == Code::NotEnough,
          "T7 removal above owned amount rejected",
          Fmt("ok=%d left=%lld", ok ? 1 : 0, static_cast<long long>(inv.Count(1001))));
}

// ---- micro benchmark --------------------------------------------------------
void BenchTransaction() {
    constexpr int kSlots = 40;
    constexpr int kOps = 400000;
    Inventory inv(kSlots);
    Result r;
    std::vector<double> latNs;
    latNs.reserve(kOps);
    double checksum = 0.0;

    for (int i = 0; i < 2000; ++i) inv.Add(5000 + i % 30, 99, 1000000ULL + i, r);

    for (int i = 0; i < kOps; ++i) {
        const int item = 5000 + (i % 30);
        const auto t0 = std::chrono::steady_clock::now();
        if (i % 3 == 0) {
            Result rr;
            inv.Add(item, 5, 2000000ULL + i, rr);
            checksum += std::strlen(Reason(rr.code));
        } else {
            Result rr;
            inv.Remove(item, 3, rr);
            checksum += std::strlen(Reason(rr.code));
        }
        const auto t1 = std::chrono::steady_clock::now();
        latNs.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }

    std::sort(latNs.begin(), latNs.end());
    auto pct = [&](double p) { return latNs[static_cast<size_t>(p * (latNs.size() - 1))]; };
    const double totalNs = [&] {
        double s = 0; for (double v : latNs) s += v; return s;
    }();
    std::printf("\n[bench] inventory_transaction slots=%d ops=%d\n", kSlots, kOps);
    std::printf("[bench] latency_ns p50=%.0f p95=%.0f p99=%.0f max=%.0f mean=%.0f\n",
                pct(0.50), pct(0.95), pct(0.99), latNs.back(), totalNs / latNs.size());
    std::printf("[bench] throughput_ops_per_sec=%.0f checksum=%.0f\n",
                1e9 / (totalNs / latNs.size()), checksum);
}

#endif // INVENTORY_MODEL_ONLY
}  // namespace

#ifndef INVENTORY_MODEL_ONLY
int main(int argc, char** argv) {
    std::printf("gameplay-core | inventory transaction test suite\n");
    std::printf("compiler=%s c++17 (flags recorded by runner)\n", __VERSION__);
    std::printf("--------------------------------------------------------------------------\n");
    TestStacking();
    TestTopUpFirst();
    TestCapacityRollback();
    TestIdempotency();
    TestInvalidArguments();
    TestRemovePartial();
    TestRemoveGuard();
    if (argc == 2 && std::strcmp(argv[1], "--benchmark") == 0) BenchTransaction();
    else if (argc != 1) { std::fprintf(stderr, "usage: inventory_txn [--benchmark]\n"); return 2; }
    std::printf("--------------------------------------------------------------------------\n");
    std::printf("RESULT pass=%d fail=%d\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}

#endif // INVENTORY_MODEL_ONLY
