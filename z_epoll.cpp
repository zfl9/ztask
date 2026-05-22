#include "z_epoll.hpp"
#include <errno.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <signal.h>
#include "z_timer.hpp"
#include "g.hpp"
#include "log.h"

z_Epoll::z_Epoll() noexcept {
    ::signal(SIGPIPE, SIG_IGN);
    ep_fd = epoll_create1(EPOLL_CLOEXEC);
}

z_Epoll::~z_Epoll() noexcept {
    close(ep_fd);
}

void z_Epoll::run() noexcept {
    constexpr int max_events = 128;
    struct epoll_event events[max_events];

    for (;;) {
        flush_dirty_fds();
        int timeout = g::timer_mgr.epoll_timeout();
        int n_events = epoll_wait(ep_fd, events, max_events, timeout);

        // update time wheel
        // g::timer_mgr->update();

        if (n_events < 0) [[unlikely]] {
            if (errno == EINTR) continue;
            log_error("epoll_wait(fd:%d, timeout:%d): (%d) %m", ep_fd, timeout, errno);
            break;
        }

        // add ref
        for (int i = 0; i < n_events; ++i) {
            z_Fd *fd = (z_Fd *)events[i].data.ptr;
            fd->ref();
        }

        for (int i = 0; i < n_events; ++i) {
            z_Fd *fd = (z_Fd *)events[i].data.ptr;
            uint32_t ev = events[i].events;

            if (ev & (EPOLLIN | EPOLLHUP | EPOLLERR))
                fd->on_readable();

            if (ev & (EPOLLOUT | EPOLLHUP | EPOLLERR))
                fd->on_writable();

            // drop ref
            fd->unref();
        }
    }
}

void z_Epoll::on_fd_dirty(z_Fd *fd) noexcept {
    if (!fd->ep_node.linked())
        dirty_fds.push_tail(fd);
}

void z_Epoll::on_fd_close(z_Fd *fd) noexcept {
    fd->ep_node.unlink();
    if (fd->ep_events != 0) {
        epoll_ctl(ep_fd, EPOLL_CTL_DEL, fd->raw_fd, nullptr);
        fd->unref(); // drop ref (own by epoll instance)
    }
}

void z_Epoll::flush_dirty_fds() noexcept {
    while (z_Fd *fd = dirty_fds.pop_head()) {
        uint32_t want_events = 0;
        if (!fd->readers.is_empty()) want_events |= EPOLLIN;
        if (!fd->writers.is_empty()) want_events |= EPOLLOUT;

        if (want_events != fd->ep_events) {
            if (want_events == 0) {
                epoll_ctl(ep_fd, EPOLL_CTL_DEL, fd->raw_fd, nullptr);
                fd->unref(); // drop ref (own by epoll instance)
            } else {
                struct epoll_event ev{
                    .events = want_events | EPOLLET,
                    .data = { .ptr = fd },
                };
                int op = (fd->ep_events == 0) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
                epoll_ctl(ep_fd, op, fd->raw_fd, &ev);
                if (op == EPOLL_CTL_ADD) fd->ref(); // add ref (own by epoll instance)
            }
            fd->ep_events = want_events;
        }
    }
}
