#pragma once
#include <bit>
#include <new>
#include <type_traits>
#include "z.hpp"
#include "z_list.hpp"

template <typename T>
concept SmallTrivial = std::is_trivially_copyable_v<T> && (sizeof(T) <= 32);

template <SmallTrivial T>
struct z_Queue {
private:
    z_List<z_Task, &z_Task::wait_node> _push_waiters{};
    z_List<z_Task, &z_Task::wait_node> _pop_waiters{};
    T *_array = nullptr;
    size_t _head = 0;
    size_t _tail = 0;
    size_t _count = 0;
    const size_t _capacity;
    using DestroyFn = void (*)(T item) noexcept;
    DestroyFn _destroy_fn;

public:
    z_Queue(size_t capacity = 64, DestroyFn destroy_fn = nullptr) noexcept :
        _capacity{std::bit_ceil(capacity)},
        _destroy_fn{destroy_fn}
        {}

    ~z_Queue() noexcept {
        assert(_push_waiters.is_empty());
        assert(_pop_waiters.is_empty());
        clear(true);
    }

    // copy,move ctor
    z_Queue(z_Queue &&other) = delete;
    z_Queue(const z_Queue &) = delete;

    // copy,move assign
    z_Queue &operator=(z_Queue &&) = delete;
    z_Queue &operator=(const z_Queue &) = delete;

    struct z_push {
        z_leaf_fields();

        z_def_deinit(z_push) {}

        z_function(void, z_Queue *queue, T item) {
            z_begin();
            while (!queue->push(item)) {
                queue->_push_waiters.push_tail(z_current());
                z_yield(z_current()->wait_node.unlink());
            }
            z_ret();
        }
    };

    struct z_pop {
        z_leaf_fields();

        z_def_deinit(z_pop) {}

        z_function(void, z_Queue *queue, T *item) {
            z_begin();
            while (!queue->pop(item)) {
                queue->_pop_waiters.push_tail(z_current());
                z_yield(z_current()->wait_node.unlink());
            }
            z_ret();
        }
    };

    // on data available
    void on_data() noexcept {
        while (z_Task *waiter = _pop_waiters.first()) {
            if (is_empty()) break;
            waiter->resume();
        }
    }

    // on space available
    void on_space() noexcept {
        while (z_Task *waiter = _push_waiters.first()) {
            if (is_full()) break;
            waiter->resume();
        }
    }

    // @return ok
    bool push(T item) noexcept {
        if (!_array) {
            _array = new (std::nothrow) T[_capacity];
            if (!_array) [[unlikely]] return false;
        }
        if (_count >= _capacity) return false;
        _array[_tail] = item;
        _tail = (_tail + 1) & (_capacity - 1); 
        _count++;
        on_data();
        return true;
    }

    // @return ok
    bool pop(T *item) noexcept {
        assert(item != nullptr);
        if (_count == 0) return false;
        *item = _array[_head];
        _head = (_head + 1) & (_capacity - 1);
        _count--;
        on_space();
        return true;
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

    bool is_empty() const noexcept {
        return _count == 0;
    }

    bool is_full() const noexcept {
        return _count >= _capacity;
    }

    void clear(bool free_mem = false) noexcept {
        if (_destroy_fn) {
            for (size_t i = 0; i < _count; ++i) {
                size_t pos = (_head + i) & (_capacity - 1);
                _destroy_fn(_array[pos]);
            }
        }
        _head = _tail;
        _count = 0;
        if (free_mem && _array) {
            delete[] _array;
            _array = nullptr;
        }
    }

    void set_destroy_fn(DestroyFn destroy_fn) noexcept {
        _destroy_fn = destroy_fn;
    }
};
