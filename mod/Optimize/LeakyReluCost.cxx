#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#ifndef USE_MODULE
#include <compare>
#include <type_traits>
#else
export module LeakyReluCost;
import STL;
#endif

#ifndef USE_MODULE
namespace CostModeling {
#else
export namespace CostModeling {
#endif
struct LeakyReluCost {
  static constexpr double a = 0.0625;
  // static constexpr double a = 0.125;
  // constexpr LeakyReluCost() = default;
  constexpr auto operator=(double c) -> LeakyReluCost & {
#ifndef NDEBUG
    if (c < 0.0) __builtin_trap();
#endif
    max_cost_ = c;
    leaky_cost_ = 0.0;
    return *this;
  }
  constexpr auto operator+(double c) const -> LeakyReluCost {
#ifndef NDEBUG
    if (c < 0.0) __builtin_trap();
#endif
    double leaky_cost = (c > max_cost_) ? max_cost_ : c,
           max_cost = (c > max_cost_) ? c : max_cost_;
    return {.max_cost_ = max_cost, .leaky_cost_ = leaky_cost};
  }
  constexpr auto operator+=(double c) -> LeakyReluCost & {
#ifndef NDEBUG
    if (c < 0.0) __builtin_trap();
#endif
    leaky_cost_ += (c > max_cost_) ? max_cost_ : c;
    max_cost_ = (c > max_cost_) ? c : max_cost_;
    return *this;
  }
  constexpr auto operator+(LeakyReluCost c) -> LeakyReluCost {
    double leaky_cost = ((c.max_cost_ > max_cost_) ? max_cost_ : c.max_cost_) +
                        c.leaky_cost_,
           max_cost = (c.max_cost_ > max_cost_) ? c.max_cost_ : max_cost_;
    return {.max_cost_ = max_cost, .leaky_cost_ = leaky_cost};
  }
  constexpr auto operator+=(LeakyReluCost c) -> LeakyReluCost & {
    leaky_cost_ +=
      ((c.max_cost_ > max_cost_) ? max_cost_ : c.max_cost_) + c.leaky_cost_;
    max_cost_ = (c.max_cost_ > max_cost_) ? c.max_cost_ : max_cost_;
    return *this;
  }
  explicit constexpr operator double() const {
    return max_cost_ + (a * leaky_cost_);
  }
  // constexpr auto operator=(const LeakyReluCost&)->LeakyReluCost&=default;
  double max_cost_{0.0}, leaky_cost_{0.0};

private:
  friend constexpr auto operator==(LeakyReluCost x, LeakyReluCost y) -> bool {
    return static_cast<double>(x) == static_cast<double>(y);
  }
  friend constexpr auto operator<=>(LeakyReluCost x, LeakyReluCost y)
    -> std::partial_ordering {
    return static_cast<double>(x) <=> static_cast<double>(y);
  }
  friend constexpr auto operator==(LeakyReluCost x, double y) -> bool {
    return static_cast<double>(x) == y;
  }
  friend constexpr auto operator<=>(LeakyReluCost x, double y)
    -> std::partial_ordering {
    return static_cast<double>(x) <=> y;
  }
  friend constexpr auto operator==(double x, LeakyReluCost y) -> bool {
    return x == static_cast<double>(y);
  }
  friend constexpr auto operator<=>(double x, LeakyReluCost y)
    -> std::partial_ordering {
    return x <=> static_cast<double>(y);
  }
};

} // namespace CostModeling

#ifdef USE_MODULE
export {
#endif
  template <> struct std::common_type<CostModeling::LeakyReluCost, double> {
    using type = CostModeling::LeakyReluCost;
  };
  template <> struct std::common_type<double, CostModeling::LeakyReluCost> {
    using type = CostModeling::LeakyReluCost;
  };
#ifdef USE_MODULE
} // namespace std
#endif

