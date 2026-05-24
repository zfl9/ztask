#include "z_fd.hpp"
#include <unistd.h>
#include "g.hpp"

void z_Fd::close_fd() noexcept {
    if (raw_fd >= 0) {
        g::on_fd_close(this);
        ::close(raw_fd);
        raw_fd = -1;

        // wake up all waiters

    }
}

void z_Fd::add_read_w(z_Waiter *w) noexcept {
    assert(!has_data);
    read_wq.push_tail(w);
    g::on_fd_dirty(this);
}

void z_Fd::add_write_w(z_Waiter *w) noexcept {
    assert(!has_space);
    write_wq.push_tail(w);
    g::on_fd_dirty(this);
}

void z_Fd::del_read_w(z_Waiter *w) noexcept {
    w->unlink();
    g::on_fd_dirty(this);
}

void z_Fd::del_write_w(z_Waiter *w) noexcept {
    w->unlink();
    g::on_fd_dirty(this);
}

void z_Fd::on_readable() noexcept {
    ref();
    z_FdPayload payload{.fd = this, .readable = true};
    while (z_Waiter *w = read_wq.first()) {
        if (!has_data) break;
        w->callback(w, z_Waker::RESOURCE, &payload);
    }
    unref();
}

void z_Fd::on_writable() noexcept {
    ref();
    z_FdPayload payload{.fd = this, .writable = true};
    while (z_Waiter *w = write_wq.first()) {
        if (!has_space) break;
        w->callback(w, z_Waker::RESOURCE, &payload);
    }
    unref();
}

void z_Fd::on_error() noexcept {
    // todo: set error flag
    on_readable();
    on_writable();
    close_fd();
}

z_function_def(z_Fd::z_read, ssize_t, z_Fd *fd, void *buf, size_t len, size_t at_least) {
    z_begin();
    // todo
    (void)z_current();
    (void)fd;
    (void)buf;
    (void)len;
    (void)at_least;
    z_ret();
}

z_function_def(z_Fd::z_write, ssize_t, z_Fd *fd, const void *buf, size_t len) {
    z_begin();
    // todo
    (void)z_current();
    (void)fd;
    (void)buf;
    (void)len;
    z_ret();
}

z_function_def(z_Fd::z_accept, int, z_Fd *fd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    z_begin();
    // todo
    (void)z_current();
    (void)fd;
    (void)addr;
    (void)addrlen;
    (void)flags;
    z_ret();
}

z_function_def(z_Fd::z_connect, int, z_Fd *fd, const struct sockaddr *addr, socklen_t addrlen) {
    z_begin();
    // todo
    (void)z_current();
    (void)fd;
    (void)addr;
    (void)addrlen;
    z_ret();
}
