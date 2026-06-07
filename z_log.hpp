#pragma once
#include <stdio.h>
#include "g.hpp"

#define z_log_write(color, level, fmt, args...) do { \
    printf("\e[" color ";1m%s " level "\e[0m " \
        "\e[1m[%s:%d %s]\e[0m " fmt "\n", \
        g.wall_timestr, __FILE__, __LINE__, __func__, ##args); \
} while (0)

#define z_log_info(fmt, args...) \
    z_log_write("32", "I", fmt, ##args)

#define z_log_warning(fmt, args...) \
    z_log_write("33", "W", fmt, ##args)

#define z_log_error(fmt, args...) \
    z_log_write("35", "E", fmt, ##args)
