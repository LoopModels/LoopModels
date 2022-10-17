#pragma once
#include <concepts>
#include <type_traits>

// #undef HWY_TARGET_INCLUDE
// #define HWY_TARGET_INCLUDE "./TypePromotion.hpp"
// #include <hwy/foreach_target.h> 
// #include <hwy/highway.h>
// namespace hn = hwy::HWY_NAMESPACE;

// #undef HWY_TARGET_INCLUD
// #define HWY_TARGET_INCLUDE "./TypePromotion.hpp"
// #include <hwy/foreach_target.h>
#if defined(TYPEPROMOTION_H) == defined(HWY_TARGET_TOGGLE)
#ifdef TYPEPROMOTION_H
#undef TYPEPROMOTION_H 
#else
#define TYPEPROMOTION_H 
#endif

#include <hwy/highway.h>
// namespace hn = hwy::HWY_NAMESPACE;

HWY_BEFORE_NAMESPACE();
// namespace project {  // optional
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

struct Rational;
template <typename T>
concept Scalar =
    std::integral<T> || std::floating_point<T> || std::same_as<T, Rational>;

template<typename T>
using eltype_t = typename std::remove_reference_t<T>::eltype;

template <typename T>
concept HasEltype = requires(T) {
    std::is_scalar_v<eltype_t<T>>;
};

template <typename A> struct GetEltype {};
template <HasEltype A> struct GetEltype<A> {
    using eltype = typename A::eltype;
};
template <std::integral A> struct GetEltype<A> { using eltype = A; };
template <std::floating_point A> struct GetEltype<A> { using eltype = A; };

template <typename A, typename B> struct PromoteType {};
template <std::signed_integral A, std::signed_integral B>
struct PromoteType<A, B> {
    using eltype = std::conditional_t<sizeof(A) >= sizeof(B), A, B>;
};
template <std::unsigned_integral A, std::unsigned_integral B>
struct PromoteType<A, B> {
    using eltype = std::conditional_t<sizeof(A) >= sizeof(B), A, B>;
};
template <std::signed_integral A, std::unsigned_integral B>
struct PromoteType<A, B> {
    using eltype = A;
};
template <std::unsigned_integral A, std::signed_integral B>
struct PromoteType<A, B> {
    using eltype = B;
};
template <std::floating_point A, std::integral B> struct PromoteType<A, B> {
    using eltype = A;
};
template <std::integral A, std::floating_point B> struct PromoteType<A, B> {
    using eltype = B;
};
template <typename A, typename B> struct PromoteEltype {
    using eltype = typename PromoteType<typename GetEltype<A>::eltype,
                                        typename GetEltype<B>::eltype>::eltype;
};
template <typename T, typename = void> struct VType;
template <typename T> struct VType<T,std::enable_if_t<std::is_scalar_v<T>>>{
    using type = hn::VFromD<hn::ScalableTag<T>>;
};
template <typename T> struct VType<T,std::enable_if_t<HasEltype<T>>>{
    using type = hn::VFromD<hn::ScalableTag<eltype_t<T>>>;
};
template <typename T> using vtype_t = typename VType<T>::type;

}  // namespace HWY_NAMESPACE
// }  // namespace project - optional
HWY_AFTER_NAMESPACE();

#endif