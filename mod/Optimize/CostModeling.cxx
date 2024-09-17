#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/CaptureTracking.h>
#include <llvm/Analysis/MemoryBuiltins.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Containers/Tuple.cxx"
#include "IR/IR.cxx"
#include "Math/Array.cxx"
#include "Optimize/BBCosts.cxx"
#include "Optimize/CostFunction.cxx"
#include "Optimize/IRGraph.cxx"
#include "Optimize/Legality.cxx"
#include "Target/Machine.cxx"
#else
export module CostModeling;
import Arena;
import Array;
import HeuristicOptimizer;
import IR;
import Legality;
import TargetMachine;
import Tuple;
import :BasicBlock;
import :CostFunction;
#endif
// import BoxOptInt;

using alloc::Arena;
using containers::Tuple;
using poly::Dependence;
#ifdef USE_MODULE
export namespace CostModeling {
#else
namespace CostModeling {
#endif

//
// Considering reordering legality, example
// for (int i = 0: i < I; ++i){
//   for (int j = 0 : j < i; ++j){
//     x[i] -= x[j]*U[j,i];
//   }
//   x[i] /= U[i,i];
// }
// We have an edge from the store `x[i] = x[i] / U[i,i]` to the load of
// `x[j]`, when `j = ` the current `i`, on some future iteration.
// We want to unroll;
// for (int i = 0: i < I-3; i += 4){
//   for (int j = 0 : j < i; ++j){
//     x[i] -= x[j]*U[j,i];
//     x[i+1] -= x[j]*U[j,i+1];
//     x[i+2] -= x[j]*U[j,i+2];
//     x[i+3] -= x[j]*U[j,i+3];
//   }
//   x[i] /= U[i,i]; // store 0
//   { // perform unrolled j = i iter
//     int j = i; // these all depend on store 0
//     x[i+1] -= x[j]*U[j,i+1];
//     x[i+2] -= x[j]*U[j,i+2];
//     x[i+3] -= x[j]*U[j,i+3];
//   }
//   // j+1 iteration for i=i iter goes here (but doesn't happen)
//   x[i+1] /= U[i+1,i+1]; // store 1
//   { // perform unrolled j = i + 1 iter
//     int j = i+1; // these all depend on store 1
//     x[i+2] -= x[j]*U[j,i+2];
//     x[i+3] -= x[j]*U[j,i+3];
//   }
//   // j+2 iteration for i=i iter goes here (but doesn't happen)
//   // j+2 iteration for i=i+1 iter goes here (but doesn't happen)
//   x[i+2] /= U[i+2,i+2]; // store 2
//   { // perform unrolled j = i + 2 iter
//     int j = i+2; // this depends on store 2
//     x[i+3] -= x[j]*U[j,i+3];
//   }
//   // j+3 iteration for i=i iter goes here (but doesn't happen)
//   // j+3 iteration for i=i+1 iter goes here (but doesn't happen)
//   // j+3 iteration for i=i+2 iter goes here (but doesn't happen)
//   x[i+3] /= U[i+3,i+3];
// }
// The key to legality here is that we peel off the dependence polyhedra
// from the loop's iteration space.
// We can then perform the dependent iterations in order.
// With masking, the above code can be vectorized in this manner.
// The basic approach is that we have the dependence polyhedra:
//
// 0 <= i_s < I
// 0 <= i_l < I
// 0 <= j_l < i_l
// i_s = j_l // dependence, yields same address in `x`
//
// Note that our schedule sets
// i_s = i_l
// Which gives:
// i_l = i_s = j_l < i_l
// a contradiction, meaning that the dependency is
// conditionally (on our schedule) satisfied.
// Excluding the `i_s = i_l` constraint from the
// polyhedra gives us the region of overlap.
//
// When unrolling by `U`, we get using `U=4` as an example:
// i^0_s + 1 = i^1_s
// i^0_s + 2 = i^2_s
// i^0_s + 3 = i^3_s
// 0 <= i^0_s < I
// 0 <= i^1_s < I
// 0 <= i^2_s < I
// 0 <= i^3_s < I
// 0 <= i^0_l < I
// 0 <= i^1_l < I
// 0 <= i^2_l < I
// 0 <= i^3_l < I
// 0 <= j_l < i^0_l
// 0 <= j_l < i^1_l
// 0 <= j_l < i^2_l
// 0 <= j_l < i^3_l
// i^0_s = j_l ||  i^1_s = j_l || i^2_s = j_l || i^3_s = j_l
// where the final union can be replaced with
// i^0_s = j_l ||  i^0_s+1 = j_l || i^0_s+2 = j_l || i^0_s+3 = j_l
// i^0_s <= j_1 <= i^0_s+3
//
// Similarly, we can compress the other inequalities...
// 0 <= i^0_s < I - 3
// 0 <= i^0_l < I - 3
// 0 <= j_l < i^0_l
// i^0_s <= j_1 <= i^0_s+3 // dependence region
//
// So, the parallel region is the union
// i^0_s > j_1 || j_1 > i^0_s+3
//
// In this example, note that the region `j_1 > i^0_s+3` is empty
// so we have one parallel region, and then one serial region.
//
// Lets consider simpler checks. We have
// [ 1 0 ] : x[i] -=
// [ 0 1 ] : x[j]
// [ 1 ]   : x[i] /=
// we have a dependency when `i == j`. `i` carries the dependency, but we can
// peel off the independent iters from `j`, and unroll `i` for these.
//
// How to identify:
// [ 1 -1 ]
// vs, if we had two `x[i]` or two `x[j]`
// [ 0, 0 ]
// An idea: look for non-zero so we can peel?
// Or should we look specifically for `x[i] == x[j]` type pattern?
// E.g., if we had
// [ i,  j, k,  l ]
// [ 2, -1, 2, -1 ]
// we'd need a splitting algorithm.
// E.g., split on the 2nd loop, so we get `j == 2*i + 2*k - l`
// With this, we'd split iterations into groups
// j  < 2*i + 2*k - l
// j == 2*i + 2*k - l
// j  > 2*i + 2*k - l
// Subsetting the `k` and `l` iteration spaces may be a little annoying,
// so we may initially want to restrict ourselves to peeling the innermost loop.
///
/// Optimize the schedule
template <bool TTI>
inline auto optimize(Arena<> salloc, IR::Dependencies &deps, IR::Cache &instr,
                     dict::set<llvm::BasicBlock *> &loopBBs,
                     dict::set<llvm::CallBase *> &eraseCandidates,
                     lp::LoopBlock::OptimizationResult res,
                     target::Machine<TTI> target)
  -> Tuple<IR::Loop *, double, math::PtrVector<LoopTransform>> {
  // we must build the IR::Loop
  // Initially, to help, we use a nested vector, so that we can index into it
  // using the fusion omegas. We allocate it with the longer lived `instr`
  // alloc, so we can checkpoint it here, and use alloc for other IR nodes.
  // The `instr` allocator is more generally the longer lived allocator,
  // as it allocates the actual nodes; only here do we use it as short lived.

  auto [root, loopDeps, loop_count] =
    IROptimizer::optimize(salloc, deps, instr, loopBBs, eraseCandidates, res);

  Hard::LoopTreeCostFn fn(&salloc, root, target, loop_count);

  auto [opt, trfs] = fn.optimize();

  return {root, opt, trfs};
}

/*
// NOLINTNEXTLINE(misc-no-recursion)
inline auto printSubDotFile(Arena<> *alloc, llvm::raw_ostream &out,
                          map<LoopTreeSchedule *, std::string> &names,
                          llvm::SmallVectorImpl<std::string> &addrNames,
                          unsigned addrIndOffset, poly::Loop *lret)
-> poly::Loop * {
poly::Loop *loop{nullptr};
size_t j = 0;
for (auto *addr : header.getAddr()) loop = addr->getAffLoop();
for (auto &subTree : subTrees) {
  // `names` might realloc, relocating `names[this]`
  if (getDepth())
    names[subTree.subTree] = names[this] + "SubLoop#" + std::to_string(j++);
  else names[subTree.subTree] = "LoopNest#" + std::to_string(j++);
  if (loop == nullptr)
    for (auto *addr : subTree.exit.getAddr()) loop = addr->getAffLoop();
  loop = subTree.subTree->printSubDotFile(alloc, out, names, addrNames,
                                          addrIndOffset, loop);
}
const std::string &name = names[this];
out << "\"" << name
    << "\" [shape=plain\nlabel = <<table><tr><td port=\"f0\">";
// assert(depth == 0 || (loop != nullptr));
if (loop && (getDepth() > 0)) {
  for (size_t i = loop->getNumLoops(), k = getDepth(); i > k;)
    loop = loop->removeLoop(alloc, --i);
  loop->pruneBounds(alloc);
  loop->printBounds(out);
} else out << "Top Level";
out << "</td></tr>\n";
size_t i = header.printDotNodes(out, 0, addrNames, addrIndOffset, name);
j = 0;
std::string loopEdges;
for (auto &subTree : subTrees) {
  std::string label = "f" + std::to_string(++i);
  out << " <tr> <td port=\"" << label << "\"> SubLoop#" << j++
      << "</td></tr>\n";
  loopEdges += "\"" + name + "\":f" + std::to_string(i) + " -> \"" +
               names[subTree.subTree] + "\":f0 [color=\"#ff0000\"];\n";
  i = subTree.exit.printDotNodes(out, i, addrNames, addrIndOffset, name);
}
out << "</table>>];\n" << loopEdges;
if (lret) return lret;
if ((loop == nullptr) || (getDepth() <= 1)) return nullptr;
return loop->removeLoop(alloc, getDepth() - 1);
}

inline void printDotFile(Arena<> *alloc, llvm::raw_ostream &out) {
map<LoopTreeSchedule *, std::string> names;
llvm::SmallVector<std::string> addrNames(numAddr_);
names[this] = "toplevel";
out << "digraph LoopNest {\n";
auto p = alloc.scope();
printSubDotFile(alloc, out, names, addrNames, subTrees.size(), nullptr);
printDotEdges(out, addrNames);
out << "}\n";
}
*/
// class LoopForestSchedule : LoopTreeSchedule {
//   [[no_unique_address]] Arena<> *allocator;
// };
} // namespace CostModeling
