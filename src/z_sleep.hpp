#pragma once
#include "z_task.hpp"

struct z_sleep {
    z_leaf_fields();
    z_deinit(z_sleep) {}
    z_function(void, uint64_t ms);
};
