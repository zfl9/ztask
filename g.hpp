#pragma once
#include <cstdint>

// forward declarations to avoid circular dependencies
struct z_Timer;
struct z_TimerMgr;
struct z_Fd;
struct z_Epoll;

struct G {
    uint64_t tick_time; // clock_monotonic in ms
    uint64_t wall_time; // clock_realtime in ms
    char wall_timestr[24]; // "2026-06-06 15:44:03.136"

    G() noexcept;
    ~G() noexcept = default;

    // update the `tick_time`, `wall_time*`
    void time_update() noexcept;

    static z_TimerMgr *timer_mgr() noexcept;
    static z_Epoll *epoll() noexcept;

    static void add_timer(z_Timer *timer, uint64_t after_ms) noexcept;
    static void add_timer(z_Timer *timer) noexcept;
    static void del_timer(z_Timer *timer) noexcept;

    static void on_fd_dirty(z_Fd *fd) noexcept;
    static void on_fd_close(z_Fd *fd) noexcept;
};

extern G g;
