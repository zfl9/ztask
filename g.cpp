#include "g.hpp"
#include "z_epoll.hpp"
#include "z_timer.hpp"

z_Epoll g::epoll{};
z_TimerMgr g::timer_mgr{0}; // todo: param now

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
