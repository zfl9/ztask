#include "z_sleep.hpp"
#include "z_env.hpp"

z_function_def(z_sleep, void, uint64_t ms) {
    z_begin();

    z_env::add_timer(z_timer(), ms);
    z_yield();
    z_env::del_timer(z_timer());

    z_return_void();
}
