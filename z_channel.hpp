#pragma once
#include <bit>
#include <new>
#include <utility>
#include <cerrno>
#include "z_ref.hpp"
#include "z_task.hpp"
#include "z_waiter.hpp"
#include "z_util.hpp"

template<typename T>
requires(z_pure_c_type<T> && sizeof(T) <= 32)
struct z_Channel {
    using DestroyFn = void (*)(T item) noexcept;

private:
    size_t _head = 0;
    size_t _tail = 0;
    size_t _count = 0;
    size_t _capacity = 0;
    T *_array = nullptr;
    DestroyFn _destroy_fn = nullptr;
    z_WaiterList _read_wq{};
    z_WaiterList _write_wq{};
    uint32_t ref_count = 1;
    bool _closed = false;

    explicit z_Channel(size_t capacity, DestroyFn destroy_fn = nullptr) noexcept :
        _capacity{std::bit_ceil(capacity)},
        _array{new (std::nothrow) T[_capacity]},
        _destroy_fn{destroy_fn}
        {}

    ~z_Channel() noexcept {
        close();
        if (_destroy_fn) {
            for (size_t i = 0; i < _count; ++i) {
                size_t pos = (_head + i) & (_capacity - 1);
                _destroy_fn(_array[pos]);
            }
        }
        delete[] _array;
    }

public:
    z_ref_impl(z_Channel);
    z_ref_create(z_Channel);

    struct z_read {
        z_leaf_fields();
        z_deinit(z_read) {}
        z_function(int, z_Channel *channel, T *item, int timeout = 0) {
            z_begin();
            z_timer_arm(timeout);
            int res;
            for (;;) {
                res = channel->read(item);
                if (res != -EAGAIN) {
                    goto out;
                } else {
                    channel->add_read_w(z_waiter());
                    z_yield();
                    switch (z_event()) {
                        case z_Event::WAITER:
                            continue;
                        case z_Event::TIMER:
                        case z_Event::CANCEL:
                            channel->del_read_w(z_waiter());
                            res = (z_event() == z_Event::TIMER) ? -ETIMEDOUT : -ECANCELED;
                            goto out;
                        default:
                            std::unreachable();
                    }
                }
            }
            out:
            z_timer_disarm();
            z_return(res);
        }
    };

    struct z_write {
        z_leaf_fields();
        z_deinit(z_write) {}
        z_function(int, z_Channel *channel, T item, int timeout = 0) {
            z_begin();
            z_timer_arm(timeout);
            int res;
            for (;;) {
                res = channel->write(item);
                if (res != -EAGAIN) {
                    goto out;
                } else {
                    channel->add_write_w(z_waiter());
                    z_yield();
                    switch (z_event()) {
                        case z_Event::WAITER:
                            continue;
                        case z_Event::TIMER:
                        case z_Event::CANCEL:
                            channel->del_write_w(z_waiter());
                            res = (z_event() == z_Event::TIMER) ? -ETIMEDOUT : -ECANCELED;
                            goto out;
                        default:
                            std::unreachable();
                    }
                }
            }
            out:
            z_timer_disarm();
            z_return(res);
        }
    };

    void add_read_w(z_Waiter *w) noexcept {
        assert(!has_data() && !_closed);
        _read_wq.push_tail(w);
    }

    void add_write_w(z_Waiter *w) noexcept {
        assert(!has_space() && !_closed);
        _write_wq.push_tail(w);
    }

    void del_read_w(z_Waiter *w) noexcept {
        w->unlink();
    }

    void del_write_w(z_Waiter *w) noexcept {
        w->unlink();
    }

    void on_data() noexcept {
        while (has_data() || _closed) {
            auto *w = _read_wq.pop_head();
            if (!w) break;
            w->callback(w, this);
        }
    }

    void on_space() noexcept {
        while (has_space() || _closed) {
            auto *w = _write_wq.pop_head();
            if (!w) break;
            w->callback(w, this);
        }
    }

    /// @return `-err` or `0`
    int read(T *item) noexcept {
        assert(item != nullptr);
        if (_count == 0) return _closed ? -EPIPE : -EAGAIN;
        *item = _array[_head];
        _head = (_head + 1) & (_capacity - 1);
        _count--;
        on_space();
        return 0;
    }

    /// @return `-err` or `0`
    int write(T item) noexcept {
        if (_closed) [[unlikely]] return -EPIPE;
        if (_count >= _capacity) return -EAGAIN;
        _array[_tail] = item;
        _tail = (_tail + 1) & (_capacity - 1); 
        _count++;
        on_data();
        return 0;
    }

    void close() noexcept {
        if (_closed) return;
        _closed = true;
        on_data();
        on_space();
        assert(_read_wq.is_empty());
        assert(_write_wq.is_empty());
    }

    bool is_closed() const noexcept {
        return _closed;
    }

    // peek first item (or nullptr if empty)
    T *peek() const noexcept {
        return (_count > 0) ? &_array[_head] : nullptr;
    }

    size_t count() const noexcept {
        return _count;
    }

    size_t capacity() const noexcept {
        return _capacity;
    }

    bool has_data() const noexcept {
        return _count > 0;
    }

    bool has_space() const noexcept {
        return _count < _capacity;
    }

    void set_destroy_fn(DestroyFn destroy_fn) noexcept {
        _destroy_fn = destroy_fn;
    }
};
