#pragma once
#include <cstdint>
#include <array>
#include <sys/uio.h>
#include <sys/socket.h>
#include "z_ref.hpp"
#include "z_task.hpp"
#include "z_waiter.hpp"
#include "z_net.hpp"

struct z_Fd {
    friend struct z_Epoll;

private:
    z_WaiterList read_wq{};
    z_WaiterList write_wq{};
    z_Node ep_node{}; // link to z_Epoll::dirty_fds
    uint32_t ep_events = 0; // events registered in epoll(kernel)
    uint32_t ref_count = 1;
    int raw_fd = -1;
    uint8_t in_event_fn = 0;
    bool has_data = true; // set to false when read encounter EAGAIN
    bool has_space = true; // set to false when write encounter EAGAIN

    explicit z_Fd(int fd) noexcept : raw_fd{fd} {}
    ~z_Fd() noexcept { close(); }

    void sync_dirty_state() noexcept;

public:
    z_ref_counted(z_Fd);
    z_ref_creator(z_Fd);

    int handle() const noexcept { return raw_fd; }

    void close() noexcept;
    bool is_closed() const noexcept { return raw_fd < 0; }

    int shutdown(int how) noexcept { return ::shutdown(raw_fd, how); }

    void add_read_w(z_Waiter *w) noexcept;
    void add_write_w(z_Waiter *w) noexcept;

    void del_read_w(z_Waiter *w) noexcept;
    void del_write_w(z_Waiter *w) noexcept;

    uint32_t want_events() const noexcept;

    void on_event(bool ev_data, bool ev_space) noexcept;

    // for byte-stream
    struct z_read {
        ssize_t n_read = 0;
        z_leaf_fields();
        z_deinit(z_read) {}

        struct Opt { size_t at_least; int timeout; };
        z_function(ssize_t, z_Fd *fd, void *buf, size_t len, Opt opt = {});
        z_function(ssize_t, z_Fd *fd, const iovec *iov, int iovcnt, Opt opt = {});

        template<size_t N>
        z_function(ssize_t, z_Fd *fd, std::array<iovec, N> &&iov, Opt opt = {}) {
            return z_function_call(fd, &iov[0], (int)N, opt);
        }
    };

    // for byte-stream
    struct z_write {
        ssize_t n_write = 0;
        z_leaf_fields();
        z_deinit(z_write) {}

        struct Opt { int timeout; };
        z_function(ssize_t, z_Fd *fd, const void *buf, size_t len, Opt opt = {});
        z_function(ssize_t, z_Fd *fd, const iovec *iov, int iovcnt, Opt opt = {});

        template<size_t N>
        z_function(ssize_t, z_Fd *fd, std::array<iovec, N> &&iov, Opt opt = {}) {
            return z_function_call(fd, &iov[0], (int)N, opt);
        }
    };

    // for byte-stream or datagram
    struct z_recv {
        ssize_t n_read = 0;
        z_leaf_fields();
        z_deinit(z_recv) {}

        struct Opt { size_t at_least; z_net::Addr *addr; int flags; int timeout; };
        z_function(ssize_t, z_Fd *fd, void *buf, size_t len, Opt opt = {});
        z_function(ssize_t, z_Fd *fd, msghdr *msg, Opt opt = {});
        z_function(ssize_t, z_Fd *fd, msghdr &&msg, Opt opt = {}) {
            return z_function_call(fd, &msg, opt);
        }
    };

    // for byte-stream or datagram
    struct z_send {
        ssize_t n_write = 0;
        z_leaf_fields();
        z_deinit(z_send) {}

        struct Opt { const z_net::Addr *addr; int flags; int timeout; };
        z_function(ssize_t, z_Fd *fd, const void *buf, size_t len, Opt opt = {});
        z_function(ssize_t, z_Fd *fd, const msghdr *msg, Opt opt = {});
        z_function(ssize_t, z_Fd *fd, msghdr &&msg, Opt opt = {}) {
            return z_function_call(fd, &msg, opt);
        }
    };

    // for datagram
    struct z_recvmmsg {
        z_leaf_fields();
        z_deinit(z_recvmmsg) {}

        struct Opt { int flags; int timeout; };
        z_function(int, z_Fd *fd, mmsghdr *msgv, unsigned vlen, Opt opt = {});

        template<size_t N>
        z_function(int, z_Fd *fd, std::array<mmsghdr, N> &&msgv, Opt opt = {}) {
            return z_function_call(fd, &msgv[0], (unsigned)N, opt);
        }
    };

    // for datagram
    struct z_sendmmsg {
        int n_sent = 0;
        z_leaf_fields();
        z_deinit(z_sendmmsg) {}

        struct Opt { int flags; int timeout; };
        z_function(int, z_Fd *fd, mmsghdr *msgv, unsigned vlen, Opt opt = {});

        template<size_t N>
        z_function(int, z_Fd *fd, std::array<mmsghdr, N> &&msgv, Opt opt = {}) {
            return z_function_call(fd, &msgv[0], (unsigned)N, opt);
        }
    };

    struct z_accept {
        z_leaf_fields();
        z_deinit(z_accept) {}

        struct Opt { int timeout; };
        z_function(int, z_Fd *fd, z_net::Addr *addr, Opt opt = {});
    };

    struct z_connect {
        z_leaf_fields();
        z_deinit(z_connect) {}

        struct Opt { int timeout; };
        z_function(int, z_Fd *fd, const z_net::Addr *addr, Opt opt = {});
        z_function(int, z_Fd *fd, z_net::Addr &&addr, Opt opt = {}) {
            return z_function_call(fd, &addr, opt);
        }
    };

    // for byte-stream && use splice to zero-copy
    struct z_forward {
        z_Waiter waiter{waiter_cb};
        z_Task *task = nullptr;
        size_t a_len = 0;
        size_t b_len = 0;
        z_leaf_fields();
        bool a_shutdown = false;
        bool b_shutdown = false;
        bool half_started = false;
        z_deinit(z_forward) {}

        struct Opt { unsigned flags; int idle_timeout; int half_timeout; };
        z_function(int, z_Fd *a_fd, z_Fd *b_fd, int a_pipe, int b_pipe, Opt opt = {});

        static void waiter_cb(z_Waiter *waiter, void *arg) noexcept {
            z_forward *f = z_container_of<&z_forward::waiter>(waiter);
            z_EventCtx event_ctx{
                .u = {.waiter = waiter},
                .arg = arg,
            };
            return f->task->resume(z_Event::WAITER, &event_ctx);
        }

        static int do_forward(z_Waiter *w, z_Fd *in, z_Fd *out,
            int pipe, size_t &len, bool &shutdown, unsigned flags) noexcept;
    };
};
