#pragma once
#include <cstdint>

// forward declarations to avoid circular dependencies
struct z_Timer;
struct z_TimerMgr;
struct z_Fd;
struct z_Epoll;

struct g {
    static uint64_t now; // cached time in ms (clock_monotonic)
    static z_TimerMgr timer_mgr;
    static z_Epoll epoll;

    static void update_now() noexcept;

    static void add_timer(z_Timer *timer, uint64_t after_ms) noexcept;
    static void add_timer(z_Timer *timer) noexcept;
    static void del_timer(z_Timer *timer) noexcept;

    static void on_fd_dirty(z_Fd *fd) noexcept;
    static void on_fd_close(z_Fd *fd) noexcept;
};
