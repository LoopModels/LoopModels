#pragma once
#include <concepts>
#include <type_traits>

template <typename T>
concept HasEltype = requires(T) {
    std::is_scalar_v<typename std::remove_reference_t<T>::eltype>;
};

template <typename A> struct GetEltype {};
template <HasEltype A> struct GetEltype<A> {
    using eltype = typename A::eltype;
};
template <std::integral A> struct GetEltype<A> { using eltype = A; };
template <std::floating_point A> struct GetEltype<A> { using eltype = A; };

template <typename T>
using eltype_t = typename GetEltype<std::remove_reference_t<T>>::eltype;

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
    using eltype = typename PromoteType<eltype_t<A>, eltype_t<B>>::eltype;
};
template <typename A, typename B>
using promote_eltype_t = typename PromoteEltype<A, B>::eltype;
