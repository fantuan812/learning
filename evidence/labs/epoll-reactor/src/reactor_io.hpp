#ifndef EPOLL_LAB_REACTOR_IO_HPP
#define EPOLL_LAB_REACTOR_IO_HPP

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace lab {
[[noreturn]] inline void system_error(const char* operation) {
    const int error = errno;
    throw std::runtime_error(std::string(operation) + ": " + std::strerror(error));
}

class UniqueFd {
    int fd_ = -1;
public:
    explicit UniqueFd(int fd = -1) : fd_(fd) {}
    ~UniqueFd() { if (fd_ >= 0) ::close(fd_); } // Linux: never retry close after EINTR.
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = other.release();
        }
        return *this;
    }
    int get() const { return fd_; }
    int release() { return std::exchange(fd_, -1); }
    explicit operator bool() const { return fd_ >= 0; }
};

struct WriteStats {
    std::uint64_t partial = 0;
    std::uint64_t would_block = 0;
};

// Fixed-capacity ring: storage never grows with a slow reader's backlog.
// Kernel TCP buffers and per-connection metadata are separate from this bound.
class OutputQueue {
    std::vector<char> bytes_;
    std::size_t begin_ = 0;
    std::size_t size_ = 0;
public:
    explicit OutputQueue(std::size_t capacity) : bytes_(capacity) {
        if (capacity == 0) throw std::invalid_argument("zero queue capacity");
    }
    std::size_t size() const { return size_; }
    std::size_t free_space() const { return bytes_.size() - size_; }
    bool empty() const { return size_ == 0; }
    void append(const char* data, std::size_t size) {
        if (size > free_space()) throw std::length_error("output queue limit");
        const auto end = (begin_ + size_) % bytes_.size();
        const auto first = std::min(size, bytes_.size() - end);
        std::memcpy(bytes_.data() + end, data, first);
        std::memcpy(bytes_.data(), data + first, size - first);
        size_ += size;
    }
    // false means a hard peer error. true includes EAGAIN, with bytes retained.
    bool flush(int fd, WriteStats& stats) {
        while (size_ > 0) {
            const auto requested = std::min(size_, bytes_.size() - begin_);
            const ssize_t sent = ::send(fd, bytes_.data() + begin_, requested, MSG_NOSIGNAL);
            if (sent > 0) {
                const auto count = static_cast<std::size_t>(sent);
                if (count < requested) ++stats.partial;
                begin_ = (begin_ + count) % bytes_.size();
                size_ -= count;
                continue;
            }
            if (sent < 0 && errno == EINTR) continue;
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                ++stats.would_block;
                return true;
            }
            return false; // Includes a non-progressing send(>0 bytes) result of 0.
        }
        return true;
    }
};
} // namespace lab
#endif
