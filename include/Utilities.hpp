#pragma once
#include <concepts>
#include <limits>
#include <optional>
#include <utility>

struct Empty {};

template <typename T> struct Optional {
    std::optional<T> opt;
    [[nodiscard]] constexpr auto hasValue() const -> bool {
        return opt.has_value();
    }
    [[nodiscard]] constexpr auto getValue() const -> T & {
        assert(hasValue());
        return *opt;
    }
    constexpr operator bool() const { return hasValue(); }
    constexpr auto operator->() -> T * { return &getValue(); }
    constexpr Optional() = default;
    constexpr Optional(T value) : opt(std::move(value)) {}
    constexpr Optional(Empty) {}
    constexpr auto operator*() -> T & { return getValue(); }
};

template <typename T> struct Optional<T *> {
    [[no_unique_address]] T *value{nullptr};
    [[nodiscard]] constexpr auto hasValue() const -> bool {
        return value != nullptr;
    }
    [[nodiscard]] constexpr auto getValue() const -> T & {
        assert(hasValue());
        return *value;
    }
    [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
    constexpr operator bool() const { return hasValue(); }
    constexpr auto operator->() -> T * { return value; }
    constexpr Optional() = default;
    constexpr Optional(T *v) : value(v) {}
};
template <std::signed_integral T> struct Optional<T> {
    [[no_unique_address]] T value{std::numeric_limits<T>::min()};
    [[nodiscard]] constexpr auto hasValue() const -> bool {
        return value != std::numeric_limits<T>::min();
    }
    [[nodiscard]] constexpr auto getValue() const -> T & {
        assert(hasValue());
        return value;
    }
    [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
    constexpr operator bool() const { return hasValue(); }
};
template <std::unsigned_integral T> struct Optional<T> {
    [[no_unique_address]] T value{std::numeric_limits<T>::max()};
    [[nodiscard]] constexpr auto hasValue() const -> bool {
        return value != std::numeric_limits<T>::max();
    }
    [[nodiscard]] constexpr auto getValue() const -> T & {
        assert(hasValue());
        return value;
    }
    [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
    constexpr operator bool() const { return hasValue(); }
};