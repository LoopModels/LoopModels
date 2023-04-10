#pragma once
#include "TypePromotion.hpp"
#include <concepts>
#include <cstddef>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>

namespace LinAlg {
template <typename T>
concept AbstractVector = HasEltype<T> && requires(T t, size_t i) {
  { t[i] } -> std::convertible_to<eltype_t<T>>;
  { t.size() } -> std::convertible_to<size_t>;
  { t.view() };
  // {
  //     std::remove_reference_t<T>::canResize
  //     } -> std::same_as<const bool &>;
  // {t.extendOrAssertSize(i)};
};

// This didn't work: #include "Math/Vector.hpp" NOLINT(unused-includes)
// so I moved some code from "Math/Array.hpp" here instead.
template <class T>
concept SizeMultiple8 = (sizeof(T) % 8) == 0;

template <class S> struct default_capacity_type {
  using type = unsigned int;
};
template <SizeMultiple8 S> struct default_capacity_type<S> {
  using type = std::size_t;
};
template <class S>
using default_capacity_type_t = typename default_capacity_type<S>::type;

static_assert(!SizeMultiple8<uint32_t>);
static_assert(SizeMultiple8<uint64_t>);
static_assert(std::is_same_v<default_capacity_type_t<uint32_t>, uint32_t>);
static_assert(std::is_same_v<default_capacity_type_t<uint64_t>, uint64_t>);

template <class T> consteval auto PreAllocStorage() -> size_t {
  constexpr size_t TotalBytes = 128;
  constexpr size_t RemainingBytes =
    TotalBytes - sizeof(llvm::SmallVector<T, 0>);
  constexpr size_t N = RemainingBytes / sizeof(T);
  return std::max<size_t>(1, N);
}

constexpr auto selfDot(const auto &a) {
  eltype_t<decltype(a)> sum = 0;
  for (auto x : a) sum += x * x;
  return sum;
}

} // namespace LinAlg
