#pragma once
#include "Utilities/Invariant.hpp"
#include <memory>

template <typename T> class UList {
  T *data[6]; // NOLINT(modernize-avoid-c-arrays)
  size_t count{0};
  UList<T> *next{nullptr};

public:
  constexpr UList() = default;
  constexpr UList(T *t) : count(1) { data[0] = t; }
  constexpr UList(T *t, UList *n) : count(1), next(n) { data[0] = t; }
  constexpr void forEach(const auto &f) {
    invariant(count <= std::size(data));
    for (size_t i = 0; i < count; i++) f(data[i]);
    if (next != nullptr) next->forEach(f);
  }
  constexpr void forEachReverse(const auto &f) {
    invariant(count <= std::size(data));
    UList<T> *recurse = next;
    for (size_t i = count; i--;) f(data[i]);
    if (recurse != nullptr) recurse->forEachReverse(f);
  }
  constexpr void forEachStack(const auto &f) {
    invariant(count <= std::size(data));
    // the motivation of this implementation is that we use this to
    // deallocate the list, which may contain pointers that themselves
    // allocated this.
    UList<T> copy = *this;
    copy._forEachStack(f);
  }
  constexpr void _forEachStack(const auto &f) {
    invariant(count <= std::size(data));
    for (size_t i = 0; i < count; i++) f(data[i]);
    if (next != nullptr) next->forEachStack(f);
  }
  constexpr void forEachNoRecurse(const auto &f) {
    invariant(count <= std::size(data));
    for (size_t i = 0; i < count; i++) f(data[i]);
  }
  constexpr void pushHasCapacity(T *t) {
    invariant(count < std::size(data));
    data[count++] = t;
  }
  /// unordered push
  template <class A>
  [[nodiscard]] constexpr auto push(A &alloc, T *t) -> UList * {
    invariant(count <= std::size(data));
    if (!isFull()) {
      data[count++] = t;
      return this;
    }
    UList<T> *other = alloc.allocate(1);
    std::construct_at(other, t, this);
    return other;
  }
  /// ordered push
  template <class A> constexpr void push_ordered(A &alloc, T *t) {
    invariant(count <= std::size(data));
    if (!isFull()) {
      data[count++] = t;
      return;
    }
    if (next == nullptr) {
      next = alloc.allocate(1);
      std::construct_at(next, t);
    } else next->push_ordered(alloc, t);
  }
  [[nodiscard]] constexpr auto push(T *t) -> UList * {
    std::allocator<UList<T>> alloc;
    return push(alloc, t);
  }
  /// ordered push
  constexpr void push_ordered(T *t) {
    std::allocator<UList<T>> alloc;
    push_ordered(alloc, t);
  }
  [[nodiscard]] constexpr auto isFull() const -> bool {
    return count == std::size(data);
  }
  constexpr auto getNext() const -> UList * { return next; }
  constexpr void clear() {
    invariant(count <= std::size(data));
    count = 0;
    next = nullptr;
  }
};
