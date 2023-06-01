#pragma once
#include "Utilities/Valid.hpp"
#include <concepts>
#include <limits>
#include <optional>
#include <utility>

template <typename T> struct Optional {
  std::optional<T> opt;
  [[nodiscard]] constexpr auto hasValue() const -> bool {
    return opt.has_value();
  }
  [[nodiscard]] constexpr auto getValue() -> T & {
    invariant(hasValue());
    return *opt;
  }
  constexpr explicit operator bool() const { return hasValue(); }
  constexpr auto operator->() -> T * { return &getValue(); }
  constexpr auto operator->() const -> const T * { return &getValue(); }
  constexpr Optional() = default;
  constexpr Optional(T value) : opt(std::move(value)) {}
  constexpr auto operator*() -> T & { return getValue(); }
};

template <std::signed_integral T> struct Optional<T> {
  static constexpr T null = std::numeric_limits<T>::min();
  [[no_unique_address]] T value{null};
  [[nodiscard]] constexpr auto hasValue() const -> bool {
    return value != null;
  }
  [[nodiscard]] constexpr auto getValue() -> T & {
    invariant(hasValue());
    return value;
  }
  [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
  constexpr auto operator->() -> T * { return &value; }
  constexpr auto operator->() const -> const T * { return &value; }
  constexpr explicit operator bool() const { return hasValue(); }
  constexpr Optional() = default;
  constexpr Optional(T v) : value(v) {}
};
template <std::unsigned_integral T> struct Optional<T> {
  static constexpr T null = std::numeric_limits<T>::max();
  [[no_unique_address]] T value{null};
  [[nodiscard]] constexpr auto hasValue() const -> bool {
    return value != null;
  }
  [[nodiscard]] constexpr auto getValue() -> T & {
    invariant(hasValue());
    return value;
  }
  [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
  constexpr auto operator->() -> T * { return &value; }
  constexpr auto operator->() const -> const T * { return &value; }
  constexpr explicit operator bool() const { return hasValue(); }
  constexpr Optional() = default;
  constexpr Optional(T v) : value(v) {}
};

template <typename T> struct Optional<T &> {
  [[no_unique_address]] T *value{nullptr};
  [[nodiscard]] constexpr auto hasValue() const -> bool {
    return value != nullptr;
  }
  [[nodiscard]] constexpr auto getValue() -> T & {
    invariant(hasValue());
    return *value;
  }
  [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
  constexpr explicit operator bool() const { return hasValue(); }
  constexpr auto operator->() -> T * { return value; }
  constexpr auto operator->() const -> const T * { return &value; }
  constexpr Optional() = default;
  constexpr Optional(T &v) : value(&v) {}
};

// template deduction guides
template <typename T> Optional(T) -> Optional<T>;
template <typename T> Optional(T *) -> Optional<T *>;
template <typename T> Optional(T &) -> Optional<T &>;

template <typename T> struct Optional<T *> {
  [[no_unique_address]] T *value{nullptr};
  [[nodiscard]] constexpr auto hasValue() const -> bool {
    return value != nullptr;
  }
  [[nodiscard]] constexpr auto getValue() -> T & {
    invariant(hasValue());
    return *value;
  }
  [[nodiscard]] constexpr auto getValue() const -> const T & {
    invariant(hasValue());
    return *value;
  }
  [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
  [[nodiscard]] constexpr auto operator*() const -> const T & {
    return getValue();
  }
  [[nodiscard]] constexpr operator NotNull<T>() { return value; }
  constexpr explicit operator bool() const { return hasValue(); }
  constexpr auto operator->() -> T * {
    invariant(hasValue());
    return value;
  }
  constexpr auto operator->() const -> const T * {
    invariant(hasValue());
    return value;
  }
  constexpr Optional() = default;
  constexpr Optional(T *v) : value(v) {}
  constexpr Optional(NotNull<T> v) : value(v) {}
};
