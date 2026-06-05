#pragma once
#include <cstdint>
#include <sys/socket.h>
#include "z_ref.hpp"
#include "z_task.hpp"
#include "z_waiter.hpp"

struct z_Fd {
    friend struct z_Epoll;

private:
    z_WaiterList read_wq{};
    z_WaiterList write_wq{};
    z_Node ep_node{}; // link to z_Epoll::dirty_fds
    uint32_t ep_events = 0; // events registered in epoll(kernel)
    uint32_t ref_count = 1;
    int raw_fd = -1;
    bool has_data = true; // set to false when a read operation encounters EAGAIN
    bool has_space = true; // set to false when a write operation encounters EAGAIN

    explicit z_Fd(int fd) noexcept : raw_fd{fd} {}
    ~z_Fd() noexcept { close(); }

public:
    z_ref_impl(z_Fd);
    z_ref_create(z_Fd);

    void close() noexcept;
    bool is_closed() const noexcept { return raw_fd < 0; }

    void add_read_w(z_Waiter *w) noexcept;
    void add_write_w(z_Waiter *w) noexcept;

    void del_read_w(z_Waiter *w) noexcept;
    void del_write_w(z_Waiter *w) noexcept;

    void on_event(bool ev_data, bool ev_space) noexcept;

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
