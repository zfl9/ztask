#include "g.hpp"
#include <ctime>
#include "z_epoll.hpp"
#include "z_timer.hpp"

namespace {
    uint64_t real_now() noexcept {
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return ((uint64_t)t.tv_sec * 1000) + ((uint64_t)t.tv_nsec / 1000000);
    }
}

uint64_t g::now = real_now();
z_TimerMgr g::timer_mgr{g::now};
z_Epoll g::epoll{};

void g::update_now() noexcept {
    now = real_now();
}

void g::add_timer(z_Timer *timer) noexcept {
    timer_mgr.add_timer(timer);
}

void g::del_timer(z_Timer *timer) noexcept {
    timer_mgr.del_timer(timer);
}

void g::on_fd_dirty(z_Fd *fd) noexcept {
    epoll.on_fd_dirty(fd);
}

void g::on_fd_close(z_Fd *fd) noexcept {
    epoll.on_fd_close(fd);
}
