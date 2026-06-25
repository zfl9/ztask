#include "z_signal.hpp"
#include <sys/signalfd.h>

sighandler_t z_signal::register_handler(int sig, sighandler_t handler, int flags) noexcept {
    struct sigaction act{};
    act.sa_handler = handler;
    ::sigemptyset(&act.sa_mask);
    act.sa_flags = flags;

    struct sigaction old_act;
    if (::sigaction(sig, &act, &old_act) < 0) [[unlikely]]
        return SIG_ERR;
    return old_act.sa_handler;
}

void z_signal::ignore_sigpipe() noexcept {
    register_handler(SIGPIPE, SIG_IGN);   
}

sigset_t z_signal::make_sigset(std::initializer_list<int> signals) noexcept {
    sigset_t set;
    ::sigemptyset(&set);
    for (int sig : signals)
        ::sigaddset(&set, sig);
    return set;
}

int z_signal::block_signal(std::initializer_list<int> signals) noexcept {
    sigset_t set = make_sigset(signals);
    return ::sigprocmask(SIG_BLOCK, &set, nullptr);
}

int z_signal::unblock_signal(std::initializer_list<int> signals) noexcept {
    sigset_t set = make_sigset(signals);
    return ::sigprocmask(SIG_UNBLOCK, &set, nullptr);
}

int z_signal::new_signal_fd(std::initializer_list<int> signals, int sig_fd) noexcept {
    sigset_t set = make_sigset(signals);
    return ::signalfd(sig_fd, &set, SFD_NONBLOCK | SFD_CLOEXEC);
}
