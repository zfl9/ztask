#include "z_fd.hpp"
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
                        n_read = -ESHUTDOWN;
                        goto out;
                    }
                    continue;
                case z_Waker::CANCEL:
                    fd->del_read_w(z_waiter());
                    n_read = -ECANCELED;
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            n_read = -errno;
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
                        n_write = -ESHUTDOWN;
                        goto out;
                    }
                    continue;
                case z_Waker::CANCEL:
                    fd->del_write_w(z_waiter());
                    n_write = -ECANCELED;
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            n_write = -errno;
            goto out;
        }
    }

    out:
    z_return(n_write);
}

z_function_def(z_Fd::z_accept, int, z_Fd *fd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    flags |= SOCK_NONBLOCK | SOCK_CLOEXEC;

    z_begin();

    int new_fd;
    for (;;) {
        new_fd = ::accept4(fd->raw_fd, addr, addrlen, flags);
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
                        new_fd = -ESHUTDOWN;
                        goto out;
                    }
                    continue;
                case z_Waker::CANCEL:
                    fd->del_read_w(z_waiter());
                    new_fd = -ECANCELED;
                    goto out;
                default:
                    std::unreachable();
            }
        } else {
            // error
            new_fd = -errno;
            goto out;
        }
    }

    out:
    z_return(new_fd);
}

z_function_def(z_Fd::z_connect, int, z_Fd *fd, const struct sockaddr *addr, socklen_t addrlen) {
    z_begin();

    int res; res = ::connect(fd->raw_fd, addr, addrlen);
    if (res == 0) goto out;

    if (errno != EINPROGRESS) {
        res = -errno;
        goto out;
    }

    fd->has_space = false;
    fd->add_write_w(z_waiter());
    z_yield();
    switch (z_waker()) {
        case z_Waker::RESOURCE: {
            assert(!z_waiter()->linked());
            if (fd->is_closed()) [[unlikely]] {
                res = -ESHUTDOWN;
                goto out;
            }
            int opt_err = ::getsockopt(fd->raw_fd, SOL_SOCKET, SO_ERROR, &res, (socklen_t *)&res);
            res = (opt_err == 0) ? -res : -errno;
            goto out;
        }
        case z_Waker::CANCEL:
            fd->del_write_w(z_waiter());
            res = -ECANCELED;
            goto out;
        default:
            std::unreachable();
    }

    out:
    z_return(res);
}
