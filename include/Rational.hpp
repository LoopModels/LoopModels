#pragma once

#include "./GreatestCommonDivisor.hpp"
#include "./Math.hpp"
#include <cstdint>
#include <llvm/Support/raw_ostream.h>
#include <optional>

template <class T, int Bits>
concept is_int_v = std::signed_integral<T> && sizeof(T) == (Bits / 8);

template <is_int_v<32> T> constexpr auto widen(T x) -> int64_t { return x; }
template <is_int_v<64> T> constexpr auto widen(T x) -> __int128_t { return x; }
template <is_int_v<32> T> constexpr auto splitInt(T x) -> int64_t { return x; }

struct Rational {
  [[no_unique_address]] int64_t numerator{0};
  [[no_unique_address]] int64_t denominator{1};

  constexpr Rational() = default;
  constexpr Rational(int64_t coef) : numerator(coef){};
  constexpr Rational(int coef) : numerator(coef){};
  constexpr Rational(int64_t n, int64_t d)
    : numerator(d > 0 ? n : -n), denominator(n ? (d > 0 ? d : -d) : 1) {}
  constexpr static auto create(int64_t n, int64_t d) -> Rational {
    if (n) {
      int64_t sign = 2 * (d > 0) - 1;
      int64_t g = gcd(n, d);
      n *= sign;
      d *= sign;
      if (g != 1) {
        n /= g;
        d /= g;
      }
      return Rational{n, d};
    } else {
      return Rational{0, 1};
    }
  }
  constexpr static auto createPositiveDenominator(int64_t n, int64_t d)
    -> Rational {
    if (n) {
      int64_t g = gcd(n, d);
      if (g != 1) {
        n /= g;
        d /= g;
      }
      return Rational{n, d};
    } else {
      return Rational{0, 1};
    }
  }

  [[nodiscard]] constexpr auto safeAdd(Rational y) const
    -> std::optional<Rational> {
    auto [xd, yd] = divgcd(denominator, y.denominator);
    int64_t a, b, n, d;
    bool o1 = __builtin_mul_overflow(numerator, yd, &a);
    bool o2 = __builtin_mul_overflow(y.numerator, xd, &b);
    bool o3 = __builtin_mul_overflow(denominator, yd, &d);
    bool o4 = __builtin_add_overflow(a, b, &n);
    if ((o1 | o2) | (o3 | o4)) {
      return {};
    } else if (n) {
      auto [nn, nd] = divgcd(n, d);
      return Rational{nn, nd};
    } else {
      return Rational{0, 1};
    }
  }
  constexpr auto operator+(Rational y) const -> Rational {
    return *safeAdd(y); // NOLINT(bugprone-unchecked-optional-access)
  }
  constexpr auto operator+=(Rational y) -> Rational & {
    std::optional<Rational> a = *this + y;
    assert(a.has_value());
    *this = *a;
    return *this;
  }
  [[nodiscard]] constexpr auto safeSub(Rational y) const
    -> std::optional<Rational> {
    auto [xd, yd] = divgcd(denominator, y.denominator);
    int64_t a, b, n, d;
    bool o1 = __builtin_mul_overflow(numerator, yd, &a);
    bool o2 = __builtin_mul_overflow(y.numerator, xd, &b);
    bool o3 = __builtin_mul_overflow(denominator, yd, &d);
    bool o4 = __builtin_sub_overflow(a, b, &n);
    if ((o1 | o2) | (o3 | o4)) {
      return {};
    } else if (n) {
      auto [nn, nd] = divgcd(n, d);
      return Rational{nn, nd};
    } else {
      return Rational{0, 1};
    }
  }
  constexpr auto operator-(Rational y) const -> Rational {
    return *safeSub(y); // NOLINT(bugprone-unchecked-optional-access)
  }
  constexpr auto operator-=(Rational y) -> Rational & {
    std::optional<Rational> a = *this - y;
    assert(a.has_value());
    *this = *a;
    return *this;
  }
  [[nodiscard]] constexpr auto safeMul(int64_t y) const
    -> std::optional<Rational> {
    auto [xd, yn] = divgcd(denominator, y);
    int64_t n;
    if (__builtin_mul_overflow(numerator, yn, &n))
      return {};
    else
      return Rational{n, xd};
  }
  [[nodiscard]] constexpr auto safeMul(Rational y) const
    -> std::optional<Rational> {
    if ((numerator != 0) & (y.numerator != 0)) {
      auto [xn, yd] = divgcd(numerator, y.denominator);
      auto [xd, yn] = divgcd(denominator, y.numerator);
      int64_t n, d;
      bool o1 = __builtin_mul_overflow(xn, yn, &n);
      bool o2 = __builtin_mul_overflow(xd, yd, &d);
      if (o1 | o2)
        return {};
      else
        return Rational{n, d};
    } else {
      return Rational{0, 1};
    }
  }
  constexpr auto operator*(int64_t y) const -> Rational {
    return *safeMul(y); // NOLINT(bugprone-unchecked-optional-access)
  }
  constexpr auto operator*(Rational y) const -> Rational {
    return *safeMul(y); // NOLINT(bugprone-unchecked-optional-access)
  }
  constexpr auto operator*=(Rational y) -> Rational & {
    if ((numerator != 0) & (y.numerator != 0)) {
      auto [xn, yd] = divgcd(numerator, y.denominator);
      auto [xd, yn] = divgcd(denominator, y.numerator);
      numerator = xn * yn;
      denominator = xd * yd;
    } else {
      numerator = 0;
      denominator = 1;
    }
    return *this;
  }
  [[nodiscard]] constexpr auto inv() const -> Rational {
    if (numerator < 0) {
      // make sure we don't have overflow
      assert(denominator != std::numeric_limits<int64_t>::min());
      return Rational{-denominator, -numerator};
    } else {
      return Rational{denominator, numerator};
    }
    // return Rational{denominator, numerator};
    // bool positive = numerator > 0;
    // return Rational{positive ? denominator : -denominator,
    //                 positive ? numerator : -numerator};
  }
  [[nodiscard]] constexpr auto safeDiv(Rational y) const
    -> std::optional<Rational> {
    return (*this) * y.inv();
  }
  constexpr auto operator/(Rational y) const -> Rational {
    return *safeDiv(y); // NOLINT(bugprone-unchecked-optional-access)
  }
  // *this -= a*b
  constexpr auto fnmadd(Rational a, Rational b) -> bool {
    if (std::optional<Rational> ab = a.safeMul(b)) {
      if (std::optional<Rational> c = safeSub(*ab)) {
        *this = *c;
        return false;
      }
    }
    return true;
  }
  constexpr auto div(Rational a) -> bool {
    if (std::optional<Rational> d = safeDiv(a)) {
      *this = *d;
      return false;
    }
    return true;
  }
  // Rational operator/=(Rational y) { return (*this) *= y.inv(); }
  constexpr operator double() {
    return double(numerator) / double(denominator);
  }

  constexpr auto operator==(Rational y) const -> bool {
    return (numerator == y.numerator) & (denominator == y.denominator);
  }
  constexpr auto operator!=(Rational y) const -> bool {
    return (numerator != y.numerator) | (denominator != y.denominator);
  }
  [[nodiscard]] constexpr auto isEqual(int64_t y) const -> bool {
    if (denominator == 1)
      return (numerator == y);
    else if (denominator == -1)
      return (numerator == -y);
    else
      return false;
  }
  constexpr auto operator==(int y) const -> bool { return isEqual(y); }
  constexpr auto operator==(int64_t y) const -> bool { return isEqual(y); }
  constexpr auto operator!=(int y) const -> bool { return !isEqual(y); }
  constexpr auto operator!=(int64_t y) const -> bool { return !isEqual(y); }
  constexpr auto operator<(Rational y) const -> bool {
    return (widen(numerator) * widen(y.denominator)) <
           (widen(y.numerator) * widen(denominator));
  }
  constexpr auto operator<=(Rational y) const -> bool {
    return (widen(numerator) * widen(y.denominator)) <=
           (widen(y.numerator) * widen(denominator));
  }
  constexpr auto operator>(Rational y) const -> bool {
    return (widen(numerator) * widen(y.denominator)) >
           (widen(y.numerator) * widen(denominator));
  }
  constexpr auto operator>=(Rational y) const -> bool {
    return (widen(numerator) * widen(y.denominator)) >=
           (widen(y.numerator) * widen(denominator));
  }
  constexpr auto operator>=(int y) const -> bool {
    return *this >= Rational(y);
  }

  friend constexpr auto isZero(Rational x) -> bool { return x.numerator == 0; }
  friend constexpr auto isOne(Rational x) -> bool {
    return (x.numerator == x.denominator);
  }
  [[nodiscard]] constexpr auto isInteger() const -> bool {
    return denominator == 1;
  }
  constexpr void negate() { numerator = -numerator; }
  constexpr operator bool() const { return numerator != 0; }

  friend inline auto operator<<(llvm::raw_ostream &os, const Rational &x)
    -> llvm::raw_ostream & {
    os << x.numerator;
    if (x.denominator != 1)
      os << " // " << x.denominator;
    return os;
  }
  void dump() const { llvm::errs() << *this << "\n"; }
};
[[maybe_unused]] inline auto gcd(Rational x, Rational y)
  -> std::optional<Rational> {
  return Rational{gcd(x.numerator, y.numerator),
                  lcm(x.denominator, y.denominator)};
}
[[maybe_unused]] inline auto denomLCM(PtrVector<Rational> x) -> int64_t {
  int64_t l = 1;
  for (auto r : x)
    l = lcm(l, r.denominator);
  return l;
}

static_assert(AbstractVector<PtrVector<Rational>>);
static_assert(AbstractVector<LinearAlgebra::ElementwiseVectorBinaryOp<
                LinearAlgebra::Sub, PtrVector<Rational>, PtrVector<Rational>>>);
