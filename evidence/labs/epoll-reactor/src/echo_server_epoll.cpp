// Linux C++17 teaching Reactor: ./echo_server_epoll <port:0..65535> <lt|et>
// Loopback only. Port 0 asks the kernel for an unused port; READY prints it.
// Both modes drain nonblocking I/O. Mechanism tests isolate the LT/ET contract.
// This is not a production server: no fairness budget, idle timeout, TLS,
// authentication, worker pool, or application framing. Never expose externally.

#include "reactor_io.hpp"

#include <arpa/inet.h>
#include <array>
#include <charconv>
#include <csignal>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <unordered_map>

namespace {
constexpr std::size_t kQueueLimit = 256 * 1024;
constexpr std::size_t kLowWater = kQueueLimit / 2;
constexpr std::size_t kConnectionLimit = 64;
constexpr std::uint64_t kListener = 0;
volatile std::sig_atomic_t stopping = 0;
void stop_handler(int) { stopping = 1; }

struct Connection {
    lab::UniqueFd fd;
    lab::OutputQueue output{kQueueLimit};
    bool eof = false;
    bool paused = false;
    std::uint32_t interest = 0;
    explicit Connection(lab::UniqueFd descriptor) : fd(std::move(descriptor)) {}
};

struct Statistics {
    lab::WriteStats writes;
    std::uint64_t accepted = 0;
    std::uint64_t closed = 0;
    std::uint64_t read_pauses = 0;
    std::size_t max_pending = 0;
};

// Returns false only on peer failure or after EOF and all queued output is sent.
bool pump(Connection& c, std::uint64_t id, Statistics& stats) {
    if (!c.output.flush(c.fd.get(), stats.writes)) return false;
    if (c.paused && c.output.size() <= kLowWater) c.paused = false;
    std::array<char, 16384> input{};
    while (!stopping && !c.eof && !c.paused) {
        const std::size_t count = std::min(input.size(), c.output.free_space());
        if (count == 0) {
            c.paused = true;
            ++stats.read_pauses;
            std::cout << "PAUSE id=" << id << " pending=" << c.output.size() << '\n'
                      << std::flush;
            break;
        }
        const ssize_t n = ::recv(c.fd.get(), input.data(), count, 0);
        if (n > 0) {
            c.output.append(input.data(), static_cast<std::size_t>(n));
            stats.max_pending = std::max(stats.max_pending, c.output.size());
            if (!c.output.flush(c.fd.get(), stats.writes)) return false;
            continue;
        }
        if (n == 0) { c.eof = true; break; }
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        std::cerr << "recv id=" << id << ": " << std::strerror(errno) << '\n';
        return false;
    }
    // EPOLLOUT callbacks also enter the read loop after low-water resumption.
    // In ET mode we cannot wait for a fresh EPOLLIN edge for already queued data.
    return !(c.eof && c.output.empty());
}

std::uint32_t interest_for(const Connection& c, bool edge) {
    std::uint32_t flags = edge ? static_cast<std::uint32_t>(EPOLLET) : 0U;
    if (!c.eof && !c.paused) flags |= EPOLLIN | EPOLLRDHUP;
    if (!c.output.empty()) flags |= EPOLLOUT;
    // In LT, retaining RDHUP while paused could wake forever on a half-close.
    return flags;
}

void control(int ep, int operation, int fd, std::uint64_t id, std::uint32_t mask) {
    epoll_event event{};
    event.events = mask;
    event.data.u64 = id;
    int rc;
    do { rc = ::epoll_ctl(ep, operation, fd, &event); } while (rc < 0 && errno == EINTR);
    if (rc < 0) lab::system_error("epoll_ctl");
}

int run(unsigned port, bool edge) {
    struct sigaction action{};
    action.sa_handler = stop_handler;
    if (::sigemptyset(&action.sa_mask) < 0 ||
        ::sigaction(SIGINT, &action, nullptr) < 0 ||
        ::sigaction(SIGTERM, &action, nullptr) < 0) lab::system_error("sigaction");

    lab::UniqueFd listener(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (!listener) lab::system_error("socket");
    const int one = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
        lab::system_error("setsockopt SO_REUSEADDR");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
        lab::system_error("bind");
    if (::listen(listener.get(), 128) < 0) lab::system_error("listen");
    socklen_t length = sizeof(address);
    if (::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &length) < 0)
        lab::system_error("getsockname");
    lab::UniqueFd ep(::epoll_create1(EPOLL_CLOEXEC));
    if (!ep) lab::system_error("epoll_create1");
    control(ep.get(), EPOLL_CTL_ADD, listener.get(), kListener,
            EPOLLIN | (edge ? static_cast<std::uint32_t>(EPOLLET) : 0U));
    std::unordered_map<std::uint64_t, Connection> clients;
    std::uint64_t next_id = 1;
    Statistics stats;
    std::cout << "READY address=127.0.0.1 port=" << ntohs(address.sin_port)
              << " mode=" << (edge ? "et" : "lt")
              << " queue_limit=" << kQueueLimit << " max_connections=" << kConnectionLimit
              << '\n' << std::flush;
    std::array<epoll_event, 64> events{};
    while (!stopping) {
        // Bounded signal/epoll_wait race: a stop just before wait costs <=1 s.
        const int count = ::epoll_wait(ep.get(), events.data(), events.size(), 1000);
        if (count < 0) {
            if (errno == EINTR) continue;
            lab::system_error("epoll_wait");
        }
        for (int i = 0; i < count && !stopping; ++i) {
            const auto id = events[static_cast<std::size_t>(i)].data.u64;
            if (id == kListener) {
                while (!stopping) {
                    lab::UniqueFd incoming(::accept4(listener.get(), nullptr, nullptr,
                                                     SOCK_NONBLOCK | SOCK_CLOEXEC));
                    if (!incoming) {
                        if (errno == EINTR) continue;
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == ECONNABORTED || errno == EPROTO || errno == ENETDOWN ||
                            errno == ENONET || errno == ENETUNREACH || errno == EHOSTDOWN ||
                            errno == EHOSTUNREACH || errno == EOPNOTSUPP || errno == ENOPROTOOPT)
                            continue;
                        // Fail closed on resource exhaustion rather than spinning on EMFILE.
                        lab::system_error("accept4");
                    }
                    if (clients.size() >= kConnectionLimit) continue; // RAII rejects excess peers.
                    // Deliberately small for reproducible lab backpressure, not throughput tuning.
                    const int send_buffer = 16384;
                    if (::setsockopt(incoming.get(), SOL_SOCKET, SO_SNDBUF,
                                     &send_buffer, sizeof(send_buffer)) < 0)
                        lab::system_error("setsockopt SO_SNDBUF");
                    const std::uint64_t token = next_id++;
                    auto entry = clients.try_emplace(token, std::move(incoming)).first;
                    auto& c = entry->second;
                    c.interest = interest_for(c, edge);
                    control(ep.get(), EPOLL_CTL_ADD, c.fd.get(), token, c.interest);
                    ++stats.accepted;
                }
                continue;
            }
            auto found = clients.find(id);
            if (found == clients.end()) continue; // Stale batch entry cannot target a reused fd.
            auto& c = found->second;
            bool keep = true;
            if ((events[static_cast<std::size_t>(i)].events & EPOLLERR) != 0) {
                int error = 0;
                socklen_t size = sizeof(error);
                if (::getsockopt(c.fd.get(), SOL_SOCKET, SO_ERROR, &error, &size) < 0)
                    lab::system_error("getsockopt SO_ERROR");
                if (error != 0) {
                    std::cerr << "peer-error id=" << id << ": " << std::strerror(error) << '\n';
                    keep = false;
                }
            }
            if (keep) keep = pump(c, id, stats); // Includes HUP/RDHUP; recv(0) establishes EOF.
            if (!keep) {
                control(ep.get(), EPOLL_CTL_DEL, c.fd.get(), id, 0);
                clients.erase(found);
                ++stats.closed;
            } else {
                const auto mask = interest_for(c, edge);
                if (mask != c.interest) {
                    control(ep.get(), EPOLL_CTL_MOD, c.fd.get(), id, mask);
                    c.interest = mask;
                }
            }
        }
    }
    std::cout << "STATS accepted=" << stats.accepted << " closed=" << stats.closed
              << " read_pauses=" << stats.read_pauses
              << " partial_writes=" << stats.writes.partial
              << " write_eagain=" << stats.writes.would_block
              << " max_pending=" << stats.max_pending << '\n';
    // Shutdown closes all sockets; it is not a graceful whole-server drain protocol.
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <port:0..65535> <lt|et>\n";
        return 2;
    }
    const std::string port_text(argv[1]);
    const std::string mode(argv[2]);
    unsigned port = 0;
    const auto parsed = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != port_text.data() + port_text.size() ||
        port > 65535 || (mode != "lt" && mode != "et")) {
        std::cerr << "invalid port or mode\n";
        return 2;
    }
    try { return run(port, mode == "et"); }
    catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << '\n';
        return 1;
    }
}
