#pragma once
#include "z_task.hpp"
#include "libev/ev.h"
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct z_ev {
    static inline ev_loop *evloop = nullptr;

    static void init() noexcept {
        evloop = ev_default_loop(0);
    }

    static void run() noexcept {
        ev_run(evloop);
    }

    using io_callback_t = void (*)(ev_loop *, ev_io *, int revents) noexcept;
    using timer_callback_t = void (*)(ev_loop *, ev_timer *, int revents) noexcept;

    static void io_start(ev_io *io, z_Task *task, int events) noexcept {
        io_callback_t callback = [] (ev_loop *, ev_io *io, int) static noexcept {
            auto *task = static_cast<z_Task *>(io->data);
            task->resume();
        };

        ev_io_stop(evloop, io);

        ev_io_set(io, io->fd, events);
        ev_set_cb(io, callback);
        io->data = task;

        ev_io_start(evloop, io);
    }

    static void io_stop(ev_io *io) noexcept {
        ev_io_stop(evloop, io);
    }

    static void timer_start(ev_timer *timer, z_Task *task, double after_sec, double repeat_sec = 0.0) noexcept {
        timer_callback_t callback = [] (ev_loop *, ev_timer *timer, int) static noexcept {
            auto *task = static_cast<z_Task *>(timer->data);
            task->resume();
        };

        ev_timer_stop(evloop, timer);

        ev_timer_set(timer, after_sec, repeat_sec);
        ev_set_cb(timer, callback);
        timer->data = task;

        ev_timer_start(evloop, timer);
    }

    static void timer_stop(ev_timer *timer) noexcept {
        ev_timer_stop(evloop, timer);
    }
};

struct z_ev_read {
    z_leaf_fields();
    size_t n_read = 0;

    z_deinit(z_ev_read) {}

    z_function(ssize_t, ev_io *io, void *buf, size_t len, size_t at_least = 0) {
        z_begin();

        if (at_least > len) at_least = len;

        while (n_read == 0 || n_read < at_least) {
            ssize_t res; res = read(io->fd, (char *)buf + n_read, len - n_read);
            if (res > 0) {
                n_read += res;
            } else if (res == 0) {
                break;
            } else if (errno == EAGAIN) {
                z_ev::io_start(io, z_current(), EV_READ);
                z_yield(z_ev::io_stop(io));
            } else {
                z_return(res);
            }
        }

        z_return(n_read);
    }
};

struct z_ev_write {
    z_leaf_fields();
    size_t n_write = 0;

    z_deinit(z_ev_write) {}

    z_function(ssize_t, ev_io *io, const void *buf, size_t len) {
        z_begin();

        while (n_write < len) {
            ssize_t res; res = write(io->fd, (char *)buf + n_write, len - n_write);
            if (res >= 0) {
                n_write += res;
            } else if (errno == EAGAIN) {
                z_ev::io_start(io, z_current(), EV_WRITE);
                z_yield(z_ev::io_stop(io));
            } else {
                z_return(res);
            }
        }

        z_return(n_write);
    }
};

struct z_ev_accept {
    z_leaf_fields();

    z_deinit(z_ev_accept) {}

    z_function(int, ev_io *io, struct sockaddr *addr = nullptr, socklen_t *addrlen = nullptr, int flags = SOCK_CLOEXEC|SOCK_NONBLOCK) {
        z_begin();

        for (;;) {
            int cfd; cfd = accept4(io->fd, addr, addrlen, flags);
            if (cfd >= 0 || errno != EAGAIN) {
                z_return(cfd);
            } else {
                z_ev::io_start(io, z_current(), EV_READ);
                z_yield(z_ev::io_stop(io));
            }
        }
    }
};

struct z_ev_connect {
    z_leaf_fields();

    z_deinit(z_ev_connect) {}

    z_function(int, ev_io *io, const struct sockaddr *addr, socklen_t addrlen) {
        z_begin();

        int res; res = connect(io->fd, addr, addrlen);
        if (res == 0 || errno != EINPROGRESS) return res;

        z_ev::io_start(io, z_current(), EV_WRITE);
        z_yield(z_ev::io_stop(io));

        // check errno
        int err;
        socklen_t errlen = sizeof(err);
        if (getsockopt(io->fd, SOL_SOCKET, SO_ERROR, &err, &errlen) < 0)
            z_return(-1);
        if (err) {
            errno = err;
            z_return(-1);
        }
        z_return(0);
    }
};

struct z_ev_sleep {
    z_leaf_fields();

    z_deinit(z_ev_sleep) {}

    z_function(void, ev_timer *timer, double sleep_sec) {
        z_begin();

        z_ev::timer_start(timer, z_current(), sleep_sec);
        z_yield(z_ev::timer_stop(timer));

        z_ret();
    }
};
