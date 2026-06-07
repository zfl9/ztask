#pragma once
#include <cstddef>
#include <concepts>
#include <type_traits>

#define Z_STRINGIZE_(x) #x
#define Z_STRINGIZE(x) Z_STRINGIZE_(x)

#define Z_CONCAT_(a, b) a##b
#define Z_CONCAT(a, b) Z_CONCAT_(a, b)

template<typename T>
concept z_pure_c_type =
    std::is_trivially_default_constructible_v<T> &&
    std::is_trivially_copy_constructible_v<T> &&
    std::is_trivially_copy_assignable_v<T> &&
    std::is_trivially_move_constructible_v<T> &&
    std::is_trivially_move_assignable_v<T> &&
    std::is_trivially_destructible_v<T>;

template<auto field_name>
struct z_field_name_traits;

template<typename Container, typename Field, Field Container::*field_name>
struct z_field_name_traits<field_name> {
    using container_type = Container;
    using field_type = Field;
};

template<auto field_name, typename Field>
auto *z_container_of(Field *p) noexcept {
    using traits = z_field_name_traits<field_name>;
    using Container = typename traits::container_type;
    using ExpectedField = typename traits::field_type;
    static_assert(std::is_same_v<std::remove_cv_t<Field>, ExpectedField>);
    size_t offset = reinterpret_cast<size_t>(&(static_cast<Container *>(nullptr)->*field_name));
    return reinterpret_cast<Container *>(reinterpret_cast<char *>(p) - offset);
}

template<auto field_name, typename Field>
const auto *z_container_of(const Field *p) noexcept {
    using traits = z_field_name_traits<field_name>;
    using Container = typename traits::container_type;
    using ExpectedField = typename traits::field_type;
    static_assert(std::is_same_v<std::remove_cv_t<Field>, ExpectedField>);
    size_t offset = reinterpret_cast<size_t>(&(static_cast<const Container *>(nullptr)->*field_name));
    return reinterpret_cast<const Container *>(reinterpret_cast<const char *>(p) - offset);
}
