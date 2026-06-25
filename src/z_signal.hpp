#pragma once
#include <signal.h>
#include <initializer_list>

struct z_signal {
    z_signal() = delete;
    ~z_signal() = delete;

    // return `old_handler`, or `SIG_ERR` if error
    static sighandler_t register_handler(int sig, sighandler_t handler, int flags = 0) noexcept;

    static void ignore_sigpipe() noexcept;

    static sigset_t make_sigset(std::initializer_list<int> signals) noexcept;

    static int block_signal(std::initializer_list<int> signals) noexcept;
    static int unblock_signal(std::initializer_list<int> signals) noexcept;

    // flags: SFD_NONBLOCK | SFD_CLOEXEC
    static int new_signal_fd(std::initializer_list<int> signals, int sig_fd = -1) noexcept;
};
