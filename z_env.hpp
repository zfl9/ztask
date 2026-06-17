#pragma once
#include <stdint.h>

// forward declarations to avoid circular dependencies
struct z_Timer;
struct z_TimerMgr;
struct z_Fd;
struct z_Epoll;

// thread-local, initialized via `z_EnvInit`
struct z_env {
    z_env() = delete;
    ~z_env() = delete;

    // clock_monotonic in ms
    static uint64_t tick_time() noexcept;
    // clock_realtime in ms
    static uint64_t wall_time() noexcept;
    // clock_realtime: "2026-06-06 15:44:03.136"
    static const char *wall_timestr() noexcept;
    // update the tick_time, wall_time, wall_timestr
    static void time_update() noexcept;

    static z_TimerMgr *timer_mgr() noexcept;
    static z_Epoll *epoll() noexcept;

    static void add_timer(z_Timer *timer, uint64_t after_ms) noexcept;
    static void add_timer(z_Timer *timer) noexcept;
    static void del_timer(z_Timer *timer) noexcept;

    static void on_fd_dirty(z_Fd *fd) noexcept;
    static void on_fd_close(z_Fd *fd) noexcept;

    static void run() noexcept;
};

// place it at the beginning of `main()` and `thread_main()`
#define z_env_init() \
    [[maybe_unused]] z_EnvInit __z_env_init{}

struct z_EnvInit {
    z_EnvInit() noexcept;
    ~z_EnvInit() noexcept;
};
