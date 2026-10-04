// C++23 semantic probes; no global allocator replacement or timing claims.
#include <algorithm>
#include <cstddef>
#include <exception>
#include <expected>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>
#include <vector>

#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "Requires C++23 <expected> with __cpp_lib_expected >= 202202L"
#endif

namespace {
int checks = 0;
int failures = 0;
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL: " << name << '\n'; }
}

struct Resource final : std::pmr::memory_resource {
    std::size_t allocations = 0, deallocations = 0, bytes = 0;
private:
    void* do_allocate(std::size_t n, std::size_t alignment) override {
        void* p = std::pmr::new_delete_resource()->allocate(n, alignment);
        ++allocations; bytes += n;
        return p;
    }
    void do_deallocate(void* p, std::size_t n, std::size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(p, n, alignment);
        ++deallocations;
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

template<bool ErrorSide> void resource_payload() {
    Resource resource;
    std::pmr::polymorphic_allocator<int> allocator{&resource};
    constexpr std::size_t count = 1024;
    {
        if constexpr (ErrorSide) {
            std::expected<int, std::pmr::vector<int>> result(
                std::unexpect, count, 7, allocator);
            check(!result.has_value(), "error state");
            check(result.error().size() == count, "error vector size");
            check(result.error().back() == 7, "error vector contents");
        } else {
            std::expected<std::pmr::vector<int>, int> result(
                std::in_place, count, 7, allocator);
            check(result.has_value(), "value state");
            check(result->size() == count, "value vector size");
            check(result->back() == 7, "value vector contents");
        }
    }
    check(resource.allocations > 0, "payload requested dynamic storage");
    check(resource.allocations == resource.deallocations, "payload releases storage");
    std::cout << (ErrorSide ? "error_payload" : "value_payload")
              << " elements=" << count << " allocations=" << resource.allocations
              << " deallocations=" << resource.deallocations
              << " requested_bytes=" << resource.bytes << '\n';
}

struct Counts { int destroyed = 0, unwinding = 0, incomplete_destroyed = 0; };
struct Probe {
    Counts& counts;
    ~Probe() noexcept {
        ++counts.destroyed;
        if (std::uncaught_exceptions() > 0) ++counts.unwinding;
    }
};
struct ConstructorFailure {};
struct ThrowingPayload {
    Probe completed_member;
    explicit ThrowingPayload(Counts& counts) : completed_member{counts} {
        throw ConstructorFailure{};
    }
    ~ThrowingPayload() noexcept { ++completed_member.counts.incomplete_destroyed; }
};

template<bool ErrorSide> void constructor_exception() {
    Counts counts;
    bool caught = false;
    try {
        Probe caller{counts};
        if constexpr (ErrorSide) {
            std::expected<int, ThrowingPayload> result(std::unexpect, counts);
            (void)result;
        } else {
            std::expected<ThrowingPayload, int> result(std::in_place, counts);
            (void)result;
        }
    } catch (const ConstructorFailure&) { caught = true; }
    check(caught, "payload constructor exception propagates");
    check(counts.destroyed == 2, "completed member and caller are destroyed");
    check(counts.unwinding == 2, "both probes observe an exception in flight");
    check(counts.incomplete_destroyed == 0, "incomplete payload is not destroyed");
    std::cout << (ErrorSide ? "error_ctor" : "value_ctor")
              << " caught=" << caught << " destroyed=" << counts.destroyed
              << " unwinding=" << counts.unwinding
              << " incomplete_destroyed=" << counts.incomplete_destroyed << '\n';
}

void observers() {
    std::expected<int, int> failure(std::unexpect, 17);
    int handled = 0;
    if (!failure.has_value()) handled = failure.error();
    check(handled == 17, "explicit business error branch");
    std::expected<int, int> success(42);
    check(success.value() == 42, "value() on success");
    std::cout << "guarded_error code=" << handled << " success_value=" << success.value() << '\n';
    Counts counts;
    bool caught = false;
    try {
        Probe caller{counts};
        (void)failure.value();
    } catch (const std::bad_expected_access<int>& error) {
        caught = true;
        check(error.error() == 17, "bad_expected_access carries error");
    }
    check(caught, "value() on error throws");
    check(counts.destroyed == 1, "caller destroyed after value() throw");
    check(counts.unwinding == 1, "caller observes value() unwinding");
    std::cout << "value_on_error caught=" << caught << " destroyed=" << counts.destroyed
              << " unwinding=" << counts.unwinding << '\n';
}

void storage_duration() {
    static std::expected<int, int> a(3);
    thread_local std::expected<int, int> b(4);
    std::expected<int, int> c(5);
    auto d = std::make_unique<std::expected<int, int>>(6);
    check(*a == 3, "static wrapper"); check(*b == 4, "thread-local wrapper");
    check(*c == 5, "automatic wrapper"); check(**d == 6, "dynamic wrapper");
    // Legal storage durations, not a measurement of physical placement or allocations.
    std::cout << "wrapper_storage static=" << *a << " thread=" << *b
              << " automatic=" << *c << " dynamic=" << **d << '\n';
}
struct alignas(64) Aligned { unsigned char byte; };
template<class T, class E> void layout(const char* name) {
    const auto maximum = std::max(sizeof(T), sizeof(E));
    // Diagnostic only: no fixed-size or +8 assertion.
    std::cout << "layout " << name << " max_payload=" << maximum
              << " expected=" << sizeof(std::expected<T, E>)
              << " alignment=" << alignof(std::expected<T, E>)
              << " difference=" << sizeof(std::expected<T, E>) - maximum << '\n';
}
} // namespace

int main() {
    std::cout << "__cplusplus=" << __cplusplus
              << "\n__cpp_lib_expected=" << __cpp_lib_expected << '\n';
#ifdef __GLIBCXX__
    std::cout << "__GLIBCXX__=" << __GLIBCXX__ << '\n';
#endif
#ifdef _GLIBCXX_RELEASE
    std::cout << "_GLIBCXX_RELEASE=" << _GLIBCXX_RELEASE << '\n';
#endif
    static_assert(noexcept(std::declval<const std::expected<int, int>&>().has_value()));
    static_assert(!std::is_nothrow_constructible_v<
                  std::expected<ThrowingPayload, int>, std::in_place_t, Counts&>);
    layout<char, char>("char,char");
    layout<int, int>("int,int");
    layout<Aligned, char>("aligned64,char");
    storage_duration();
    resource_payload<false>(); resource_payload<true>();
    constructor_exception<false>(); constructor_exception<true>();
    observers();
    std::cout << "checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
