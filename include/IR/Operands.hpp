#pragma once

#include "IR/Node.hpp"
#include <Containers/UnrolledList.hpp>

namespace poly::IR {

class Operands {
  containers::UList<Node *> *operands;
};

} // namespace poly::IR
