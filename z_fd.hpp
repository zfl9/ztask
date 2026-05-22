#pragma once
#include <cstdint>
#include <sys/socket.h>
#include "z.hpp"
#include "z_list.hpp"

struct z_Fd {
    z_List<z_Task, &z_Task::wait_node> readers{};
    z_List<z_Task, &z_Task::wait_node> writers{};
    z_Node ep_node{}; // link to z_Epoll::dirty_fds
    uint32_t ep_events = 0; // events registered in epoll(kernel)
    uint32_t ref_count = 1;
    int raw_fd = -1;
    bool has_data = true; // set to false when a read operation encounters EAGAIN
    bool has_space = true; // set to false when a write operation encounters EAGAIN

    explicit z_Fd(int fd) noexcept : raw_fd{fd} {}

    ~z_Fd() noexcept {
        close_fd();
    }

    void close_fd() noexcept;

    z_Fd *ref() noexcept {
        ++ref_count;
        return this;
    }

    void unref() noexcept {
        if (--ref_count == 0)
            delete this;
    }

    void add_reader(z_Task *task) noexcept;
    void add_writer(z_Task *task) noexcept;

    void del_reader(z_Task *task) noexcept;
    void del_writer(z_Task *task) noexcept;

    void on_readable() noexcept;
    void on_writable() noexcept;
    void on_error() noexcept;

    struct z_read {
        z_leaf_fields();
        size_t n_read = 0;
        z_deinit(z_read) {}
        z_function(ssize_t, z_Fd *fd, void *buf, size_t len, size_t at_least = 0);
    };

    struct z_write {
        z_leaf_fields();
        size_t n_write = 0;
        z_deinit(z_write) {}
        z_function(ssize_t, z_Fd *fd, const void *buf, size_t len);
    };

    struct z_accept {
        z_leaf_fields();
        z_deinit(z_accept) {}
        z_function(int, z_Fd *fd, struct sockaddr *addr = nullptr, socklen_t *addrlen = nullptr, int flags = 0);
    };

    struct z_connect {
        z_leaf_fields();
        z_deinit(z_connect) {}
        z_function(int, z_Fd *fd, const struct sockaddr *addr, socklen_t addrlen);
    };
};
