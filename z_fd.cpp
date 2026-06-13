#include "z_fd.hpp"
#include <asm-generic/errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include "z_env.hpp"

static_assert(EAGAIN == EWOULDBLOCK);

void z_Fd::close() noexcept {
    if (raw_fd >= 0) {
        z_env::on_fd_close(this);

        int fd = raw_fd;
        raw_fd = -1;

        // wake up all waiters
        on_event(false, false);
        assert(read_wq.is_empty());
        assert(write_wq.is_empty());

        ::close(fd);
    }
}

void z_Fd::add_read_w(z_Waiter *w) noexcept {
    assert(!has_data && !is_closed());
    bool pre_empty = read_wq.is_empty();
    read_wq.push_tail(w);
    if (pre_empty && !in_event_fn) z_env::on_fd_dirty(this);
}

void z_Fd::add_write_w(z_Waiter *w) noexcept {
    assert(!has_space && !is_closed());
    bool pre_empty = write_wq.is_empty();
    write_wq.push_tail(w);
    if (pre_empty && !in_event_fn) z_env::on_fd_dirty(this);
}

void z_Fd::del_read_w(z_Waiter *w) noexcept {
    w->unlink(); // pop from read_wq
    sync_dirty_state();
}

void z_Fd::del_write_w(z_Waiter *w) noexcept {
    w->unlink(); // pop from write_wq
    sync_dirty_state();
}

uint32_t z_Fd::want_events() const noexcept {
    uint32_t want_events = 0;
    if (!read_wq.is_empty()) want_events |= EPOLLIN;
    if (!write_wq.is_empty()) want_events |= EPOLLOUT;
    return want_events;
}

void z_Fd::sync_dirty_state() noexcept {
    if (is_closed() || in_event_fn) return;

    if (want_events() != ep_events)
        z_env::on_fd_dirty(this);
    else
        ep_node.unlink();
}

void z_Fd::on_event(bool ev_data, bool ev_space) noexcept {
    if (ev_data) has_data = true;
    if (ev_space) has_space = true;

    ++in_event_fn;
    while (has_data || is_closed()) {
        auto *w = read_wq.pop_head();
        if (!w) break;
        w->callback(w, this);
    }
    while (has_space || is_closed()) {
        auto *w = write_wq.pop_head();
        if (!w) break;
        w->callback(w, this);
    }
    --in_event_fn;

    sync_dirty_state();
}

namespace {
    void on_error(auto *__restrict transferred, int set_errno = 0) noexcept {
        if (*transferred == 0) {
            // no progress has been made
            if (set_errno) errno = set_errno;
            *transferred = -1;
        }
    }
}

z_function_def(z_Fd::z_read, ssize_t, z_Fd *fd, void *buf, size_t len, Opt opt) {
    iovec iov{
        .iov_base = buf,
        .iov_len = len,
    };
    return z_function_call(fd, &iov, 1, opt);
}

z_function_def(z_Fd::z_read, ssize_t, z_Fd *fd, const iovec *iov, int iovcnt, Opt opt) {
    // parameter preprocessing
    size_t buf_len = 0;
    for (int i = 0; i < iovcnt; ++i)
        buf_len += iov[i].iov_len;

    if (opt.at_least < 1) opt.at_least = 1;
    if (opt.at_least > buf_len) opt.at_least = buf_len;

    // enter the state machine
    z_begin();
    z_timer_arm(opt.timeout);

    while ((size_t)n_read < opt.at_least) {
        ssize_t res; res = z_net::readv(fd->raw_fd, iov, iovcnt, n_read);
        if (res > 0) {
            n_read += res;
        } else if (res == 0) {
            // EOF
            goto out;
        } else if (errno == EAGAIN) {
            fd->has_data = false;
            fd->add_read_w(z_waiter());
            z_yield();
            switch (z_event()) {
                case z_Event::WAITER:
                    if (fd->is_closed()) [[unlikely]] {
                        on_error(&n_read, ESHUTDOWN);
                        goto out;
                    }
                    continue;
                case z_Event::TIMER:
                case z_Event::CANCEL:
                    fd->del_read_w(z_waiter());
                    on_error(&n_read, (z_event() == z_Event::TIMER) ? ETIMEDOUT : ECANCELED);
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            on_error(&n_read);
            goto out;
        }
    }

    out:
    z_timer_disarm();
    z_return(n_read);
}

z_function_def(z_Fd::z_write, ssize_t, z_Fd *fd, const void *buf, size_t len, Opt opt) {
    iovec iov{
        .iov_base = (void *)buf,
        .iov_len = len,
    };
    return z_function_call(fd, &iov, 1, opt);
}

z_function_def(z_Fd::z_write, ssize_t, z_Fd *fd, const iovec *iov, int iovcnt, Opt opt) {
    size_t data_len = 0;
    for (int i = 0; i < iovcnt; ++i)
        data_len += iov[i].iov_len;

    z_begin();
    z_timer_arm(opt.timeout);

    while ((size_t)n_write < data_len) {
        ssize_t res; res = z_net::writev(fd->raw_fd, iov, iovcnt, n_write);
        if (res > 0) {
            n_write += res;
        } else if (res == 0) {
            // avoid infinite loop
            goto out;
        } else if (errno == EAGAIN) {
            fd->has_space = false;
            fd->add_write_w(z_waiter());
            z_yield();
            switch (z_event()) {
                case z_Event::WAITER:
                    if (fd->is_closed()) [[unlikely]] {
                        on_error(&n_write, ESHUTDOWN);
                        goto out;
                    }
                    continue;
                case z_Event::TIMER:
                case z_Event::CANCEL:
                    fd->del_write_w(z_waiter());
                    on_error(&n_write, (z_event() == z_Event::TIMER) ? ETIMEDOUT : ECANCELED);
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            on_error(&n_write);
            goto out;
        }
    }

    out:
    z_timer_disarm();
    z_return(n_write);
}

z_function_def(z_Fd::z_recv, ssize_t, z_Fd *fd, void *buf, size_t len, Opt opt) {
    iovec iov{
        .iov_base = buf,
        .iov_len = len,
    };
    msghdr msg{
        .msg_name = opt.addr ? &opt.addr->sa : nullptr,
        .msg_namelen = (socklen_t)(opt.addr ? sizeof(*opt.addr) : 0),
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = nullptr,
        .msg_controllen = 0,
        .msg_flags = 0,
    };
    return z_function_call(fd, &msg, opt);
}

z_function_def(z_Fd::z_recv, ssize_t, z_Fd *fd, msghdr *msg, Opt opt) {
    // parameter preprocessing
    size_t buf_len = 0;
    for (int i = 0; i < (int)msg->msg_iovlen; ++i)
        buf_len += msg->msg_iov[i].iov_len;

    if (opt.at_least < 1) opt.at_least = 1;
    if (opt.at_least > buf_len) opt.at_least = buf_len;

    // enter the state machine
    z_begin();
    z_timer_arm(opt.timeout);

    while ((size_t)n_read < opt.at_least || buf_len == 0) {
        ssize_t res; res = z_net::recvmsg(fd->raw_fd, msg, opt.flags, n_read);
        if (res > 0) {
            n_read += res;
        } else if (res == 0) {
            // EOF
            goto out;
        } else if (errno == EAGAIN) {
            fd->has_data = false;
            fd->add_read_w(z_waiter());
            z_yield();
            switch (z_event()) {
                case z_Event::WAITER:
                    if (fd->is_closed()) [[unlikely]] {
                        on_error(&n_read, ESHUTDOWN);
                        goto out;
                    }
                    continue;
                case z_Event::TIMER:
                case z_Event::CANCEL:
                    fd->del_read_w(z_waiter());
                    on_error(&n_read, (z_event() == z_Event::TIMER) ? ETIMEDOUT : ECANCELED);
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            on_error(&n_read);
            goto out;
        }
    }

    out:
    z_timer_disarm();
    z_return(n_read);
}

z_function_def(z_Fd::z_send, ssize_t, z_Fd *fd, const void *buf, size_t len, Opt opt) {
    iovec iov{
        .iov_base = (void *)buf,
        .iov_len = len,
    };
    msghdr msg{
        .msg_name = (void *)(opt.addr ? &opt.addr->sa : nullptr),
        .msg_namelen = opt.addr ? opt.addr->len() : 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = nullptr,
        .msg_controllen = 0,
        .msg_flags = 0,
    };
    return z_function_call(fd, &msg, opt);
}

z_function_def(z_Fd::z_send, ssize_t, z_Fd *fd, const msghdr *msg, Opt opt) {
    size_t data_len = 0;
    for (int i = 0; i < (int)msg->msg_iovlen; ++i)
        data_len += msg->msg_iov[i].iov_len;

    z_begin();
    z_timer_arm(opt.timeout);

    while ((size_t)n_write < data_len || data_len == 0) {
        ssize_t res; res = z_net::sendmsg(fd->raw_fd, msg, opt.flags, n_write);
        if (res > 0) {
            n_write += res;
        } else if (res == 0) {
            // avoid infinite loop
            goto out;
        } else if (errno == EAGAIN) {
            fd->has_space = false;
            fd->add_write_w(z_waiter());
            z_yield();
            switch (z_event()) {
                case z_Event::WAITER:
                    if (fd->is_closed()) [[unlikely]] {
                        on_error(&n_write, ESHUTDOWN);
                        goto out;
                    }
                    continue;
                case z_Event::TIMER:
                case z_Event::CANCEL:
                    fd->del_write_w(z_waiter());
                    on_error(&n_write, (z_event() == z_Event::TIMER) ? ETIMEDOUT : ECANCELED);
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            on_error(&n_write);
            goto out;
        }
    }

    out:
    z_timer_disarm();
    z_return(n_write);
}

z_function_def(z_Fd::z_recvmmsg, int, z_Fd *fd, mmsghdr *msgv, unsigned vlen, Opt opt) {
    z_begin();
    z_timer_arm(opt.timeout);

    int n_msg;
    for (;;) {
        n_msg = ::recvmmsg(fd->raw_fd, msgv, vlen, opt.flags, nullptr);
        if (n_msg >= 0 || errno != EAGAIN) {
            goto out;
        } else {
            fd->has_data = false;
            fd->add_read_w(z_waiter());
            z_yield();
            switch (z_event()) {
                case z_Event::WAITER:
                    if (fd->is_closed()) [[unlikely]] {
                        errno = ESHUTDOWN;
                        n_msg = -1;
                        goto out;
                    }
                    continue;
                case z_Event::TIMER:
                case z_Event::CANCEL:
                    fd->del_read_w(z_waiter());
                    errno = (z_event() == z_Event::TIMER) ? ETIMEDOUT : ECANCELED;
                    n_msg = -1;
                    goto out;
                default:
                    std::unreachable();
            }
        }
    }

    out:
    z_timer_disarm();
    z_return(n_msg);
}

z_function_def(z_Fd::z_sendmmsg, int, z_Fd *fd, mmsghdr *msgv, unsigned vlen, Opt opt) {
    z_begin();
    z_timer_arm(opt.timeout);

    while ((unsigned)n_sent < vlen) {
        int res; res = ::sendmmsg(fd->raw_fd, msgv + n_sent, vlen - n_sent, opt.flags);
        if (res > 0) {
            n_sent += res;
        } else if (res == 0) {
            // avoid infinite loop
            goto out;
        } else if (errno == EAGAIN) {
            fd->has_space = false;
            fd->add_write_w(z_waiter());
            z_yield();
            switch (z_event()) {
                case z_Event::WAITER:
                    if (fd->is_closed()) [[unlikely]] {
                        on_error(&n_sent, ESHUTDOWN);
                        goto out;
                    }
                    continue;
                case z_Event::TIMER:
                case z_Event::CANCEL:
                    fd->del_write_w(z_waiter());
                    on_error(&n_sent, (z_event() == z_Event::TIMER) ? ETIMEDOUT : ECANCELED);
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            on_error(&n_sent);
            goto out;
        }
    }

    out:
    z_timer_disarm();
    z_return(n_sent);
}

z_function_def(z_Fd::z_accept, int, z_Fd *fd, z_net::Addr *addr, Opt opt) {
    z_begin();
    z_timer_arm(opt.timeout);

    int new_fd;
    for (;;) {
        new_fd = z_net::accept(fd->raw_fd, addr);
        if (new_fd >= 0 || errno != EAGAIN) {
            goto out;
        } else {
            fd->has_data = false;
            fd->add_read_w(z_waiter());
            z_yield();
            switch (z_event()) {
                case z_Event::WAITER:
                    if (fd->is_closed()) [[unlikely]] {
                        errno = ESHUTDOWN;
                        new_fd = -1;
                        goto out;
                    }
                    continue;
                case z_Event::TIMER:
                case z_Event::CANCEL:
                    fd->del_read_w(z_waiter());
                    errno = (z_event() == z_Event::TIMER) ? ETIMEDOUT : ECANCELED;
                    new_fd = -1;
                    goto out;
                default:
                    std::unreachable();
            }
        }
    }

    out:
    z_timer_disarm();
    z_return(new_fd);
}

z_function_def(z_Fd::z_connect, int, z_Fd *fd, const z_net::Addr *addr, Opt opt) {
    z_begin();
    z_timer_arm(opt.timeout);

    int res; res = z_net::connect(fd->raw_fd, addr);
    if (res == 0 || errno != EINPROGRESS)
        goto out;

    fd->has_space = false;
    fd->add_write_w(z_waiter());
    z_yield();
    switch (z_event()) {
        case z_Event::WAITER:
            if (fd->is_closed()) [[unlikely]] {
                errno = ESHUTDOWN;
                res = -1;
            } else if (!z_net::getsockopt_int(fd->raw_fd, SOL_SOCKET, SO_ERROR, &res)) [[unlikely]] {
                // getsockopt fail
                res = -1;
            } else if (res != 0) [[unlikely]] {
                // socket error
                errno = res;
                res = -1;
            } else {
                // connect succ
                res = 0;
            }
            goto out;
        case z_Event::TIMER:
        case z_Event::CANCEL:
            fd->del_write_w(z_waiter());
            errno = (z_event() == z_Event::TIMER) ? ETIMEDOUT : ECANCELED;
            res = -1;
            goto out;
        default:
            std::unreachable();
    }

    out:
    z_timer_disarm();
    z_return(res);
}

z_function_def(z_Fd::z_forward, int, z_Fd *a_fd, z_Fd *b_fd, int a_pipe, int b_pipe, Opt opt) {
    z_begin();

    task = z_current();
    int res;

    #define forward_a2b() do { \
        if (!do_forward(z_waiter(), a_fd, b_fd, a_pipe, a_len, a_shutdown, opt.flags)) [[unlikely]] { \
            /* errno has been set */ \
            res = -1; \
            goto out; \
        } \
    } while (0)

    #define forward_b2a() do { \
        if (!do_forward(&waiter, b_fd, a_fd, b_pipe, b_len, b_shutdown, opt.flags)) [[unlikely]] { \
            /* errno has been set */ \
            res = -1; \
            goto out; \
        } \
    } while (0)

    forward_a2b();
    forward_b2a();

    while (!a_shutdown || !b_shutdown) {
        // restart timer
        if (!a_shutdown && !b_shutdown) {
            // transmission in progress
            if (opt.idle_timeout > 0)
                z_timer_restart(opt.idle_timeout);
        } else {
            // half-closed state
            if (opt.half_timeout > 0 && !half_started) {
                half_started = true;
                z_timer_restart(opt.half_timeout);
            }
        }

        // wait for events
        z_yield();
        switch (z_event()) {
            case z_Event::WAITER: {
                auto *ctx = z_event_ctx();
                if (ctx->u.waiter == z_waiter())
                    forward_a2b();
                else
                    forward_b2a();
                continue;
            }
            case z_Event::TIMER:
            case z_Event::CANCEL:
                errno = (z_event() == z_Event::TIMER) ? ETIMEDOUT : ECANCELED;
                res = -1;
                goto out;
            default:
                std::unreachable();
        }
    }
    assert(a_shutdown && b_shutdown);
    res = 0;

    #undef forward_a2b
    #undef forward_b2a

    out:
    // a->b
    a_fd->del_read_w(z_waiter());
    b_fd->del_write_w(z_waiter());

    // b->a
    b_fd->del_read_w(&waiter);
    a_fd->del_write_w(&waiter);

    z_timer_disarm();
    z_return(res);
}

bool z_Fd::z_forward::do_forward(
    z_Waiter *w, z_Fd *in, z_Fd *out,
    int pipe, size_t &len, bool &shutdown,
    unsigned flags) noexcept
{
    constexpr size_t splice_sz = 1048576; // 1MB

    for (;;) {
        // in_fd -> pipe
        if (len == 0) {
            ssize_t n = splice(in->raw_fd, nullptr, pipe, nullptr, splice_sz, flags);
            if (n > 0) {
                len = n;
            } else if (n == 0) {
                // EOF
                out->shutdown(SHUT_WR);
                shutdown = true;
                return true;
            } else if (errno == EAGAIN) {
                in->has_data = false;
                in->add_read_w(w);
                return true;
            } else {
                // error
                return false;
            }
        }

        // pipe -> out_fd
        while (len > 0) {
            ssize_t n = splice(pipe, nullptr, out->raw_fd, nullptr, len, flags);
            if (n > 0) {
                len -= (size_t)n;
            } else if (n == 0) {
                // avoid infinite loop
                errno = EDEADLOCK;
                return false;
            } else if (errno == EAGAIN) {
                out->has_space = false;
                out->add_write_w(w);
                return true;
            } else {
                // error
                return false;
            }
        }
    }

    std::unreachable();
}
