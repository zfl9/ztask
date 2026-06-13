#include "z_sleep.hpp"

z_function_def(z_sleep, void, uint64_t ms) {
    z_begin();
    z_timer_arm(ms);

    // wait for timer event
    z_yield();

    z_timer_disarm();
    z_return_void();
}
