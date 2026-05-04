#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <time.h>
#include "z.hpp"
#include "z_ev.hpp"
#include "z_queue.hpp"
#include "z_list.hpp"
#include "z_timer.hpp"

struct tcp_echo final : z_Task {
    z_fields(z_ev_read read; z_ev_write write);
    ev_io io;
    char buf[128];
    ssize_t len;

    z_impl_deinit(tcp_echo) {
        printf("~tcp_echo(): fd:%d\n", io.fd);
        z_ev::io_stop(&io);
        close(io.fd);
    }

    tcp_echo(int fd) {
        ev_io_init(&io, nullptr, fd, 0);
    }

    z_function(void) {
        z_begin();

        for (;;) {
            z_call(read, &len, &io, &buf, sizeof(buf));
            if (len < 0) {
                perror("read err");
                break;
            }
            if (len == 0) {
                printf("fd:%d (close)\n", io.fd);
                break;
            }
            if (buf[len-1] == '\n') --len;
            if (len == 0) continue;
            printf("fd:%d msg:'%.*s'\n", io.fd, (int)len, buf);
            z_call(write, z_no_result(), &io, &buf, len);
        }

        z_ret();
    }
};

struct tcp_server final : z_Task {
    z_fields(z_ev_accept accept);
    ev_io io;
    int port;

    z_impl_deinit(tcp_server) {
        printf("~tcp_server(): fd:%d\n", io.fd);
        z_ev::io_stop(&io);
        close(io.fd);
    }

    tcp_server(int port) : port(port) {
        ev_io_init(&io, nullptr, -1, 0);
    }

    bool init() {
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = INADDR_ANY;
        sin.sin_port = htons(port);

        if (bind(fd, (sockaddr *)&sin, sizeof(sin)) < 0 || listen(fd, 128) < 0) {
            perror("bind or listen err");
            close(fd);
            return false;
        }

        ev_io_init(&io, nullptr, fd, 0);
        return true;
    };

    z_function(void) {
        z_begin();

        if (!init()) z_ret();

        for (;;) {
            int cfd;
            z_call(accept, &cfd, &io);
            if (cfd < 0) {
                perror("accept err");
                break;
            }
            z_launch(tcp_echo, cfd);
        }

        z_ret();
    }
};

struct producer final : z_Task {
    z_fields(z_Queue<int>::z_push push; z_ev_sleep sleep);
    ev_timer timer;
    z_Queue<int> *queue;
    int i = 0;
    int count;
    int interval;

    z_impl_deinit(producer) {
        printf("~producer()\n");
        z_ev::timer_stop(&timer);
    }

    producer(z_Queue<int> *queue, int count, int interval) noexcept :
        queue{queue}, count{count}, interval{interval}
    {
        ev_timer_init(&timer, nullptr, 0, 0);
    }

    z_function(void) {
        z_begin();

        for (i = 0; i < count; ++i) {
            z_call(sleep, z_no_result(), &timer, interval);
            printf("[%ld] push ... (%d)\n", time(nullptr), i);
            z_call(push, z_no_result(), queue, i);
            printf("[%ld] push end (%d)\n", time(nullptr), i);
        }

        z_ret();
    }
};

struct consumer final : z_Task {
    z_fields(z_Queue<int>::z_pop pop; z_ev_sleep sleep);
    ev_timer timer;
    z_Queue<int> *queue;
    int i = 0;
    int count;
    int interval;

    z_impl_deinit(consumer) {
        printf("~consumer()\n");
        z_ev::timer_stop(&timer);
    }

    consumer(z_Queue<int> *queue, int count, int interval) noexcept :
        queue{queue}, count{count}, interval{interval}
    {
        ev_timer_init(&timer, nullptr, 0, 0);
    }

    z_function(void) {
        z_begin();

        for (i = 0; i < count; ++i) {
            z_call(sleep, z_no_result(), &timer, interval);
            printf("[%ld] pop ... (%d)\n", time(nullptr), i);
            int data;
            z_call(pop, z_no_result(), queue, &data);
            printf("[%ld] pop end (%d): %d\n", time(nullptr), i, data);
        }

        z_ret();
    }
};

int main() {
    z_ev::init();

    z_launch(tcp_server, 8888);
    z_launch(tcp_server, 8888); // fail-fast
    z_launch(tcp_server, 8889);

    z_Queue<int> queue{1};
    z_launch(producer, &queue, 5, 1);
    z_launch(consumer, &queue, 5, 3);

    {
        z_Queue<int> queue{8};
        for (int i = 0; i < 10; ++i) {
            bool ok = queue.push(i);
            if (i < 8) assert(ok);
            else assert(!ok);
        }
        assert(queue.count() == 8);
    }

    {
        struct Item {
            z_Node node{};
            int id;
        };

        z_List<Item, &Item::node> list{};

        Item a{.id = 1}, b{.id = 2}, c{.id = 3};

        list.push_tail(&a);
        list.push_tail(&b);
        list.push_tail(&c);

        printf("items()\n");
        for (auto item : list.items()) {
            printf("item.id: %d\n", item->id);
        }
        printf("rev_items()\n");
        for (auto item : list.rev_items()) {
            printf("item.id: %d\n", item->id);
        }

        list.clear();
        printf("items()\n");
        for (auto item : list.items()) {
            printf("item.id: %d\n", item->id);
        }
        printf("rev_items()\n");
        for (auto item : list.rev_items()) {
            printf("item.id: %d\n", item->id);
        }

        list.push_head(&a);
        list.push_head(&b);
        list.push_head(&c);
        printf("items()\n");
        for (auto item : list.items()) {
            printf("item.id: %d\n", item->id);
        }
        printf("rev_items()\n");
        for (auto item : list.rev_items()) {
            printf("item.id: %d\n", item->id);
        }
    }

    z_ev::run();

    return 0;
}
