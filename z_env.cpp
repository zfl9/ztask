#include "z_env.hpp"
#include <array>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <csignal>
#include <cassert>
#include "z_util.hpp"
#include "z_epoll.hpp"
#include "z_timer.hpp"

// user setting: local-exec, initial-exec, local-dynamic, global-dynamic
#ifdef Z_TLS_MODEL
#define z_tls_model Z_STRINGIZE(Z_TLS_MODEL)
#else
#define z_tls_model "local-exec"
#endif

#define z_attr_tls_model __attribute__((tls_model(z_tls_model)))

// internal helper
namespace {
    uint64_t sys_tick_time() noexcept {
        struct timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        return ((uint64_t)t.tv_sec * 1000) + ((uint64_t)t.tv_nsec / 1000000);
    }

    uint64_t sys_wall_time() noexcept {
        struct timespec t;
        clock_gettime(CLOCK_REALTIME, &t);
        return ((uint64_t)t.tv_sec * 1000) + ((uint64_t)t.tv_nsec / 1000000);
    }

    // 00 ~ 99
    alignas(2) constexpr char digit_pairs[] =
        "00" "01" "02" "03" "04" "05" "06" "07" "08" "09"
        "10" "11" "12" "13" "14" "15" "16" "17" "18" "19"
        "20" "21" "22" "23" "24" "25" "26" "27" "28" "29"
        "30" "31" "32" "33" "34" "35" "36" "37" "38" "39"
        "40" "41" "42" "43" "44" "45" "46" "47" "48" "49"
        "50" "51" "52" "53" "54" "55" "56" "57" "58" "59"
        "60" "61" "62" "63" "64" "65" "66" "67" "68" "69"
        "70" "71" "72" "73" "74" "75" "76" "77" "78" "79"
        "80" "81" "82" "83" "84" "85" "86" "87" "88" "89"
        "90" "91" "92" "93" "94" "95" "96" "97" "98" "99";

    void write_digit_pair(char *dst, unsigned num) noexcept {
        memcpy(dst, &digit_pairs[num * 2], 2);
    }

    void format_wall_timestr(char *buf, size_t bufsz, uint64_t wall_time) noexcept {
        time_t sec = (time_t)(wall_time / 1000); // seconds
        unsigned ms = (unsigned)(wall_time % 1000); // milliseconds

        struct tm tm;
        localtime_r(&sec, &tm);

        assert(bufsz >= 24);
        (void)bufsz;

        unsigned t_year = tm.tm_year + 1900;
        unsigned t_mon = tm.tm_mon + 1;
        unsigned t_mday = tm.tm_mday;
        unsigned t_hour = tm.tm_hour;
        unsigned t_min = tm.tm_min;
        unsigned t_sec = tm.tm_sec;

        write_digit_pair(buf, t_year / 100);
        write_digit_pair(buf + 2, t_year % 100);
        buf[4] = '-';
        write_digit_pair(buf + 5, t_mon);
        buf[7] = '-';
        write_digit_pair(buf + 8, t_mday);
        buf[10] = ' ';
        write_digit_pair(buf + 11, t_hour);
        buf[13] = ':';
        write_digit_pair(buf + 14, t_min);
        buf[16] = ':';
        write_digit_pair(buf + 17, t_sec);
        buf[19] = '.';
        write_digit_pair(buf + 20, ms / 10);
        buf[22] = '0' + (ms % 10);
        buf[23] = '\0';
    }

    std::array<char, 24> init_wall_timestr(uint64_t wall_time) noexcept {
        std::array<char, 24> buf{};
        format_wall_timestr(buf.data(), buf.size(), wall_time);
        return buf;
    }
}

// z_env_impl (thread_local)
namespace {
    struct z_EnvImpl {
        uint64_t tick_time = 0;
        uint64_t wall_time = 0;
        std::array<char, 24> wall_timestr{};
        z_TimerMgr timer_mgr{};
        z_Epoll epoll{};

        z_EnvImpl() noexcept :
            tick_time{sys_tick_time()},
            wall_time{sys_wall_time()},
            wall_timestr{init_wall_timestr(wall_time)}
        {}

        ~z_EnvImpl() noexcept = default;

        void time_update() noexcept {
            tick_time = sys_tick_time();
            wall_time = sys_wall_time();
            format_wall_timestr(wall_timestr.data(), wall_timestr.size(), wall_time);
        }

        void add_timer(z_Timer *timer, uint64_t after_ms) noexcept {
            timer->expire = tick_time + after_ms;
            timer_mgr.add_timer(timer);
        }

        void add_timer(z_Timer *timer) noexcept {
            timer_mgr.add_timer(timer);
        }

        void del_timer(z_Timer *timer) noexcept {
            timer_mgr.del_timer(timer);
        }

        void on_fd_dirty(z_Fd *fd) noexcept {
            epoll.on_fd_dirty(fd);
        }

        void on_fd_close(z_Fd *fd) noexcept {
            epoll.on_fd_close(fd);
        }
    };

    alignas(z_EnvImpl) __thread char z_env_impl_storage[sizeof(z_EnvImpl)] z_attr_tls_model;

    #define z_env_impl (reinterpret_cast<z_EnvImpl *>(z_env_impl_storage))
}

// ================== z_EnvInit (guard) ==================

z_EnvInit::z_EnvInit() noexcept {
    signal(SIGPIPE, SIG_IGN);
    new (z_env_impl_storage) z_EnvImpl{};
}

z_EnvInit::~z_EnvInit() noexcept {
    z_env_impl->~z_EnvImpl();
}

// ================== z_env (thin wrapper) ==================

uint64_t z_env::tick_time() noexcept {
    return z_env_impl->tick_time;
}

uint64_t z_env::wall_time() noexcept {
    return z_env_impl->wall_time;
}

const char *z_env::wall_timestr() noexcept {
    return z_env_impl->wall_timestr.data();
}

void z_env::time_update() noexcept {
    return z_env_impl->time_update();
}

z_TimerMgr *z_env::timer_mgr() noexcept {
    return &z_env_impl->timer_mgr;
}

z_Epoll *z_env::epoll() noexcept {
    return &z_env_impl->epoll;
}

void z_env::add_timer(z_Timer *timer, uint64_t after_ms) noexcept {
    return z_env_impl->add_timer(timer, after_ms);
}

void z_env::add_timer(z_Timer *timer) noexcept {
    return z_env_impl->add_timer(timer);
}

void z_env::del_timer(z_Timer *timer) noexcept {
    return z_env_impl->del_timer(timer);
}

void z_env::on_fd_dirty(z_Fd *fd) noexcept {
    return z_env_impl->on_fd_dirty(fd);
}

void z_env::on_fd_close(z_Fd *fd) noexcept {
    return z_env_impl->on_fd_close(fd);
}
