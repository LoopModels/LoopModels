#pragma once
#include "TypePromotion.hpp"
#include <concepts>
#include <cstddef>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>

namespace LinearAlgebra {
template <typename T>
concept AbstractVector =
  HasEltype<T> && requires(T t, size_t i) {
                    { t[i] } -> std::convertible_to<eltype_t<T>>;
                    { t.size() } -> std::convertible_to<size_t>;
                    { t.view() };
                    // {
                    //     std::remove_reference_t<T>::canResize
                    //     } -> std::same_as<const bool &>;
                    // {t.extendOrAssertSize(i)};
                  };

} // namespace LinearAlgebra
