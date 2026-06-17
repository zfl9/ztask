#include "z_net.hpp"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

z_net::Addr z_net::Addr::from(int family, const char *ip, uint16_t port) noexcept {
    z_net::Addr addr{}; // zero init
    if (family == AF_INET) {
        if (inet_pton(AF_INET, ip, &addr.sin.sin_addr) == 1) {
            addr.sin.sin_family = family;
            addr.sin.sin_port = htons(port);
        }
    } else {
        assert(family == AF_INET6);
        if (inet_pton(AF_INET6, ip, &addr.sin6.sin6_addr) == 1) {
            addr.sin6.sin6_family = family;
            addr.sin6.sin6_port = htons(port);
        }
    }
    return addr;
}

z_net::Addr::Text z_net::Addr::to_text() const noexcept {
    Text res;
    if (sa.sa_family == AF_INET) {
        inet_ntop(AF_INET, &sin.sin_addr, res.ip, sizeof(res.ip));
        res.port = ntohs(sin.sin_port);
    } else {
        assert(sa.sa_family == AF_INET6);
        inet_ntop(AF_INET6, &sin6.sin6_addr, res.ip, sizeof(res.ip));
        res.port = ntohs(sin6.sin6_port);
    }
    return res;
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

int z_net::setsockopt_int(int fd, int level, int opt, int value) noexcept {
    return ::setsockopt(fd, level, opt, &value, sizeof(value));
}

int z_net::getsockopt_int(int fd, int level, int opt, int *value) noexcept {
    socklen_t len = sizeof(*value);
    return ::getsockopt(fd, level, opt, value, &len);
}

int z_net::set_ipv6only(int fd, bool v6only) noexcept {
    return setsockopt_int(fd, IPPROTO_IPV6, IPV6_V6ONLY, v6only ? 1 : 0);
}

int z_net::set_reuseaddr(int fd) noexcept {
    return setsockopt_int(fd, SOL_SOCKET, SO_REUSEADDR, 1);
}

int z_net::set_reuseport(int fd) noexcept {
    return setsockopt_int(fd, SOL_SOCKET, SO_REUSEPORT, 1);
}

int z_net::set_nodelay(int fd) noexcept {
    return setsockopt_int(fd, IPPROTO_TCP, TCP_NODELAY, 1);
}

int z_net::set_keepalive(int fd, int idle_sec, int interval_sec, int count) noexcept {
    if (setsockopt_int(fd, SOL_SOCKET, SO_KEEPALIVE, 1) < 0) [[unlikely]]
        return -1;
    if (idle_sec > 0 && setsockopt_int(fd, IPPROTO_TCP, TCP_KEEPIDLE, idle_sec) < 0) [[unlikely]]
        return -1;
    if (interval_sec > 0 && setsockopt_int(fd, IPPROTO_TCP, TCP_KEEPINTVL, interval_sec) < 0) [[unlikely]]
        return -1;
    if (count > 0 && setsockopt_int(fd, IPPROTO_TCP, TCP_KEEPCNT, count) < 0) [[unlikely]]
        return -1;
    return 0;
}

int z_net::getsockname(int fd, Addr *addr) noexcept {
    assert(addr != nullptr);
    socklen_t addrlen = sizeof(*addr);
    return ::getsockname(fd, &addr->sa, &addrlen);
}

int z_net::getpeername(int fd, Addr *addr) noexcept {
    assert(addr != nullptr);
    socklen_t addrlen = sizeof(*addr);
    return ::getpeername(fd, &addr->sa, &addrlen);
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

namespace {
    struct iov_slice_result {
        const iovec *iov;
        int iovcnt;
    };

    iov_slice_result iov_slice(iovec *tmp_iov, int tmp_iovmax,
        const iovec *raw_iov, int raw_iovcnt,
        size_t skip_bytes) noexcept
    {
        int skip_iovcnt = 0;
        while (skip_iovcnt < raw_iovcnt && skip_bytes >= raw_iov[skip_iovcnt].iov_len) {
            skip_bytes -= raw_iov[skip_iovcnt].iov_len;
            ++skip_iovcnt;
        }

        if (skip_bytes == 0) {
            return {
                .iov = raw_iov + skip_iovcnt, 
                .iovcnt = raw_iovcnt - skip_iovcnt,
            };
        } else {
            int remain_iovcnt = raw_iovcnt - skip_iovcnt;
            if (remain_iovcnt <= 0 || remain_iovcnt > tmp_iovmax) [[unlikely]] {
                errno = EINVAL;
                return {}; // null
            }

            tmp_iov[0] = {
                .iov_base = (char *)raw_iov[skip_iovcnt].iov_base + skip_bytes,
                .iov_len = raw_iov[skip_iovcnt].iov_len - skip_bytes,
            };
            if (remain_iovcnt > 1) {
                memcpy(&tmp_iov[1], &raw_iov[skip_iovcnt + 1], (remain_iovcnt - 1) * sizeof(iovec));
            }
            return {
                .iov = tmp_iov,
                .iovcnt = remain_iovcnt,
            };
        }
    }
}

ssize_t z_net::readv(int fd, const iovec *raw_iov, int raw_iovcnt, size_t skip_bytes) noexcept {
    if (skip_bytes == 0) {
        return ::readv(fd, raw_iov, raw_iovcnt);
    } else {
        constexpr int tmp_iovmax = 32;
        iovec tmp_iov[tmp_iovmax];

        auto [iov, iovcnt] = iov_slice(tmp_iov, tmp_iovmax, raw_iov, raw_iovcnt, skip_bytes);
        if (!iov) [[unlikely]] {
            // errno has been set
            return -1;
        }
        return ::readv(fd, iov, iovcnt);
    }
}

ssize_t z_net::writev(int fd, const iovec *raw_iov, int raw_iovcnt, size_t skip_bytes) noexcept {
    if (skip_bytes == 0) {
        return ::writev(fd, raw_iov, raw_iovcnt);
    } else {
        constexpr int tmp_iovmax = 32;
        iovec tmp_iov[tmp_iovmax];

        auto [iov, iovcnt] = iov_slice(tmp_iov, tmp_iovmax, raw_iov, raw_iovcnt, skip_bytes);
        if (!iov) [[unlikely]] {
            // errno has been set
            return -1;
        }
        return ::writev(fd, iov, iovcnt);
    }
}

ssize_t z_net::recvmsg(int fd, msghdr *raw_msg, int flags, size_t skip_bytes) noexcept {
    if (skip_bytes == 0) {
        return ::recvmsg(fd, raw_msg, flags);
    } else {
        constexpr int tmp_iovmax = 32;
        iovec tmp_iov[tmp_iovmax];

        auto [iov, iovcnt] = iov_slice(tmp_iov, tmp_iovmax, raw_msg->msg_iov, raw_msg->msg_iovlen, skip_bytes);
        if (!iov) [[unlikely]] {
            // errno has been set
            return -1;
        }

        msghdr tmp_msg{
            .msg_name = raw_msg->msg_name,
            .msg_namelen = raw_msg->msg_namelen,
            .msg_iov = (iovec *)iov,
            .msg_iovlen = (size_t)iovcnt,
            .msg_control = raw_msg->msg_control,
            .msg_controllen = raw_msg->msg_controllen,
            .msg_flags = raw_msg->msg_flags,
        };

        ssize_t res = ::recvmsg(fd, &tmp_msg, flags);
        if (res >= 0) {
            // sync back to original msghdr
            raw_msg->msg_namelen = tmp_msg.msg_namelen;
            raw_msg->msg_controllen = tmp_msg.msg_controllen;
            raw_msg->msg_flags = tmp_msg.msg_flags;
        }
        return res;
    }
}

ssize_t z_net::sendmsg(int fd, const msghdr *raw_msg, int flags, size_t skip_bytes) noexcept {
    if (skip_bytes == 0) {
        return ::sendmsg(fd, raw_msg, flags);
    } else {
        constexpr int tmp_iovmax = 32;
        iovec tmp_iov[tmp_iovmax];

        auto [iov, iovcnt] = iov_slice(tmp_iov, tmp_iovmax, raw_msg->msg_iov, raw_msg->msg_iovlen, skip_bytes);
        if (!iov) [[unlikely]] {
            // errno has been set
            return -1;
        }

        msghdr tmp_msg{
            .msg_name = raw_msg->msg_name,
            .msg_namelen = raw_msg->msg_namelen,
            .msg_iov = (iovec *)iov,
            .msg_iovlen = (size_t)iovcnt,
            .msg_control = raw_msg->msg_control,
            .msg_controllen = raw_msg->msg_controllen,
            .msg_flags = raw_msg->msg_flags,
        };
        return ::sendmsg(fd, &tmp_msg, flags);
    }
}
