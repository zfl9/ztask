#pragma once
#include <bit>
#include <new>
#include <type_traits>
#include <concepts>
#include "z.hpp"
#include "z_list.hpp"

template<typename T>
concept z_pure_c_type =
    std::is_trivially_default_constructible_v<T> &&
    std::is_trivially_copy_constructible_v<T> &&
    std::is_trivially_copy_assignable_v<T> &&
    std::is_trivially_move_constructible_v<T> &&
    std::is_trivially_move_assignable_v<T> &&
    std::is_trivially_destructible_v<T>;

template<typename T>
requires(z_pure_c_type<T> && sizeof(T) <= 32)
struct z_Queue {
    using DestroyFn = void (*)(T item) noexcept;
private:
    size_t _head = 0;
    size_t _tail = 0;
    size_t _count = 0;
    size_t _capacity = 0;
    T *_array = nullptr;
    DestroyFn _destroy_fn = nullptr;
    z_List<z_Task, &z_Task::wait_node> _push_tasks{};
    z_List<z_Task, &z_Task::wait_node> _pop_tasks{};

public:
    z_Queue(size_t capacity, DestroyFn destroy_fn = nullptr) noexcept :
        _capacity{std::bit_ceil(capacity)},
        _array{new (std::nothrow) T[_capacity]},
        _destroy_fn{destroy_fn}
        {}

    ~z_Queue() noexcept {
        clear();
        delete[] _array;
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
                queue->_push_tasks.push_tail(z_current());
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
                queue->_pop_tasks.push_tail(z_current());
                z_yield(z_current()->wait_node.unlink());
            }
            z_ret();
        }
    };

    // on data available
    void on_data() noexcept {
        while (z_Task *task = _pop_tasks.first()) {
            if (is_empty()) break;
            task->resume();
        }
    }

    // on space available
    void on_space() noexcept {
        while (z_Task *task = _push_tasks.first()) {
            if (is_full()) break;
            task->resume();
        }
    }

    // @return ok
    bool push(T item) noexcept {
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

    void clear() noexcept {
        assert(_push_tasks.is_empty());
        assert(_pop_tasks.is_empty());
        if (_destroy_fn) {
            for (size_t i = 0; i < _count; ++i) {
                size_t pos = (_head + i) & (_capacity - 1);
                _destroy_fn(_array[pos]);
            }
        }
        _head = _tail = _count = 0;
    }

    void set_destroy_fn(DestroyFn destroy_fn) noexcept {
        _destroy_fn = destroy_fn;
    }
};
