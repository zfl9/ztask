#include "z_fd.hpp"
#include <unistd.h>
#include "g.hpp"

void z_Fd::close_fd() noexcept {
    if (raw_fd >= 0) {
        g::on_fd_close(this);
        ::close(raw_fd);
        raw_fd = -1;
    }
}

void z_Fd::add_reader(z_Task *task) noexcept {
    readers.push_tail(task);
    g::on_fd_dirty(this);
}

void z_Fd::add_writer(z_Task *task) noexcept {
    writers.push_tail(task);
    g::on_fd_dirty(this);
}

void z_Fd::del_reader(z_Task *task) noexcept {
    task->wait_node.unlink();
    g::on_fd_dirty(this);
}

void z_Fd::del_writer(z_Task *task) noexcept {
    task->wait_node.unlink();
    g::on_fd_dirty(this);
}

void z_Fd::on_readable() noexcept {
    while (z_Task *task = readers.first()) {
        if (!has_data) break;
        task->resume();
    }
}

void z_Fd::on_writable() noexcept {
    while (z_Task *task = writers.first()) {
        if (!has_space) break;
        task->resume();
    }
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
    (void)fd;
    (void)buf;
    (void)len;
    (void)at_least;
    z_ret();
}

z_function_def(z_Fd::z_write, ssize_t, z_Fd *fd, const void *buf, size_t len) {
    z_begin();
    // todo
    (void)fd;
    (void)buf;
    (void)len;
    z_ret();
}

z_function_def(z_Fd::z_accept, int, z_Fd *fd, struct sockaddr *addr, socklen_t *addrlen, int flags) {
    z_begin();
    // todo
    (void)fd;
    (void)addr;
    (void)addrlen;
    (void)flags;
    z_ret();
}

z_function_def(z_Fd::z_connect, int, z_Fd *fd, const struct sockaddr *addr, socklen_t addrlen) {
    z_begin();
    // todo
    (void)fd;
    (void)addr;
    (void)addrlen;
    z_ret();
}
