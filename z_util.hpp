#pragma once
#include <cstddef>
#include <type_traits>

template<auto FieldPtr>
struct z_FieldPtrTraits;

template<typename Container, typename Field, Field Container::*FieldPtr>
struct z_FieldPtrTraits<FieldPtr> {
    using container_type = Container;
    using field_type = Field;
};

template<auto FieldPtr, typename Field>
auto *z_container_of(Field *field) noexcept {
    using Traits = z_FieldPtrTraits<FieldPtr>;
    using Container = typename Traits::container_type;
    using ExpectedField = typename Traits::field_type;
    static_assert(std::is_same_v<std::remove_cv_t<Field>, ExpectedField>);
    size_t offset = reinterpret_cast<size_t>(&(static_cast<Container *>(nullptr)->*FieldPtr));
    return reinterpret_cast<Container *>(reinterpret_cast<char *>(field) - offset);
}

template<auto FieldPtr, typename Field>
const auto *z_container_of(const Field *field) noexcept {
    using Traits = z_FieldPtrTraits<FieldPtr>;
    using Container = typename Traits::container_type;
    using ExpectedField = typename Traits::field_type;
    static_assert(std::is_same_v<std::remove_cv_t<Field>, ExpectedField>);
    size_t offset = reinterpret_cast<size_t>(&(static_cast<const Container *>(nullptr)->*FieldPtr));
    return reinterpret_cast<const Container *>(reinterpret_cast<const char *>(field) - offset);
}
