// echo_server_epoll.cpp — epoll LT/ET + Reactor 最小实现（证据：evidence/labs/epoll-reactor）
//
// 实验问题：
//  1) LT（水平触发）与 ET（边沿触发）在可读事件处理上的差异；
//  2) 非阻塞 IO + EAGAIN 处理（partial read）；
//  3) 单线程 Reactor 的事件循环结构。
//
// 用法：./echo_server_epoll <port> <lt|et>
// 预期：客户端连接后发送任意文本，服务端原样回显（Echo）。
// 说明：本实验需 Linux（epoll 为 Linux 专有）；Windows 环境未执行，属"待执行"证据。
//
// 构建：g++ -O2 -std=c++17 -Wall echo_server_epoll.cpp -o echo_server_epoll

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "用法: %s <port> <lt|et>\n", argv[0]);
        return 1;
    }
    const int port = std::atoi(argv[1]);
    const bool edge = std::string(argv[2]) == "et";

    int listenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listenFd < 0) { std::perror("socket"); return 1; }
    int one = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0) { std::perror("bind"); return 1; }
    if (listen(listenFd, 128) < 0) { std::perror("listen"); return 1; }

    int ep = epoll_create1(0);
    epoll_event ev{};
    ev.events = EPOLLIN;                 // LT：水平触发（默认）
    ev.data.fd = listenFd;
    epoll_ctl(ep, EPOLL_CTL_ADD, listenFd, &ev);

    std::printf("echo server: port=%d mode=%s pid=%d\n", port, edge ? "ET" : "LT", getpid());
    std::vector<char> buf(4096);
    epoll_event events[64];

    for (;;) {
        int n = epoll_wait(ep, events, 64, -1);
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == listenFd) {
                // 接受所有待处理连接（ET 下必须循环 accept 到 EAGAIN）
                for (;;) {
                    int cfd = accept4(listenFd, nullptr, nullptr, SOCK_NONBLOCK);
                    if (cfd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        std::perror("accept");
                        break;
                    }
                    epoll_event cev{};
                    cev.events = EPOLLIN | (edge ? EPOLLET : 0);
                    cev.data.fd = cfd;
                    epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &cev);
                    std::printf("+ conn fd=%d\n", cfd);
                }
                continue;
            }
            // 客户端可读
            for (;;) {
                ssize_t r = read(fd, buf.data(), buf.size());
                if (r > 0) {
                    // 回显（示意：写失败/部分写未完整处理，生产需写缓冲 + EPOLLOUT）
                    ssize_t w = write(fd, buf.data(), (size_t)r);
                    std::printf("echo fd=%d %zd bytes (wrote %zd)\n", fd, r, w);
                    if (edge) continue;   // ET：必须读到 EAGAIN
                    break;                // LT：等下次事件（剩余数据会再触发）
                }
                if (r == 0) {
                    std::printf("- close fd=%d\n", fd);
                    epoll_ctl(ep, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    break;
                }
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;   // 数据读完（ET 关键）
                std::perror("read");
                epoll_ctl(ep, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
                break;
            }
        }
    }
}
