#pragma once
#include <stdio.h>
#include "g.hpp"

#define log_write(color, level, fmt, args...) ({ \
    printf("\e[" color ";1m%s " level "\e[0m " \
        "\e[1m[%s:%d %s]\e[0m " fmt "\n", \
        g.wall_timestr, __FILE__, __LINE__, __func__, ##args); \
})

#define log_info(fmt, args...) \
    log_write("32", "I", fmt, ##args)

#define log_warning(fmt, args...) \
    log_write("33", "W", fmt, ##args)

#define log_error(fmt, args...) \
    log_write("35", "E", fmt, ##args)
