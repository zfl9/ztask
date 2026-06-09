#include "z_fd.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include "z_env.hpp"

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
    read_wq.push_tail(w);
    z_env::on_fd_dirty(this);
}

void z_Fd::add_write_w(z_Waiter *w) noexcept {
    assert(!has_space && !is_closed());
    write_wq.push_tail(w);
    z_env::on_fd_dirty(this);
}

void z_Fd::del_read_w(z_Waiter *w) noexcept {
    w->unlink();
    z_env::on_fd_dirty(this);
}

void z_Fd::del_write_w(z_Waiter *w) noexcept {
    w->unlink();
    z_env::on_fd_dirty(this);
}

void z_Fd::on_event(bool ev_data, bool ev_space) noexcept {
    if (ev_data) has_data = true;
    if (ev_space) has_space = true;

    while (has_data || is_closed()) {
        auto *w = read_wq.pop_head();
        if (!w) break;
        w->callback(w, z_Waker::RESOURCE, this);
    }
    while (has_space || is_closed()) {
        auto *w = write_wq.pop_head();
        if (!w) break;
        w->callback(w, z_Waker::RESOURCE, this);
    }
}

z_function_def(z_Fd::z_read, ssize_t, z_Fd *fd, void *buf, size_t len, size_t at_least) {
    if (at_least > len)
        at_least = len;

    z_begin();

    while (n_read == 0 || (size_t)n_read < at_least) {
        ssize_t res; res = ::read(fd->raw_fd, (char *)buf + n_read, len - n_read);
        if (res > 0) {
            n_read += res;
        } else if (res == 0) {
            // EOF
            goto out;
        } else if (errno == EAGAIN) {
            fd->has_data = false;
            fd->add_read_w(z_waiter());
            z_yield();
            switch (z_waker()) {
                case z_Waker::RESOURCE:
                    assert(!z_waiter()->linked());
                    if (fd->is_closed()) [[unlikely]] {
                        errno = ESHUTDOWN;
                        n_read = -1;
                        goto out;
                    }
                    continue;
                case z_Waker::CANCEL:
                    fd->del_read_w(z_waiter());
                    errno = ECANCELED;
                    n_read = -1;
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            n_read = -1;
            goto out;
        }
    }

    out:
    z_return(n_read);
}

z_function_def(z_Fd::z_write, ssize_t, z_Fd *fd, const void *buf, size_t len) {
    z_begin();

    while ((size_t)n_write < len) {
        ssize_t res; res = ::write(fd->raw_fd, (const char *)buf + n_write, len - n_write);
        if (res >= 0) {
            n_write += res;
        } else if (errno == EAGAIN) {
            fd->has_space = false;
            fd->add_write_w(z_waiter());
            z_yield();
            switch (z_waker()) {
                case z_Waker::RESOURCE:
                    assert(!z_waiter()->linked());
                    if (fd->is_closed()) [[unlikely]] {
                        errno = ESHUTDOWN;
                        n_write = -1;
                        goto out;
                    }
                    continue;
                case z_Waker::CANCEL:
                    fd->del_write_w(z_waiter());
                    errno = ECANCELED;
                    n_write = -1;
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            n_write = -1;
            goto out;
        }
    }

    out:
    z_return(n_write);
}

z_function_def(z_Fd::z_accept, int, z_Fd *fd, z_net::Addr *addr) {
    z_begin();

    int new_fd;
    for (;;) {
        new_fd = z_net::accept(fd->raw_fd, addr);
        if (new_fd >= 0) {
            break;
        } else if (errno == EAGAIN) {
            fd->has_data = false;
            fd->add_read_w(z_waiter());
            z_yield();
            switch (z_waker()) {
                case z_Waker::RESOURCE:
                    assert(!z_waiter()->linked());
                    if (fd->is_closed()) [[unlikely]] {
                        errno = ESHUTDOWN;
                        new_fd = -1;
                        goto out;
                    }
                    continue;
                case z_Waker::CANCEL:
                    fd->del_read_w(z_waiter());
                    errno = ECANCELED;
                    new_fd = -1;
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            goto out;
        }
    }

    out:
    z_return(new_fd);
}

z_function_def(z_Fd::z_connect, int, z_Fd *fd, const z_net::Addr *addr) {
    z_begin();

    int res; res = z_net::connect(fd->raw_fd, addr);
    if (res == 0 || errno != EINPROGRESS) [[unlikely]]
        goto out;

    fd->has_space = false;
    fd->add_write_w(z_waiter());
    z_yield();
    switch (z_waker()) {
        case z_Waker::RESOURCE:
            assert(!z_waiter()->linked());
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
        case z_Waker::CANCEL:
            fd->del_write_w(z_waiter());
            errno = ECANCELED;
            res = -1;
            goto out;
        default:
            std::unreachable();
    }

    out:
    z_return(res);
}

z_function_def(z_Fd::z_recvfrom, ssize_t, z_Fd *fd, void *buf, size_t len, z_net::Addr *addr, int flags) {
    z_begin();

    ssize_t res;
    for (;;) {
        res = z_net::recvfrom(fd->raw_fd, buf, len, addr, flags);
        if (res >= 0) {
            break;
        } else if (errno == EAGAIN) {
            fd->has_data = false;
            fd->add_read_w(z_waiter());
            z_yield();
            switch (z_waker()) {
                case z_Waker::RESOURCE:
                    assert(!z_waiter()->linked());
                    if (fd->is_closed()) [[unlikely]] {
                        errno = ESHUTDOWN;
                        res = -1;
                        goto out;
                    }
                    continue;
                case z_Waker::CANCEL:
                    fd->del_read_w(z_waiter());
                    errno = ECANCELED;
                    res = -1;
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            goto out;
        }
    }

    out:
    z_return(res);
}

z_function_def(z_Fd::z_sendto, ssize_t, z_Fd *fd, const void *buf, size_t len, const z_net::Addr *addr, int flags) {
    z_begin();

    ssize_t res;
    for (;;) {
        res = z_net::sendto(fd->raw_fd, buf, len, addr, flags);
        if (res >= 0) {
            break;
        } else if (errno == EAGAIN) {
            fd->has_space = false;
            fd->add_write_w(z_waiter());
            z_yield();
            switch (z_waker()) {
                case z_Waker::RESOURCE:
                    assert(!z_waiter()->linked());
                    if (fd->is_closed()) [[unlikely]] {
                        errno = ESHUTDOWN;
                        res = -1;
                        goto out;
                    }
                    continue;
                case z_Waker::CANCEL:
                    fd->del_write_w(z_waiter());
                    errno = ECANCELED;
                    res = -1;
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            goto out;
        }
    }

    out:
    z_return(res);
}
