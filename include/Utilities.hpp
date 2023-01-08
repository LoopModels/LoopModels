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

constexpr void invariant(bool condition) {
  assert(condition && "invariant violation");
  if (!condition) {
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

// TODO: communicate not-null to the compiler somehow?
template <typename T> struct NotNull {
  NotNull() = delete;
  constexpr NotNull(T &v) : value(&v) {}
  constexpr NotNull(T *v) : value(v) { invariant(value != nullptr); }
  constexpr explicit operator bool() const {
    invariant(value != nullptr);
    return true;
  }
  constexpr operator NotNull<const T>() const {
    invariant(value != nullptr);
    return NotNull<const T>(*value);
  }
  [[gnu::returns_nonnull]] constexpr operator T *() {
    invariant(value != nullptr);
    return value;
  }
  [[gnu::returns_nonnull]] constexpr operator T *() const {
    invariant(value != nullptr);
    return value;
  }
  [[gnu::returns_nonnull]] constexpr auto operator->() -> T * {
    invariant(value != nullptr);
    return value;
  }
  constexpr auto operator*() -> T & {
    invariant(value != nullptr);
    return *value;
  }
  [[gnu::returns_nonnull]] constexpr auto operator->() const -> const T * {
    invariant(value != nullptr);
    return value;
  }
  constexpr auto operator*() const -> const T & {
    invariant(value != nullptr);
    return *value;
  }
  constexpr auto operator[](size_t index) -> T & {
    invariant(value != nullptr);
    return value[index];
  }
  constexpr auto operator[](size_t index) const -> const T & {
    invariant(value != nullptr);
    return value[index];
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
  constexpr auto operator+(size_t offset) -> NotNull<T> {
    invariant(value != nullptr);
    return value + offset;
  }
  constexpr auto operator-(size_t offset) -> NotNull<T> {
    invariant(value != nullptr);
    return value - offset;
  }
  constexpr auto operator+(size_t offset) const -> NotNull<T> {
    invariant(value != nullptr);
    return value + offset;
  }
  constexpr auto operator-(size_t offset) const -> NotNull<T> {
    invariant(value != nullptr);
    return value - offset;
  }
  constexpr auto operator++() -> NotNull<T> & {
    invariant(value != nullptr);
    ++value;
    return *this;
  }
  constexpr auto operator++(int) -> NotNull<T> {
    invariant(value != nullptr);
    return value++;
  }
  constexpr auto operator--() -> NotNull<T> & {
    invariant(value != nullptr);
    --value;
    return *this;
  }
  constexpr auto operator--(int) -> NotNull<T> {
    invariant(value != nullptr);
    return value--;
  }

private:
  [[no_unique_address]] T *value;
};
template <typename T> NotNull(T &) -> NotNull<T>;
template <typename T> NotNull(T *) -> NotNull<T *>;

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
  [[nodiscard]] constexpr operator NotNull<T>() { return value; }
  constexpr explicit operator bool() const { return hasValue(); }
  constexpr auto operator->() -> T * {
    assert(hasValue());
    return value;
  }
  constexpr Optional() = default;
  constexpr Optional(T *v) : value(v) {}
};
