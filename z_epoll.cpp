#include "z_epoll.hpp"
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/epoll.h>
#include "z_log.hpp"
#include "z_timer.hpp"
#include "z_env.hpp"

void z_Epoll::run() noexcept {
    constexpr int max_events = 256;
    struct epoll_event events[max_events];

    for (;;) {
        flush_dirty_fds();
        int timeout = z_env::timer_mgr()->epoll_timeout();
        int n_events = epoll_wait(ep_fd, events, max_events, timeout);

        z_env::time_update();

        if (n_events < 0 && errno != EINTR) [[unlikely]] {
            z_log_error("epoll_wait(fd:%d, timeout:%d): (%d) %m", ep_fd, timeout, errno);
            break;
        }

        for (int i = 0; i < n_events; ++i) {
            z_Fd *fd = (z_Fd *)events[i].data.ptr;
            (void)fd->add_ref();
        }

        // must be placed after `add_ref()`
        z_env::timer_mgr()->update();

        for (int i = 0; i < n_events; ++i) {
            z_Fd *fd = (z_Fd *)events[i].data.ptr;
            uint32_t ev = events[i].events;

            // feed event to fd
            bool ev_data = ev & (EPOLLIN | EPOLLHUP | EPOLLERR);
            bool ev_space = ev & (EPOLLOUT | EPOLLHUP | EPOLLERR);
            fd->on_event(ev_data, ev_space);

            fd->drop_ref();
        }
    }
}

void z_Epoll::on_fd_dirty(z_Fd *fd) noexcept {
    if (!fd->ep_node.linked())
        dirty_fds.push_tail(fd);
}

void z_Epoll::on_fd_close(z_Fd *fd) noexcept {
    fd->ep_node.unlink();
    if (fd->ep_events != 0)
        ep_del(fd);
}

void z_Epoll::flush_dirty_fds() noexcept {
    while (z_Fd *fd = dirty_fds.pop_head()) {
        uint32_t want_events = fd->want_events();
        if (want_events != fd->ep_events) {
            if (want_events != 0)
                ep_add(fd, want_events);
            else
                ep_del(fd);
        }
    }
}

void z_Epoll::ep_add(z_Fd *fd, uint32_t events) noexcept {
    struct epoll_event ev{
        .events = events | EPOLLET,
        .data = { .ptr = fd },
    };
    int op = (fd->ep_events == 0) ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    fd->ep_events = events;
    if (op == EPOLL_CTL_ADD)
        (void)fd->add_ref(); // held by epoll instance

    int res = epoll_ctl(ep_fd, op, fd->raw_fd, &ev);
    assert(res == 0); (void)res;
}

void z_Epoll::ep_del(z_Fd *fd) noexcept {
    int res = epoll_ctl(ep_fd, EPOLL_CTL_DEL, fd->raw_fd, nullptr);
    assert(res == 0); (void)res;

    fd->ep_events = 0;
    fd->drop_ref(); // held by epoll instance
}
