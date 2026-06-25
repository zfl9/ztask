#pragma once
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <sys/epoll.h>
#include "z_list.hpp"
#include "z_fd.hpp"

struct z_Epoll {
private:
    z_List<z_Fd, &z_Fd::ep_node> dirty_fds{};
    int ep_fd = epoll_create1(EPOLL_CLOEXEC);

public:
    ~z_Epoll() noexcept {
        assert(dirty_fds.is_empty());
        ::close(ep_fd);
    }

    void run() noexcept;
    void on_fd_dirty(z_Fd *fd) noexcept;
    void on_fd_close(z_Fd *fd) noexcept;

private:
    void flush_dirty_fds() noexcept;

    void ep_add(z_Fd *fd, uint32_t events) noexcept;
    void ep_del(z_Fd *fd) noexcept;
};
