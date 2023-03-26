#pragma once
#include <concepts>
#include <type_traits>

template <typename T>
concept HasEltype = requires(
  T) { std::is_scalar_v<typename std::remove_reference_t<T>::value_type>; };

template <typename A> struct GetEltype {};
template <HasEltype A> struct GetEltype<A> {
  using value_type = typename A::value_type;
};
template <std::integral A> struct GetEltype<A> {
  using value_type = A;
};
template <std::floating_point A> struct GetEltype<A> {
  using value_type = A;
};

template <typename T>
using eltype_t = typename GetEltype<std::remove_reference_t<T>>::value_type;

template <typename A, typename B> struct PromoteType {};
template <std::signed_integral A, std::signed_integral B>
struct PromoteType<A, B> {
  using value_type = std::conditional_t<sizeof(A) >= sizeof(B), A, B>;
};
template <std::unsigned_integral A, std::unsigned_integral B>
struct PromoteType<A, B> {
  using value_type = std::conditional_t<sizeof(A) >= sizeof(B), A, B>;
};
template <std::signed_integral A, std::unsigned_integral B>
struct PromoteType<A, B> {
  using value_type = A;
};
template <std::unsigned_integral A, std::signed_integral B>
struct PromoteType<A, B> {
  using value_type = B;
};
template <std::floating_point A, std::integral B> struct PromoteType<A, B> {
  using value_type = A;
};
template <std::integral A, std::floating_point B> struct PromoteType<A, B> {
  using value_type = B;
};
template <typename A, typename B>
using promote_type_t = typename PromoteType<A, B>::value_type;

template <typename A, typename B> struct PromoteEltype {
  using value_type = typename PromoteType<eltype_t<A>, eltype_t<B>>::value_type;
};
template <typename A, typename B>
using promote_eltype_t = typename PromoteEltype<A, B>::value_type;

struct Rational;
template <> struct GetEltype<Rational> {
  using value_type = Rational;
};
template <> struct PromoteType<Rational, Rational> {
  using value_type = Rational;
};
template <std::integral I> struct PromoteType<I, Rational> {
  using value_type = Rational;
};
template <std::integral I> struct PromoteType<Rational, I> {
  using value_type = Rational;
};
