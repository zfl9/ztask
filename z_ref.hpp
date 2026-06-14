#pragma once
#include <new>
#include <utility>
#include <type_traits>

template<typename T>
struct z_Ref {
private:
    T *ptr = nullptr;

public:
    explicit z_Ref(T *ptr) noexcept : ptr{ptr} {} // ownership moved to z_Ref
    ~z_Ref() noexcept { if (ptr) ptr->drop_ref(); }

    z_Ref(z_Ref &&other) noexcept : ptr{std::exchange(other.ptr, nullptr)} {}
    z_Ref(const z_Ref &) = delete; // use `share()` instead

    z_Ref &operator=(z_Ref &&other) noexcept {
        z_Ref tmp{std::move(other)}; // ownership moved to tmp
        std::swap(ptr, tmp.ptr);
        return *this;
    }
    z_Ref &operator=(const z_Ref &) = delete;

    template<typename U>
    friend struct z_Ref;

    template<typename U>
    z_Ref(z_Ref<U> &&other) noexcept : ptr{std::exchange(other.ptr, nullptr)} {
        static_assert(std::is_convertible_v<U *, T *>, "incompatible pointer types for z_Ref conversion");
    }

    T *operator->() const noexcept { return ptr; }
    T &operator*() const noexcept { return *ptr; }
    explicit operator bool() const noexcept { return ptr != nullptr; }

    [[nodiscard]] z_Ref share() const noexcept { return z_Ref{ ptr->add_ref() }; } // ref++
    [[nodiscard]] T *raw() const noexcept { return ptr; } // raw pointer
};

#define z_ref_counted(T) \
    T(T &&) = delete; \
    T(const T &) = delete; \
    T &operator=(T &&) = delete; \
    T &operator=(const T &) = delete; \
    [[nodiscard]] z_Ref<T> share() noexcept { return z_Ref<T>{ add_ref() }; } \
    [[nodiscard]] T *add_ref() noexcept { ++ref_count; return this; } \
    void drop_ref() noexcept { if (--ref_count == 0) delete this; }

#define z_ref_creator(T) \
    template<typename... Args> \
    [[nodiscard]] static z_Ref<T> create(Args &&... args) noexcept { \
        T *ptr = new (std::nothrow) T{std::forward<Args>(args)...}; \
        return z_Ref<T>{ ptr }; \
    }
