#pragma once
#include "z_list.hpp"
#include "z_fd.hpp"

struct z_Epoll {
private:
    z_List<z_Fd, &z_Fd::ep_node> dirty_fds{};
    int ep_fd = -1;

public:
    z_Epoll() noexcept;
    ~z_Epoll() noexcept;

    void run() noexcept;
    void on_fd_dirty(z_Fd *fd) noexcept;
    void on_fd_close(z_Fd *fd) noexcept;

private:
    void flush_dirty_fds() noexcept;

    void ep_add(z_Fd *fd, uint32_t events) noexcept;
    void ep_del(z_Fd *fd) noexcept;
};
