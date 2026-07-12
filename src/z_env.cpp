#include "z_env.hpp"
#include <assert.h>
#include <string.h>
#include <time.h>
#include <new>
#include <array>
#include "z_util.hpp"
#include "z_timer.hpp"
#include "z_epoll.hpp"

// user setting: local-exec, initial-exec, local-dynamic, global-dynamic
#ifdef Z_TLS_MODEL
#define z_tls_model Z_STRINGIZE(Z_TLS_MODEL)
#else
#define z_tls_model "local-exec"
#endif

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

// z_env_local (thread_local)
namespace {
    struct z_EnvLocal {
        uint64_t tick_time = sys_tick_time();
        uint64_t wall_time = sys_wall_time();
        std::array<char, 24> wall_timestr{init_wall_timestr(wall_time)};
        z_TimerMgr timer_mgr{};
        z_Epoll epoll{};

        void time_update() noexcept {
            tick_time = sys_tick_time();
            wall_time = sys_wall_time();
            format_wall_timestr(wall_timestr.data(), wall_timestr.size(), wall_time);
        }
    };

    [[gnu::tls_model(z_tls_model)]] alignas(z_EnvLocal)
    __thread char z_env_local_storage[sizeof(z_EnvLocal)];

    #define z_env_local (reinterpret_cast<z_EnvLocal *>(z_env_local_storage))
}

// ================== z_env::Init (guard) ==================

z_env::Init::Init() noexcept {
    new (z_env_local_storage) z_EnvLocal{};
}

z_env::Init::~Init() noexcept {
    z_env_local->~z_EnvLocal();
}

// ================== z_env (thin wrapper) ==================

uint64_t z_env::tick_time() noexcept {
    return z_env_local->tick_time;
}

uint64_t z_env::wall_time() noexcept {
    return z_env_local->wall_time;
}

const char *z_env::wall_timestr() noexcept {
    return z_env_local->wall_timestr.data();
}

void z_env::time_update() noexcept {
    return z_env_local->time_update();
}

z_TimerMgr *z_env::timer_mgr() noexcept {
    return &z_env_local->timer_mgr;
}

z_Epoll *z_env::epoll() noexcept {
    return &z_env_local->epoll;
}

void z_env::add_timer(z_Timer *timer, uint64_t after_ms) noexcept {
    timer->expire = z_env_local->tick_time + after_ms;
    return z_env_local->timer_mgr.add_timer(timer);
}

void z_env::add_timer(z_Timer *timer) noexcept {
    return z_env_local->timer_mgr.add_timer(timer);
}

void z_env::del_timer(z_Timer *timer) noexcept {
    return z_env_local->timer_mgr.del_timer(timer);
}

void z_env::on_fd_dirty(z_Fd *fd) noexcept {
    return z_env_local->epoll.on_fd_dirty(fd);
}

void z_env::on_fd_close(z_Fd *fd) noexcept {
    return z_env_local->epoll.on_fd_close(fd);
}

void z_env::run() noexcept {
    return z_env_local->epoll.run();
}
