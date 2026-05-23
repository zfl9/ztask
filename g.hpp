#pragma once

// forward declarations to avoid circular dependencies
struct z_Fd;
struct z_Epoll;
struct z_Timer;
struct z_TimerMgr;

struct g {
    static z_Epoll epoll;
    static z_TimerMgr timer_mgr;

    static void add_timer(z_Timer *timer) noexcept;
    static void del_timer(z_Timer *timer) noexcept;

    static void on_fd_dirty(z_Fd *fd) noexcept;
    static void on_fd_close(z_Fd *fd) noexcept;
};
