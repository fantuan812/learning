#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

int checks = 0;
void require(bool b, const char* label) {
    ++checks;
    if (!b) { std::cerr << "FAIL " << label << '\n'; std::exit(1); }
    std::cout << "PASS " << label << '\n';
}

// BEGIN_POOL
// Single-threaded; constructors/destructors must not re-enter this pool.
template<class T, std::size_t N>
class FixedPool {
    static_assert(N > 0);
    static_assert(std::is_nothrow_destructible_v<T>);
    struct Slot {
        alignas(T) std::byte bytes[sizeof(T)];
        T* live = nullptr;
        std::size_t next = N;
    };
    std::array<Slot, N> slots_{};
    std::size_t free_ = 0;
public:
    FixedPool() noexcept {
        for (std::size_t i = 0; i < N; ++i) slots_[i].next = i + 1;
    }
    FixedPool(const FixedPool&) = delete;
    FixedPool& operator=(const FixedPool&) = delete;
    FixedPool(FixedPool&&) = delete;
    FixedPool& operator=(FixedPool&&) = delete;
    ~FixedPool() {
        for (auto& slot : slots_) if (slot.live) std::destroy_at(slot.live);
    }
    template<class... Args>
    T* create(Args&&... args) {
        if (free_ == N) return nullptr;
        Slot& slot = slots_[free_];
        // Commit metadata only after successful construction.
        T* p = ::new (static_cast<void*>(slot.bytes)) T(std::forward<Args>(args)...);
        free_ = slot.next;
        slot.live = p;
        return p;
    }
    bool destroy(T* p) noexcept {
        if (!p) return false;
        for (std::size_t i = 0; i < N; ++i) {
            Slot& slot = slots_[i];
            if (slot.live == p) {
                std::destroy_at(p);
                slot.live = nullptr;
                slot.next = free_;
                free_ = i;
                return true;
            }
        }
        return false;
    }
};
// END_POOL

// BEGIN_PROTOCOL
struct Damage { std::uint16_t amount; };
struct Heal { std::uint16_t amount; };
using Command = std::variant<Damage, Heal>;
enum class ParseError { Length, Tag, Range };

// Teaching wire format: [tag:1][amount:2, big endian], exactly three bytes.
std::expected<Command, ParseError> parse(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 3) return std::unexpected(ParseError::Length);
    if (bytes[0] != 1 && bytes[0] != 2) return std::unexpected(ParseError::Tag);
    const auto amount = static_cast<std::uint16_t>(
        (static_cast<unsigned>(bytes[1]) << 8) | bytes[2]);
    if (amount == 0 || amount > 1000) return std::unexpected(ParseError::Range);
    if (bytes[0] == 1) return Command{Damage{amount}};
    return Command{Heal{amount}};
}
struct Delta {
    int operator()(const Damage& d) const { return -static_cast<int>(d.amount); }
    int operator()(const Heal& h) const { return static_cast<int>(h.amount); }
};
// END_PROTOCOL

// BEGIN_LAYOUT
struct Original { char a; double b; char c; int d; };
struct Reordered { double b; int d; char a; char c; };
struct A { virtual int a() const = 0; virtual ~A() = default; };
struct B { virtual int b() const = 0; virtual ~B() = default; };
struct Both final : A, B {
    int a() const override { return 1; }
    int b() const override { return 2; }
};
void layout_observations() {
    std::cout << "OBS Original size/align/offsets=" << sizeof(Original) << '/'
              << alignof(Original) << '/' << offsetof(Original, a) << ','
              << offsetof(Original, b) << ',' << offsetof(Original, c) << ','
              << offsetof(Original, d) << '\n';
    std::cout << "OBS Reordered size/align=" << sizeof(Reordered) << '/'
              << alignof(Reordered) << '\n';
    Both object;
    A* a = &object; B* b = &object;
    std::cout << "OBS base address offsets A/B="
              << reinterpret_cast<std::uintptr_t>(a) - reinterpret_cast<std::uintptr_t>(&object)
              << '/' << reinterpret_cast<std::uintptr_t>(b) - reinterpret_cast<std::uintptr_t>(&object)
              << '\n';
    // Numeric address offsets are observations on this implementation, not ABI constants.
    require(a->a() == 1 && b->b() == 2, "virtual dispatch through both bases");
    require(dynamic_cast<Both*>(b) == &object, "base pointer resolves complete object");
    require(sizeof(Original) % alignof(Original) == 0, "array element size supports alignment");
}
// END_LAYOUT

// BEGIN_CHAIN
std::expected<int, ParseError> doubled_delta(std::span<const std::uint8_t> bytes) {
    return parse(bytes)
        .transform([](const Command& c) { return std::visit(Delta{}, c); })
        .and_then([](int delta) -> std::expected<int, ParseError> {
            if (delta < -500 || delta > 500) return std::unexpected(ParseError::Range);
            return delta * 2; // Bound checked before arithmetic.
        });
}
// END_CHAIN

void atomic_tests() {
    // BEGIN_PUBLICATION
    struct Payload { int sequence; std::string text; };
    Payload payload{};
    std::atomic<bool> ready{false};
    bool observed = false; // Written by consumer, read only after join.
    std::thread producer([&] {
        payload = {42, "inventory-ready"};
        ready.store(true, std::memory_order_release);
    });
    std::thread consumer([&] {
        while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
        observed = payload.sequence == 42 && payload.text == "inventory-ready";
    });
    producer.join();
    consumer.join();
    // END_PUBLICATION
    require(observed, "release-acquire payload after join");
    std::atomic<int> split{0};
    const int a = split.load(std::memory_order_relaxed);
    const int b = split.load(std::memory_order_relaxed);
    split.store(a + 1, std::memory_order_relaxed);
    split.store(b + 1, std::memory_order_relaxed);
    require(split == 1, "two valid atomic stores can lose a composite update");
    // Equal TOTAL operations across thread counts and orders; no timing assertion.
    for (int workers : {1, 4}) {
        for (auto order : {std::memory_order_relaxed, std::memory_order_acq_rel,
                           std::memory_order_seq_cst}) {
            std::atomic<int> n{0};
            std::array<std::thread, 4> threads;
            for (int i = 0; i < workers; ++i) {
                threads[static_cast<std::size_t>(i)] = std::thread([&] {
                    for (int k = 0; k < 40000 / workers; ++k) n.fetch_add(1, order);
                });
            }
            for (int i = 0; i < workers; ++i) threads[static_cast<std::size_t>(i)].join();
            require(n == 40000, "RMW exact equal-total-work count");
        }
    }
    std::atomic<int> n{7};
    int expected = 5;
    require(!n.compare_exchange_strong(expected, 9), "CAS mismatch fails");
    require(expected == 7 && n == 7, "CAS failure refreshes expected");
    require(n.compare_exchange_strong(expected, 9), "CAS matching value succeeds");
    require(n == 9, "CAS success stores desired");
}

struct alignas(64) Item {
    static inline int alive = 0;
    static inline int destroyed = 0;
    int value;
    explicit Item(int v) : value(v) {
        if (v < 0) throw std::runtime_error("construction rejected");
        ++alive;
    }
    ~Item() noexcept { --alive; ++destroyed; }
};
void pool_tests() {
    static_assert(!std::is_copy_constructible_v<FixedPool<Item, 3>>);
    static_assert(!std::is_move_constructible_v<FixedPool<Item, 3>>);
    {
        FixedPool<Item, 3> pool;
        Item* a = pool.create(1);
        Item* b = pool.create(2);
        Item* c = pool.create(3);
        require(a && b && c && Item::alive == 3, "three independent live objects");
        require(a != b && b != c && a != c, "distinct slots");
        require(reinterpret_cast<std::uintptr_t>(a) % alignof(Item) == 0 &&
                reinterpret_cast<std::uintptr_t>(b) % alignof(Item) == 0 &&
                reinterpret_cast<std::uintptr_t>(c) % alignof(Item) == 0,
                "over-aligned slots");
        require(a->value == 1 && b->value == 2 && c->value == 3, "no slot overlap");
        require(pool.create(4) == nullptr, "capacity exhausted");
        require(pool.destroy(b) && Item::alive == 2, "destroy live object");
        // Do not use b again: its object lifetime has ended.
        Item foreign{5};
        require(!pool.destroy(&foreign), "foreign live pointer rejected");
        require(!pool.destroy(nullptr), "null rejected");
        bool caught = false;
        try { (void)pool.create(-1); } catch (const std::runtime_error&) { caught = true; }
        require(caught && Item::alive == 3, "constructor failure preserves free slot");
        Item* reused = pool.create(8);
        require(reused && reused->value == 8 && a->value == 1 && c->value == 3,
                "slot reusable after exception");
        require(pool.create(9) == nullptr, "capacity restored accurately");
    }
    require(Item::alive == 0 && Item::destroyed == 5, "pool destroys all remaining objects once");
    {
        FixedPool<int, 1> one;
        int* p = one.create(9);
        require(p && *p == 9 && one.create(10) == nullptr, "capacity one");
        require(one.destroy(p), "single slot reclaim");
        p = one.create(11);
        require(p && *p == 11, "single slot reuse");
    }
    struct Large { char bytes[100]; };
    std::cout << "OBS sizeof(Large)=" << sizeof(Large) << " alignof=" << alignof(Large) << '\n';
}

void protocol_tests() {
    const std::array<std::uint8_t, 3> damage{1, 0, 42};
    const std::array<std::uint8_t, 3> heal{2, 3, 232};
    auto d = parse(damage);
    auto h = parse(heal);
    require(d && std::holds_alternative<Damage>(*d), "parse damage tag");
    require(d && std::visit(Delta{}, *d) == -42, "visit damage");
    require(h && std::visit(Delta{}, *h) == 1000, "decode big-endian maximum heal");
    require(!parse(std::span<const std::uint8_t>{}), "empty rejected");
    require(!parse(std::span(damage).first(2)), "truncated rejected");
    const std::array<std::uint8_t, 4> trailing{1, 0, 1, 99};
    auto tail = parse(trailing);
    require(!tail && tail.error() == ParseError::Length, "trailing byte rejected");
    const std::array<std::uint8_t, 3> badtag{3, 0, 1}, zero{1, 0, 0}, large{2, 3, 233};
    auto tag = parse(badtag); auto z = parse(zero); auto big = parse(large);
    require(!tag && tag.error() == ParseError::Tag, "unknown tag rejected");
    require(!z && z.error() == ParseError::Range, "zero amount rejected");
    require(!big && big.error() == ParseError::Range, "range overflow rejected");
    std::array<int, 3> data{4, 5, 6};
    std::span<int> view(data);
    view[1] = 7;
    require(data[1] == 7, "span aliases caller storage");
    require(view.subspan(1, 2).size() == 2, "valid bounded subspan");
    auto doubled = doubled_delta(damage);
    require(doubled && *doubled == -84, "expected chain success");
    auto range = doubled_delta(heal);
    require(!range && range.error() == ParseError::Range, "and_then rejects range");
    bool called = false;
    auto failed = parse(badtag).transform([&](const Command&) { called = true; return 0; });
    require(!failed && !called && failed.error() == ParseError::Tag, "transform skips error state");
    bool mismatch = false;
    try { (void)std::get<Heal>(*d); } catch (const std::bad_variant_access&) { mismatch = true; }
    require(mismatch, "wrong variant get throws");
}
int main() {
    atomic_tests(); pool_tests(); protocol_tests(); layout_observations();
    std::cout << "TOTAL " << checks << " PASS\n";
}
