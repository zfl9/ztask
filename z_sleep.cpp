#include "z_sleep.hpp"
#include <cassert>
#include "z_env.hpp"

z_function_def(z_sleep, void, uint64_t ms) {
    z_begin();

    z_env::add_timer(z_timer(), ms);
    z_yield();
    switch (z_waker()) {
        case z_Waker::TIMER:
            assert(!z_timer()->linked());
            break;
        case z_Waker::CANCEL:
            z_env::del_timer(z_timer());
            break;
        default:
            std::unreachable();
    }

    z_ret();
}
