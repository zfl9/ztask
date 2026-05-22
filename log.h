#pragma once

#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define log_write(color, level, fmt, args...) ({ \
    time_t t_ = time(NULL); \
    struct tm *tm_ = localtime(&t_); \
    printf("\e[" color ";1m%d-%02d-%02d %02d:%02d:%02d " level "\e[0m " \
        "\e[1m[%s:%d %s]\e[0m " fmt "\n", \
        tm_->tm_year + 1900, tm_->tm_mon + 1, tm_->tm_mday, \
        tm_->tm_hour,        tm_->tm_min,     tm_->tm_sec, \
        __FILE__, __LINE__, __func__, ##args); \
})

#define log_info(fmt, args...) \
    log_write("32", "I", fmt, ##args)

#define log_warning(fmt, args...) \
    log_write("33", "W", fmt, ##args)

#define log_error(fmt, args...) \
    log_write("35", "E", fmt, ##args)

#ifdef __cplusplus
}
#endif
