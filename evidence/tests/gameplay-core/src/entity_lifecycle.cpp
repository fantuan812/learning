// Single-thread entity identity regression model, not a UE implementation.
// Build: g++ -std=c++17 -O2 -Wall -Wextra -Werror entity_lifecycle.cpp -o entity_lifecycle
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <vector>

struct EntityId { uint32_t slot; uint32_t gen; };

class EntityPool {
    enum class State { Free, Alive, Retiring, Retired };
    struct Slot { uint32_t gen = 1; State state = State::Free; };
    std::vector<Slot> slots_;
    std::vector<uint32_t> free_;
    // A smaller ceiling allows the overflow policy to be exercised without billions of cycles.
    uint32_t maxGeneration_;
public:
    explicit EntityPool(uint32_t cap,
        uint32_t maxGeneration = std::numeric_limits<uint32_t>::max())
        : slots_(cap), maxGeneration_(maxGeneration ? maxGeneration : 1) {
        for (uint32_t i = 0; i < cap; ++i) free_.push_back(cap - 1 - i);
    }
    std::optional<EntityId> Spawn() {
        if (free_.empty()) return std::nullopt;
        const uint32_t s = free_.back();
        free_.pop_back();
        slots_[s].state = State::Alive;
        return EntityId{s, slots_[s].gen};
    }
    bool IsAlive(EntityId id) const {
        return id.slot < slots_.size() && slots_[id.slot].state == State::Alive &&
               slots_[id.slot].gen == id.gen;
    }
    bool BeginDespawn(EntityId id) {
        if (!IsAlive(id)) return false;
        slots_[id.slot].state = State::Retiring;
        // Broadcast Despawning only AFTER this point; reentrant lookup now fails.
        return true;
    }
    bool FinishDespawn(EntityId id) {
        if (id.slot >= slots_.size()) return false;
        Slot& slot = slots_[id.slot];
        if (slot.gen != id.gen || slot.state != State::Retiring) return false;
        // Subscriber/component cleanup is the caller's responsibility before this call.
        if (slot.gen == maxGeneration_) {
            slot.state = State::Retired; // Fail closed, never wrap into an old identity.
        } else {
            ++slot.gen;
            slot.state = State::Free;
            free_.push_back(id.slot);
        }
        return true;
    }
};

int pass = 0, fail = 0;
void Check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    ok ? ++pass : ++fail;
}
int main() {
    EntityPool empty(0);
    Check(!empty.Spawn(), "E1 zero capacity has no identity");
    EntityPool pool(1);
    const auto first = pool.Spawn();
    if (!first) return 2;
    Check(pool.IsAlive(*first) && !pool.Spawn(), "E2 exhaustion cannot alias live slot zero");
    Check(!pool.IsAlive({1,1}), "E3 out-of-bounds handle rejected");
    Check(!pool.IsAlive({0,0}), "E4 fabricated zero-generation handle rejected");
    Check(pool.BeginDespawn(*first) && !pool.IsAlive(*first), "E5 retirement invalidates before callbacks");
    Check(!pool.Spawn(), "E6 retiring slot cannot be reused before cleanup");
    Check(!pool.BeginDespawn(*first), "E7 repeated begin is harmless");
    Check(pool.FinishDespawn(*first) && !pool.FinishDespawn(*first), "E8 repeated finish cannot double-free");
    const auto second = pool.Spawn();
    if (!second) return 2;
    Check(second->slot == first->slot && second->gen != first->gen &&
          !pool.IsAlive(*first) && pool.IsAlive(*second), "E9 reuse rejects stale identity");
    Check(!pool.BeginDespawn(*first) && !pool.FinishDespawn(*first) && pool.IsAlive(*second),
          "E10 stale cleanup cannot destroy replacement");
    EntityPool wrap(1,2);
    const auto a = wrap.Spawn();
    if (!a) return 2;
    wrap.BeginDespawn(*a); wrap.FinishDespawn(*a);
    const auto b = wrap.Spawn();
    if (!b) return 2;
    Check(b->gen == 2, "E11 test ceiling reached through ordinary reuse");
    wrap.BeginDespawn(*b); wrap.FinishDespawn(*b);
    Check(!wrap.Spawn() && !wrap.IsAlive(*a) && !wrap.IsAlive(*b), "E12 exhausted generation retires slot permanently");
    std::printf("RESULT pass=%d fail=%d\n",pass,fail);
    return fail ? 1 : 0;
}
