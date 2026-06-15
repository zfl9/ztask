#pragma once
#include <cstdint>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

struct z_net {
    z_net() = delete;
    ~z_net() = delete;

    union Addr {
        struct sockaddr_in6 sin6;
        struct sockaddr_in sin;
        struct sockaddr sa;

        struct Text {
            char ip[INET6_ADDRSTRLEN];
            uint16_t port;
        };

        static Addr from(int family, const char *ip, uint16_t port) noexcept;
        Text to_text() const noexcept;

        bool is_valid() const noexcept { return sa.sa_family != 0; }
        bool is_ipv4() const noexcept { return sa.sa_family == AF_INET; }
        bool is_ipv6() const noexcept { return sa.sa_family == AF_INET6; }
        socklen_t len() const noexcept { return is_ipv4() ? sizeof(sin) : sizeof(sin6); }
    };

    static void ignore_sigpipe() noexcept;

    // AF_INET, AF_INET6, -1(error)
    static int ip_family(const char *ip) noexcept;

    // non-blocking
    static int new_sock(int family, int type) noexcept;

    static int setsockopt_int(int fd, int level, int opt, int value) noexcept;
    static int getsockopt_int(int fd, int level, int opt, int *value) noexcept;

    static int set_ipv6only(int fd, bool v6only) noexcept;
    static int set_reuseaddr(int fd) noexcept;
    static int set_reuseport(int fd) noexcept;
    static int set_nodelay(int fd) noexcept;
    static int set_keepalive(int fd, int idle_sec, int interval_sec, int count) noexcept;

    static int getsockname(int fd, Addr *addr) noexcept;
    static int getpeername(int fd, Addr *addr) noexcept;

    static int accept(int fd, Addr *addr) noexcept;
    static int connect(int fd, const Addr *addr) noexcept;

    static ssize_t recvfrom(int fd, void *buf, size_t len, Addr *addr, int flags = 0) noexcept;
    static ssize_t sendto(int fd, const void *buf, size_t len, const Addr *addr, int flags = 0) noexcept;

    static ssize_t readv(int fd, const iovec *iov, int iovcnt, size_t skip_bytes = 0) noexcept;
    static ssize_t writev(int fd, const iovec *iov, int iovcnt, size_t skip_bytes = 0) noexcept;

    static ssize_t recvmsg(int fd, msghdr *msg, int flags = 0, size_t skip_bytes = 0) noexcept;
    static ssize_t sendmsg(int fd, const msghdr *msg, int flags = 0, size_t skip_bytes = 0) noexcept;
};
