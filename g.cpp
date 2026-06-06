#include "g.hpp"
#include <cstdio>
#include <ctime>
#include <cstring>
#include <csignal>
#include "z_epoll.hpp"
#include "z_timer.hpp"

// must be placed before `g_ext`
G g{};

namespace {
    // must be placed after `g`
    struct {
        z_TimerMgr timer_mgr{};
        z_Epoll epoll{};
    } g_ext{};

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

    void make_wall_timestr(char *buf, size_t bufsz, uint64_t wall_time) noexcept {
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
}

G::G() noexcept {
    time_update();
    signal(SIGPIPE, SIG_IGN);
}

void G::time_update() noexcept {
    tick_time = sys_tick_time();
    wall_time = sys_wall_time();
    make_wall_timestr(wall_timestr, sizeof(wall_timestr), wall_time);
}

z_TimerMgr *G::timer_mgr() noexcept {
    return &g_ext.timer_mgr;
}

z_Epoll *G::epoll() noexcept {
    return &g_ext.epoll;
}

void G::add_timer(z_Timer *timer, uint64_t after_ms) noexcept {
    timer->expire = g.tick_time + after_ms;
    g_ext.timer_mgr.add_timer(timer);
}

void G::add_timer(z_Timer *timer) noexcept {
    g_ext.timer_mgr.add_timer(timer);
}

void G::del_timer(z_Timer *timer) noexcept {
    g_ext.timer_mgr.del_timer(timer);
}

void G::on_fd_dirty(z_Fd *fd) noexcept {
    g_ext.epoll.on_fd_dirty(fd);
}

void G::on_fd_close(z_Fd *fd) noexcept {
    g_ext.epoll.on_fd_close(fd);
}
