#include "z_net.hpp"
#include <cassert>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

z_net::Addr z_net::Addr::from(int family, const char *ip, uint16_t port) noexcept {
    z_net::Addr addr{}; // zero init
    if (family == AF_INET) {
        addr.sin.sin_family = family;
        inet_pton(AF_INET, ip, &addr.sin.sin_addr);
        addr.sin.sin_port = htons(port);
    } else {
        assert(family == AF_INET6);
        addr.sin6.sin6_family = family;
        inet_pton(AF_INET6, ip, &addr.sin6.sin6_addr);
        addr.sin6.sin6_port = htons(port);
    }
    return addr;
}

void z_net::Addr::tostring(char ip[INET6_ADDRSTRLEN], uint16_t *port) const noexcept {
    if (sa.sa_family == AF_INET) {
        inet_ntop(AF_INET, &sin.sin_addr, ip, INET_ADDRSTRLEN);
        *port = ntohs(sin.sin_port);
    } else {
        assert(sa.sa_family == AF_INET6);
        inet_ntop(AF_INET6, &sin6.sin6_addr, ip, INET6_ADDRSTRLEN);
        *port = ntohs(sin6.sin6_port);
    }
}

int z_net::ip_family(const char *ip) noexcept {
    union { struct in_addr in; struct in6_addr in6; } tmp;
    if (inet_pton(AF_INET, ip, &tmp.in) == 1)
        return AF_INET;
    if (inet_pton(AF_INET6, ip, &tmp.in6) == 1)
        return AF_INET6;
    return -1;
}

int z_net::new_sock(int family, int type) noexcept {
    return ::socket(family, type | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
}

bool z_net::setsockopt_int(int fd, int level, int opt, int value) noexcept {
    return ::setsockopt(fd, level, opt, &value, sizeof(value)) == 0;
}

bool z_net::getsockopt_int(int fd, int level, int opt, int *value) noexcept {
    socklen_t len = sizeof(*value);
    return ::getsockopt(fd, level, opt, value, &len) == 0;
}

bool z_net::set_reuseaddr(int fd) noexcept {
    return setsockopt_int(fd, SOL_SOCKET, SO_REUSEADDR, 1);
}

bool z_net::set_reuseport(int fd) noexcept {
    return setsockopt_int(fd, SOL_SOCKET, SO_REUSEPORT, 1);
}

bool z_net::set_nodelay(int fd) noexcept {
    return setsockopt_int(fd, IPPROTO_TCP, TCP_NODELAY, 1);
}

bool z_net::set_keepalive(int fd, int idle_sec, int interval_sec, int count) noexcept {
    if (!setsockopt_int(fd, SOL_SOCKET, SO_KEEPALIVE, 1)) [[unlikely]]
        return false;
    if (idle_sec > 0 && !setsockopt_int(fd, IPPROTO_TCP, TCP_KEEPIDLE, idle_sec)) [[unlikely]]
        return false;
    if (interval_sec > 0 && !setsockopt_int(fd, IPPROTO_TCP, TCP_KEEPINTVL, interval_sec)) [[unlikely]]
        return false;
    if (count > 0 && !setsockopt_int(fd, IPPROTO_TCP, TCP_KEEPCNT, count)) [[unlikely]]
        return false;
    return true;
}

int z_net::accept(int fd, Addr *addr) noexcept {
    socklen_t addrlen = sizeof(*addr);
    struct sockaddr *raw_addr = addr ? &addr->sa : nullptr;
    socklen_t *raw_addrlen = addr ? &addrlen : nullptr;
    return ::accept4(fd, raw_addr, raw_addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
}

int z_net::connect(int fd, const Addr *addr) noexcept {
    assert(addr != nullptr);
    return ::connect(fd, &addr->sa, addr->len());
}

ssize_t z_net::recvfrom(int fd, void *buf, size_t len, Addr *addr, int flags) noexcept {
    socklen_t addrlen = sizeof(*addr);
    struct sockaddr *raw_addr = addr ? &addr->sa : nullptr;
    socklen_t *raw_addrlen = addr ? &addrlen : nullptr;
    return ::recvfrom(fd, buf, len, flags, raw_addr, raw_addrlen);
}

ssize_t z_net::sendto(int fd, const void *buf, size_t len, const Addr *addr, int flags) noexcept {
    const struct sockaddr *raw_addr = addr ? &addr->sa : nullptr;
    socklen_t raw_addrlen = addr ? addr->len() : 0;
    return ::sendto(fd, buf, len, flags, raw_addr, raw_addrlen);
}
