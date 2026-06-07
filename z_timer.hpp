#pragma once
#include <cstdint>
#include "z_list.hpp"
#include "z_env.hpp"

struct z_Timer {
    using Callback = void (*)(z_Timer *timer) noexcept;

    z_Node node{};
    Callback callback = nullptr;
    uint64_t expire:54 = 0; // clock_monotonic in ms
    uint64_t level:2 = 0;
    uint64_t index:8 = 0;

    explicit z_Timer(Callback callback, uint64_t expire) noexcept
        : callback{callback}, expire{expire} {}

    ~z_Timer() noexcept = default;
};

using z_TimerList = z_List<z_Timer, &z_Timer::node>;

struct z_TimerMgr {
private:
    z_TimerList level_0[256]{};
    z_TimerList level_1[256]{};
    z_TimerList level_2[64]{};
    z_TimerList level_3[64]{};

    uint64_t bitset_0[4]{}; // 256 bits
    uint64_t bitset_1[4]{}; // 256 bits
    uint64_t bitset_2{};    // 64 bits
    uint64_t bitset_3{};    // 64 bits

    uint64_t current{}; // clock_monotonic in ms

public:
    z_TimerMgr() noexcept : current{z_env::tick_time()} {}

    ~z_TimerMgr() noexcept = default;

    void add_timer(z_Timer *timer) noexcept;

    void del_timer(z_Timer *timer) noexcept;

    // distance from `current` to first-timer, or UINT64_MAX if no timer
    uint64_t distance() const noexcept;

    // if no timer is pending, return UINT64_MAX
    uint64_t timeout() const noexcept;

    // timeout for epoll_wait, capped to INT_MAX
    int epoll_timeout() const noexcept;

    // advance current time to now && trigger timers
    void update() noexcept;

private:
    void do_add_timer(z_Timer *timer) noexcept;

    void link_timer(unsigned level, unsigned index, z_Timer *timer) noexcept;

    void discharge(unsigned level) noexcept;
};
