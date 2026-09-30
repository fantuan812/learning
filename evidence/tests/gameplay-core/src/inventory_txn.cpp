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
#include <unordered_set>

namespace {

constexpr int kMaxStack = 99;
constexpr int kMaxBatch = 999;

struct Slot {
    int itemId = 0;   // 0 == empty
    int count = 0;
    bool operator==(const Slot& o) const { return itemId == o.itemId && count == o.count; }
};

class Inventory {
public:
    explicit Inventory(int capacity) : slots_(static_cast<size_t>(capacity)) {}

    // Transactional add. Either the whole amount fits (all stacks written) or nothing changes.
    // Returns true when the item ends up added (or was already applied for this requestId).
    bool Add(int itemId, int count, uint64_t requestId, std::string& outReason) {
        if (processed_.count(requestId)) { outReason = "duplicate-request-idempotent"; return true; }
        if (itemId <= 0) { outReason = "invalid-item"; return false; }
        if (count <= 0) { outReason = "invalid-count"; return false; }
        if (count > kMaxBatch) { outReason = "batch-exceeds-limit"; return false; }

        // Phase 1: dry-run plan, no mutation.
        int remaining = count;
        std::vector<std::pair<size_t, int>> plan;  // slot index -> amount written
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
        if (remaining > 0) { outReason = "capacity-exhausted-rolled-back"; return false; }

        // Phase 2: commit.
        for (const auto& step : plan) {
            Slot& s = slots_[step.first];
            if (s.itemId == 0) { s.itemId = itemId; s.count = step.second; }
            else { s.count += step.second; }
        }
        processed_.insert(requestId);
        outReason = "ok";
        return true;
    }

    bool Remove(int itemId, int count, std::string& outReason) {
        if (count <= 0) { outReason = "invalid-count"; return false; }
        int have = 0;
        for (const Slot& s : slots_) if (s.itemId == itemId) have += s.count;
        if (have < count) { outReason = "not-enough-items"; return false; }
        int left = count;
        for (Slot& s : slots_) {
            if (left == 0) break;
            if (s.itemId != itemId) continue;
            const int take = std::min(s.count, left);
            s.count -= take;
            left -= take;
            if (s.count == 0) { s.itemId = 0; }
        }
        outReason = "ok";
        return true;
    }

    int Count(int itemId) const {
        int total = 0;
        for (const Slot& s : slots_) if (s.itemId == itemId) total += s.count;
        return total;
    }
    int UsedSlots() const {
        int n = 0;
        for (const Slot& s : slots_) if (s.itemId != 0) ++n;
        return n;
    }
    int StackCount(int itemId) const {
        int n = 0;
        for (const Slot& s : slots_) if (s.itemId == itemId && s.count > 0) ++n;
        return n;
    }
    const std::vector<Slot>& Slots() const { return slots_; }

private:
    std::vector<Slot> slots_;
    std::unordered_set<uint64_t> processed_;
};

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
    std::string r;
    bool ok = inv.Add(1001, 150, 1, r);
    Check(ok && inv.Count(1001) == 150 && inv.StackCount(1001) == 2 && inv.UsedSlots() == 2,
          "T1 basic stacking splits into new slot",
          Fmt("count=%d stacks=%d used=%d", inv.Count(1001), inv.StackCount(1001), inv.UsedSlots()));
}

void TestTopUpFirst() {
    Inventory inv(20);
    std::string r;
    inv.Add(1001, 40, 1, r);
    inv.Add(1001, 100, 2, r);
    const auto& s = inv.Slots();
    const bool firstFull = s[0].count == kMaxStack;
    const bool secondRest = s[1].count == 41;
    Check(firstFull && secondRest && inv.Count(1001) == 140,
          "T2 tops up existing stack before new slot",
          Fmt("slot0=%d slot1=%d total=%d", s[0].count, s[1].count, inv.Count(1001)));
}

void TestCapacityRollback() {
    Inventory inv(2);
    std::string r;
    inv.Add(1001, 50, 1, r);
    inv.Add(1002, 99, 2, r);
    const std::vector<Slot> before = inv.Slots();
    bool ok = inv.Add(1001, 200, 3, r);
    const bool unchanged = before == inv.Slots();
    Check(!ok && unchanged && r == "capacity-exhausted-rolled-back",
          "T3 capacity exhaustion is atomic (no partial write)",
          Fmt("ok=%d unchanged=%d reason=%s", ok ? 1 : 0, unchanged ? 1 : 0, r.c_str()));
}

void TestIdempotency() {
    Inventory inv(20);
    std::string r1, r2;
    inv.Add(1003, 10, 42, r1);
    bool second = inv.Add(1003, 10, 42, r2);
    Check(second && inv.Count(1003) == 10 && r2 == "duplicate-request-idempotent",
          "T4 duplicate requestId is deduplicated",
          Fmt("total=%d secondReason=%s", inv.Count(1003), r2.c_str()));
}

void TestInvalidArguments() {
    Inventory inv(20);
    std::string r;
    const bool zero = inv.Add(1004, 0, 1, r);
    const std::string zeroReason = r;
    const bool neg = inv.Add(1004, -5, 2, r);
    const bool big = inv.Add(1004, kMaxBatch + 1, 3, r);
    const bool badItem = inv.Add(-1, 10, 4, r);
    Check(!zero && !neg && !big && !badItem && inv.UsedSlots() == 0,
          "T5 invalid item/count/batch rejected without mutation",
          Fmt("zero=%d(%s) neg=%d big=%d badItem=%d",
              zero ? 1 : 0, zeroReason.c_str(), neg ? 1 : 0, big ? 1 : 0, badItem ? 1 : 0));
}

void TestRemovePartial() {
    Inventory inv(20);
    std::string r;
    inv.Add(1001, 150, 1, r);           // [99, 51]
    bool ok = inv.Remove(1001, 120, r); // -> [0, 30]
    const auto& s = inv.Slots();
    Check(ok && inv.Count(1001) == 30 && s[0].itemId == 0 && s[1].count == 30 && inv.UsedSlots() == 1,
          "T6 partial removal frees emptied slot",
          Fmt("left=%d used=%d", inv.Count(1001), inv.UsedSlots()));
}

void TestRemoveGuard() {
    Inventory inv(20);
    std::string r;
    inv.Add(1001, 10, 1, r);
    bool ok = inv.Remove(1001, 11, r);
    Check(!ok && inv.Count(1001) == 10 && r == "not-enough-items",
          "T7 removal above owned amount rejected",
          Fmt("ok=%d left=%d", ok ? 1 : 0, inv.Count(1001)));
}

// ---- micro benchmark --------------------------------------------------------
void BenchTransaction() {
    constexpr int kSlots = 40;
    constexpr int kOps = 400000;
    Inventory inv(kSlots);
    std::string r;
    std::vector<double> latNs;
    latNs.reserve(kOps);
    double checksum = 0.0;

    for (int i = 0; i < 2000; ++i) inv.Add(5000 + i % 30, 99, 1000000ULL + i, r);

    for (int i = 0; i < kOps; ++i) {
        const int item = 5000 + (i % 30);
        const auto t0 = std::chrono::steady_clock::now();
        if (i % 3 == 0) {
            std::string rr;
            inv.Add(item, 5, 2000000ULL + i, rr);
            checksum += rr.size();
        } else {
            std::string rr;
            inv.Remove(item, 3, rr);
            checksum += rr.size();
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

}  // namespace

int main() {
    std::printf("gameplay-core | inventory transaction test suite\n");
    std::printf("compiler=%s c++17 O2\n", __VERSION__);
    std::printf("--------------------------------------------------------------------------\n");
    TestStacking();
    TestTopUpFirst();
    TestCapacityRollback();
    TestIdempotency();
    TestInvalidArguments();
    TestRemovePartial();
    TestRemoveGuard();
    BenchTransaction();
    std::printf("--------------------------------------------------------------------------\n");
    std::printf("RESULT pass=%d fail=%d\n", gPass, gFail);
    return gFail == 0 ? 0 : 1;
}
