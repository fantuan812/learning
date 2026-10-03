// Deterministic readiness mechanisms, not a latency/throughput benchmark.
#include "../src/reactor_io.hpp"

#include <array>
#include <chrono>
#include <csignal>
#include <iostream>
#include <sys/epoll.h>
#include <sys/ioctl.h>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
using Clock = std::chrono::steady_clock;

struct Pair {
    lab::UniqueFd reader;
    lab::UniqueFd writer;
    Pair() {
        int fds[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fds) < 0)
            lab::system_error("socketpair");
        reader = lab::UniqueFd(fds[0]);
        writer = lab::UniqueFd(fds[1]);
    }
};

lab::UniqueFd epoll_for(int fd, std::uint32_t mask) {
    lab::UniqueFd ep(::epoll_create1(EPOLL_CLOEXEC));
    if (!ep) lab::system_error("epoll_create1");
    epoll_event event{};
    event.events = mask;
    event.data.fd = fd;
    if (::epoll_ctl(ep.get(), EPOLL_CTL_ADD, fd, &event) < 0) lab::system_error("epoll_ctl");
    return ep;
}

int ready(int ep, int timeout_ms, std::uint32_t expected) {
    epoll_event event{};
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        const int result = ::epoll_wait(ep, &event, 1, timeout_ms);
        if (result < 0 && errno == EINTR) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            require(left.count() >= 0, "epoll_wait EINTR deadline exceeded");
            timeout_ms = static_cast<int>(left.count());
            continue;
        }
        require(result >= 0, "epoll_wait failed");
        if (result > 0) require((event.events & expected) != 0, "unexpected event mask");
        return result;
    }
}

void write_small(int fd, const std::string& bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto n = ::send(fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        require(n > 0, "small send failed");
        sent += static_cast<std::size_t>(n);
    }
}
std::string read_available(int fd) {
    std::string output;
    std::array<char, 8192> bytes{};
    for (;;) {
        const auto n = ::recv(fd, bytes.data(), bytes.size(), 0);
        if (n > 0) { output.append(bytes.data(), static_cast<std::size_t>(n)); continue; }
        if (n < 0 && errno == EINTR) continue;
        require(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK), "expected recv EAGAIN");
        return output;
    }
}
void read_one(int fd) {
    char byte = 0;
    ssize_t count;
    do { count = ::recv(fd, &byte, 1, 0); } while (count < 0 && errno == EINTR);
    require(count == 1 && byte == 'a', "expected first byte a");
}

void lt_remaining() {
    Pair pair;
    auto ep = epoll_for(pair.reader.get(), EPOLLIN);
    write_small(pair.writer.get(), "abcdef");
    require(ready(ep.get(), 1000, EPOLLIN) == 1, "LT first readiness missing");
    read_one(pair.reader.get());
    require(ready(ep.get(), 0, EPOLLIN) == 1, "LT remaining data did not repeat readiness");
    require(read_available(pair.reader.get()) == "bcdef", "LT unread bytes changed");
    require(ready(ep.get(), 0, EPOLLIN) == 0, "LT ready after drain");
}
void et_remaining() {
    Pair pair;
    auto ep = epoll_for(pair.reader.get(), EPOLLIN | EPOLLET);
    write_small(pair.writer.get(), "abcdef");
    require(ready(ep.get(), 1000, EPOLLIN) == 1, "ET first readiness missing");
    read_one(pair.reader.get());
    // No new producer action occurs. This is a deliberately stalled consumer.
    require(ready(ep.get(), 0, EPOLLIN) == 0, "unexpected second ET event in controlled setup");
    int pending = 0;
    require(::ioctl(pair.reader.get(), FIONREAD, &pending) == 0 && pending == 5,
            "ET no-event did not preserve the five unread bytes");
    require(read_available(pair.reader.get()) == "bcdef", "ET unread data was lost");
    write_small(pair.writer.get(), "new");
    require(ready(ep.get(), 1000, EPOLLIN) == 1, "ET missing new readiness after drain");
    require(read_available(pair.reader.get()) == "new", "ET new bytes mismatch");
}
void preexisting(bool edge) {
    Pair pair;
    write_small(pair.writer.get(), "already-ready");
    auto ep = epoll_for(pair.reader.get(), EPOLLIN | (edge ? static_cast<std::uint32_t>(EPOLLET) : 0U));
    require(ready(ep.get(), 1000, EPOLLIN) == 1, "ADD missed preexisting readable data");
    require(read_available(pair.reader.get()) == "already-ready", "preexisting bytes mismatch");
}

void write_resume() {
    Pair pair;
    const int small = 4096;
    require(::setsockopt(pair.writer.get(), SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)) == 0,
            "setsockopt SO_SNDBUF failed");
    std::string payload(512 * 1024 + 317, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>((i * 37 + 11) % 256);
    lab::OutputQueue output(128 * 1024);
    lab::WriteStats stats;
    std::size_t admitted = output.free_space();
    output.append(payload.data(), admitted);
    require(output.flush(pair.writer.get(), stats), "initial flush hard failure");
    require(stats.partial > 0 && stats.would_block > 0 && !output.empty(),
            "did not force both partial send and EAGAIN");
    auto ep = epoll_for(pair.writer.get(), EPOLLOUT | EPOLLET);
    require(ready(ep.get(), 0, EPOLLOUT) == 0, "full send buffer unexpectedly writable");
    std::string received;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!output.empty() || admitted < payload.size()) {
        require(Clock::now() < deadline, "write continuation deadline exceeded");
        received += read_available(pair.reader.get());
        require(ready(ep.get(), 1000, EPOLLOUT) == 1, "EPOLLOUT missing after peer drained");
        const auto take = std::min(output.free_space(), payload.size() - admitted);
        output.append(payload.data() + admitted, take); // Exercises ring wraparound too.
        admitted += take;
        require(output.flush(pair.writer.get(), stats), "continued flush hard failure");
    }
    received += read_available(pair.reader.get());
    require(received == payload, "partial/EAGAIN continuation lost, duplicated, or reordered bytes");
    std::cout << "DETAIL write_resume bytes=" << payload.size() << " partial=" << stats.partial
              << " eagain=" << stats.would_block << '\n';
}
void bounded_queue() {
    lab::OutputQueue output(4);
    output.append("1234", 4);
    bool refused = false;
    try { output.append("5", 1); }
    catch (const std::length_error&) { refused = true; }
    require(refused && output.size() == 4, "queue overflow was not rejected without mutation");
}
void no_sigpipe() {
    Pair pair;
    pair.reader = lab::UniqueFd();
    lab::OutputQueue output(16);
    output.append("closed", 6);
    lab::WriteStats stats;
    require(!output.flush(pair.writer.get(), stats), "closed peer must fail flush");
    require(output.size() == 6, "failed send consumed pending bytes");
}
} // namespace

int main() {
    ::alarm(20); // Whole-binary watchdog, even when run without build_run.sh.
    int passed = 0;
    try {
        const auto test = [&passed](const char* name, const auto& run) {
            run();
            ++passed;
            std::cout << "PASS mechanism " << name << '\n';
        };
        test("lt_remaining_repeats", lt_remaining);
        test("et_unread_stalls_without_data_loss", et_remaining);
        test("lt_preexisting_data_at_add", [] { preexisting(false); });
        test("et_preexisting_data_at_add", [] { preexisting(true); });
        test("partial_write_eagain_epollout_lossless", write_resume);
        test("bounded_queue_rejects_overflow", bounded_queue);
        test("msg_nosignal_on_peer_close", no_sigpipe);
        std::cout << "SUMMARY mechanism passed=" << passed << " failed=0\n";
        ::alarm(0);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL mechanism after=" << passed << ": " << error.what() << '\n';
        return 1;
    }
}
