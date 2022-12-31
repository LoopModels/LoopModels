#pragma once
#include <concepts>
#include <limits>
#include <llvm/Support/Casting.h>
#include <optional>
#include <utility>

template <typename T> struct Optional {
  std::optional<T> opt;
  [[nodiscard]] constexpr auto hasValue() const -> bool {
    return opt.has_value();
  }
  [[nodiscard]] constexpr auto getValue() -> T & {
    assert(hasValue());
    return *opt;
  }
  constexpr explicit operator bool() const { return hasValue(); }
  constexpr auto operator->() -> T * { return &getValue(); }
  constexpr Optional() = default;
  constexpr Optional(T value) : opt(std::move(value)) {}
  constexpr auto operator*() -> T & { return getValue(); }
};

template <typename T> struct Optional<T *> {
  [[no_unique_address]] T *value{nullptr};
  [[nodiscard]] constexpr auto hasValue() const -> bool {
    return value != nullptr;
  }
  [[nodiscard]] constexpr auto getValue() -> T & {
    assert(hasValue());
    return *value;
  }
  [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
  constexpr explicit operator bool() const { return hasValue(); }
  constexpr auto operator->() -> T * { return value; }
  constexpr Optional() = default;
  constexpr Optional(T *v) : value(v) {}
};
template <std::signed_integral T> struct Optional<T> {
  static constexpr T null = std::numeric_limits<T>::min();
  [[no_unique_address]] T value{null};
  [[nodiscard]] constexpr auto hasValue() const -> bool {
    return value != null;
  }
  [[nodiscard]] constexpr auto getValue() -> T & {
    assert(hasValue());
    return value;
  }
  [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
  constexpr auto operator->() -> T * { return &value; }
  constexpr operator bool() const { return hasValue(); }
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
    assert(hasValue());
    return value;
  }
  [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
  constexpr auto operator->() -> T * { return &value; }
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
    assert(hasValue());
    return *value;
  }
  [[nodiscard]] constexpr auto operator*() -> T & { return getValue(); }
  constexpr explicit operator bool() const { return hasValue(); }
  constexpr auto operator->() -> T * { return value; }
  constexpr Optional() = default;
  constexpr Optional(T &v) : value(&v) {}
};

// template deduction guides
template <typename T> Optional(T) -> Optional<T>;
template <typename T> Optional(T *) -> Optional<T *>;
template <typename T> Optional(T &) -> Optional<T &>;

// TODO: communicate not-null to the compiler somehow?
template <typename T> struct NotNull {
  NotNull() = delete;
  constexpr NotNull(T &v) : value(&v) {}
  constexpr NotNull(T *v) : value(v) {
    if (value == nullptr) {
      assert(value != nullptr);
#if __cplusplus >= 202202L
      std::unreachable();
#else
#ifdef __has_builtin
#if __has_builtin(__builtin_unreachable)
      __builtin_unreachable();
#endif
#endif
#endif
    }
  }
  constexpr explicit operator bool() const {
    if (value == nullptr) {
      assert(value != nullptr);
#if __cplusplus >= 202202L
      std::unreachable();
#else
#ifdef __has_builtin
#if __has_builtin(__builtin_unreachable)
      __builtin_unreachable();
#endif
#endif
#endif
    }
    return true;
  }
  constexpr auto operator->() -> T * {
    if (value == nullptr) {
      assert(value != nullptr);
#if __cplusplus >= 202202L
      std::unreachable();
#else
#ifdef __has_builtin
#if __has_builtin(__builtin_unreachable)
      __builtin_unreachable();
#endif
#endif
#endif
    }
    return value;
  }
  constexpr auto operator*() -> T & {
    if (value == nullptr) {
      assert(value != nullptr);
#if __cplusplus >= 202202L
      std::unreachable();
#else
#ifdef __has_builtin
#if __has_builtin(__builtin_unreachable)
      __builtin_unreachable();
#endif
#endif
#endif
    }
    return *value;
  }
  constexpr auto operator->() const -> const T * {
    if (value == nullptr) {
      assert(value != nullptr);
#if __cplusplus >= 202202L
      std::unreachable();
#else
#ifdef __has_builtin
#if __has_builtin(__builtin_unreachable)
      __builtin_unreachable();
#endif
#endif
#endif
    }
    return value;
  }
  constexpr auto operator*() const -> const T & {
    if (value == nullptr) {
      assert(value != nullptr);
#if __cplusplus >= 202202L
      std::unreachable();
#else
#ifdef __has_builtin
#if __has_builtin(__builtin_unreachable)
      __builtin_unreachable();
#endif
#endif
#endif
    }
    return *value;
  }
  constexpr operator T *() {
    if (value == nullptr) {
      assert(value != nullptr);
#if __cplusplus >= 202202L
      std::unreachable();
#else
#ifdef __has_builtin
#if __has_builtin(__builtin_unreachable)
      __builtin_unreachable();
#endif
#endif
#endif
    }
    return value;
  }
  template <typename C> [[nodiscard]] auto dyn_cast() -> C * {
    return llvm::dyn_cast<C>(value);
  }
  template <typename C> [[nodiscard]] auto cast() -> NotNull<C> {
    return NotNull<C>{llvm::cast<C>(value)};
  }
  template <typename C> [[nodiscard]] auto dyn_cast() const -> C * {
    return llvm::dyn_cast<C>(value);
  }
  template <typename C> [[nodiscard]] auto cast() const -> NotNull<C> {
    return NotNull<C>{llvm::cast<C>(value)};
  }
  template <typename C> [[nodiscard]] auto isa() const -> bool {
    return llvm::isa<C>(value);
  }

private:
  [[no_unique_address]] T *value;
};
