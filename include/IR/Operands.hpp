#pragma once

#include "IR/Node.hpp"
#include <Containers/UnrolledList.hpp>

namespace poly::IR {

class Operands {
  containers::UList<Node *> *operands;

public:
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return operands == nullptr || operands->empty();
  }
  constexpr auto forEach(const auto &f) const noexcept {
    if (operands != nullptr) operands->forEach(f);
  }
  constexpr auto reduce(auto init, const auto &f) const noexcept {
    return (operands != nullptr) ? operands->reduce(init, f) : init;
  }
  constexpr auto operator==(const Operands &other) const noexcept -> bool {
    return operands == other.operands || ((*operands) == (*other.operands));
  }
};

} // namespace poly::IR
