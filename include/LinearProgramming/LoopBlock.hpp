#pragma once

#include "Graphs/Graphs.hpp"
#include "IR/Address.hpp"
#include "IR/Cache.hpp"
#include "IR/Node.hpp"
#include "LinearProgramming/ScheduledNode.hpp"
#include "Polyhedra/Dependence.hpp"
#include "Polyhedra/DependencyPolyhedra.hpp"
#include "Polyhedra/Loops.hpp"
#include "Polyhedra/Schedule.hpp"
#include <Containers/BitSets.hpp>
#include <Math/Array.hpp>
#include <Math/Comparisons.hpp>
#include <Math/GreatestCommonDivisor.hpp>
#include <Math/Math.hpp>
#include <Math/NormalForm.hpp>
#include <Math/Simplex.hpp>
#include <Math/StaticArrays.hpp>
#include <Utilities/Allocators.hpp>
#include <Utilities/Invariant.hpp>
#include <Utilities/ListRanges.hpp>
#include <Utilities/Valid.hpp>
#include <algorithm>
#include <bits/ranges_algo.h>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/DiagnosticInfo.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/User.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/raw_ostream.h>
#include <ranges>
#include <type_traits>

namespace poly::lp {
using math::PtrMatrix, math::MutPtrMatrix, math::Vector, math::DenseMatrix,
  math::begin, math::end, math::last, math::Row, math::Col, utils::invariant,
  IR::Dependencies;

struct Result {
  enum { Failure = 0, Dependent = 1, Independent = 3 } Value;

  constexpr explicit operator bool() const { return Value != Failure; }
  constexpr auto operator==(Result r) const -> bool { return Value == r.Value; }
  constexpr auto operator!() const -> bool { return Value == Failure; }
  constexpr auto operator&(Result r) -> Result {
    return Result(static_cast<decltype(Value)>(Value & r.Value));
  }
  constexpr auto operator&=(Result r) -> Result & {
    Value = static_cast<decltype(Value)>(Value & r.Value);
    return *this;
  }
  static constexpr auto failure() -> Result { return Result{Failure}; }
  static constexpr auto dependent() -> Result { return Result{Dependent}; }
  static constexpr auto independent() -> Result { return Result{Independent}; }
};
static_assert(!Result::failure());
static_assert(Result::independent());
static_assert(Result::dependent());
static_assert((Result::dependent() & Result::independent()) ==
              Result::dependent());
static_assert((Result::failure() & Result::independent()) == Result::failure());
static_assert((Result::failure() & Result::dependent()) == Result::failure());

/// A loop block is a block of the program that may include multiple loops.
/// These loops are either all executed (note iteration count may be 0, or
/// loops may be in rotated form and the guard prevents execution; this is okay
/// and counts as executed for our purposes here ), or none of them are.
/// That is, the LoopBlock does not contain divergent control flow, or guards
/// unrelated to loop bounds.
/// The loops within a LoopBlock are optimized together, so we can consider
/// optimizations such as reordering or fusing them together as a set.
///
///
/// Initially, the `LoopBlock` is initialized as a set of
/// `Read` and `Write`s, without any dependence polyhedra.
/// Then, it builds `DependencePolyhedra`.
/// These can be used to construct an ILP.
///
/// That is:
/// fields that must be provided/filled:
///  - refs
///  - memory
///  - userToMemory
/// fields it self-initializes:
///
///
/// NOTE: w/ respect to index linearization (e.g., going from Cartesian indexing
/// to linear indexing), the current behavior will be to fully delinearize as a
/// preprocessing step. Linear indexing may be used later as an optimization.
/// This means that not only do we want to delinearize
/// for (n = 0; n < N; ++n){
///   for (m = 0; m < M; ++m){
///      C(m + n*M)
///   }
/// }
/// we would also want to delinearize
/// for (i = 0; i < M*N; ++i){
///   C(i)
/// }
/// into
/// for (n = 0; n < N; ++n){
///   for (m = 0; m < M; ++m){
///      C(m, n)
///   }
/// }
/// and then relinearize as an optimization later.
/// Then we can compare fully delinearized loop accesses.
/// Should be in same block:
/// s = 0
/// for (i = eachindex(x)){
///   s += x[i]; // Omega = [0, _, 0]
/// }
/// m = s / length(x); // Omega = [1]
/// for (i = eachindex(y)){
///   f(m, ...); // Omega = [2, _, 0]
/// }
class LoopBlock {

  // TODO: figure out how to handle the graph's dependencies based on
  // operation/instruction chains.
  // Perhaps implicitly via the graph when using internal orthogonalization
  // and register tiling methods, and then generate associated constraints
  // or aliasing between schedules when running the ILP solver?
  // E.g., the `dstOmega[numLoopsCommon-1] > srcOmega[numLoopsCommon-1]`,
  // and all other other shared schedule parameters are aliases (i.e.,
  // identical)?
  // Addr *memory{nullptr};
  // dict::map<llvm::User *, Addr *> userToMem{};
  // dict::set<llvm::User *> visited{};
  // llvm::LoopInfo *LI;
  IR::Dependencies deps;
  utils::OwningArena<> allocator{};
  // we may turn off edges because we've exceeded its loop depth
  // or because the dependence has already been satisfied at an
  // earlier level.
  struct CoefCounts {
    unsigned numOmegaCoefs{0};
    unsigned numPhiCoefs{0};
    unsigned numSlack{0};
    unsigned numLambda{0};
    unsigned numBounding{0};
    unsigned numConstraints{0};
    unsigned numActiveEdges{0};
  };

public:
  struct OptimizationResult {
    IR::AddrChain addr;
    ScheduledNode *nodes;
    [[nodiscard]] constexpr auto getVertices() const {
      return nodes->getVertices();
    }
    constexpr auto setOrigNext(ScheduledNode *node) -> OptimizationResult {
      nodes->setOrigNext(node);
      return *this;
    }
  };

  constexpr LoopBlock() = default;
  [[nodiscard]] auto optimize(IR::Cache &cache, IR::TreeResult tr)
    -> OptimizationResult {
    // first, we peel loops for which affine repr failed
    if (unsigned numReject = tr.rejectDepth) {
      auto *SE = cache.getScalarEvolution();
      for (Addr *addr : tr.getAddr())
        addr->peelLoops(cache.getAllocator(), numReject, SE);
    }
    // fill the dependence edges between memory accesses
    for (Addr *stow : tr.getStores()) {
      Addr *next = llvm::cast_or_null<Addr>(stow->getNextAddr());
      for (Addr *other = next; other;
           other = llvm::cast_or_null<Addr>(other->getNextAddr()))
        deps.check(&allocator, stow, other);
    }
    // link stores with loads connected through registers
    ScheduledNode *nodes{nullptr};
    OptimizationResult opt{tr.addr, nodes};
    for (Addr *stow : tr.getStores())
      opt = addScheduledNode(cache, stow, opt.addr).setOrigNext(nodes);
    for (ScheduledNode *node : opt.getVertices()) shiftOmega(node);
    return optOrth(nodes, tr.getMaxDepth()) ? opt : OptimizationResult{};
  }
  void clear() { allocator.reset(); }
  [[nodiscard]] constexpr auto getAllocator() -> Arena<> * {
    return &allocator;
  }

private:
  auto addScheduledNode(IR::Cache &cache, IR::Stow stow, IR::AddrChain addr)
    -> OptimizationResult {
    // how are we going to handle load duplication?
    // we also need to duplicate the instruction graph leading to the node
    // implying we need to track that tree.
    // w = a[i]
    // x = log(w)
    // y = 2*x
    // z = 3*x
    // p = z / 5
    // q = 5 / z
    // s = p - q
    // b[i] = y
    // c[i] = s
    // if adding c[i] after b[i], we must duplicate `w` and `x`
    // but duplicating `z, `p`, `q`, or `s` is unnecessary.
    // We don't need to duplicate those instructions where
    // all uses only lead to `c[i]`.
    // The trick we use is to mark each instruction with
    // the store that visited it.
    // If one has already been visited, duplicate and
    // mark the new one.
    auto [storedVal, maxLoop, ac] =
      searchOperandsForLoads(cache, stow, stow.getStoredVal(), addr);
    maxLoop = deeperLoop(maxLoop, stow.getLoop());
    stow.setVal(storedVal); // in case it changed
    return {ac, ScheduledNode::construct(cache.getAllocator(), stow, maxLoop)};
  }
  static constexpr auto deeperLoop(poly::Loop *a, poly::Loop *b)
    -> poly::Loop * {
    if (!a) return b;
    if (!b) return a;
    return (a->getNumLoops() > b->getNumLoops()) ? a : b;
  }
  /// We search uses of user `u` for any stores so that we can assign the use
  /// and the store the same schedule. This is done because it is assumed the
  /// data is held in registers (or, if things go wrong, spilled to the stack)
  /// in between a load and a store. A complication is that LLVM IR can be
  /// messy, e.g. we may have
  /// %x = load %a
  /// %y = call foo(x)
  /// store %y, %b
  /// %z = call bar(y)
  /// store %z, %c
  /// here, we might lock all three operations
  /// together. However, this limits reordering opportunities; we thus want to
  /// insert a new load instruction so that we have:
  /// %x = load %a
  /// %y = call foo(x)
  /// store %y, %b
  /// %y.reload = load %b
  /// %z = call bar(y.reload)
  /// store %z, %c
  /// and we create a new edge from `store %y, %b` to `load %b`.
  /// We're going to also build up the `Node` graph,
  /// this is to avoid duplicating any logic or needing to traverse the object
  /// graph an extra time as we go.
  ///
  // NOLINTNEXTLINE(misc-no-recursion)
  auto searchOperandsForLoads(IR::Cache &cache, IR::Stow stow, Value *val,
                              IR::AddrChain addr)
    -> std::tuple<Value *, poly::Loop *, IR::AddrChain> {
    auto *inst = llvm::dyn_cast<Instruction>(val);
    if (!inst) return {val, nullptr, addr};
    // we use parent/child relationships here instead of next/prev
    if (Load load = IR::Load(inst)) {
      // TODO: check we don't have mutually exclusive predicates
      // we found a load; first we check if it has already been added
      if (load.getParent() != nullptr) {
        Arena<> *alloc = cache.getAllocator();
        IR::Addr *reload = ((Addr *)load)->reload(alloc);
        deps.copyDependencies(alloc, load, reload);
        invariant(reload->isLoad());
        load = reload;
        addr.addAddr(reload);
      }
      stow.insertAfter(load);
      return {load, load.getLoop(), addr};
      // it has been, therefore we need to copy the load
    }
    // if not a load, check if it is stored, so we reload
    Addr *store{nullptr};
    for (Value *use : inst->getUsers()) {
      if (auto other = IR::Stow(use)) {
        store = other;
        if (other == stow) break; // scan all users
      }
    }
    if (store && (store != (Addr *)stow)) {
      Addr *load = deps.reload(&allocator, store);
      stow.insertAfter(load); // insert load after stow
      addr.addAddr(load);
      return {load, load->getLoop(), addr};
    }
    auto *C = llvm::cast<IR::Compute>(inst);
    // could not find a load, so now we recurse, searching operands
    poly::Loop *maxLoop = nullptr;
    auto s = allocator.scope(); // create temporary
    unsigned numOps = C->getNumOperands();
    MutPtrVector<Value *> newOperands{
      math::vector<Value *>(&allocator, numOps)};
    bool opsChanged = false;
    for (ptrdiff_t i = 0; i < numOps; ++i) {
      Value *op = C->getOperand(i);
      auto [updatedOp, loop, ac] =
        searchOperandsForLoads(cache, stow, op, addr);
      addr = ac;
      maxLoop = deeperLoop(maxLoop, loop);
      if (op != updatedOp) opsChanged = true;
      newOperands[i] = updatedOp;
    }
    if (opsChanged) val = cache.similarCompute(C, newOperands);
    return {val, maxLoop, addr};
  }

  void shiftOmega(ScheduledNode *node) {
    unsigned nLoops = node->getNumLoops();
    if (nLoops == 0) return;
    auto p0 = allocator.checkpoint();
    MutPtrVector<int64_t> offs = math::vector<int64_t>(&allocator, nLoops);
    auto p1 = allocator.checkpoint();
    MutSquarePtrMatrix<int64_t> A =
      math::matrix<int64_t>(&allocator, nLoops + 1);
    // BumpPtrVector<std::pair<BitSet64, int64_t>> omegaOffsets{allocator};
    // // we check all memory accesses in the node, to see if applying the same
    // omega offsets can zero dependence offsets. If so, we apply the shift.
    // we look for offsets, then try and validate that the shift
    // if not valid, we drop it from the potential candidates.
    bool foundNonZeroOffset = false;
    unsigned rank = 0, L = nLoops - 1;
    for (ScheduledNode *n = node; n; n = n->getNext()) {
      for (Addr *m = n->getStore(); m;
           m = llvm::cast_or_null<Addr>(m->getChild())) {
        for (Dependence dep : m->inputEdges(deps)) {
          const DepPoly *depPoly = dep.getDepPoly();
          unsigned numSyms = depPoly->getNumSymbols(),
                   dep0 = depPoly->getDim0(), dep1 = depPoly->getDim1();
          PtrMatrix<int64_t> E = depPoly->getE();
          if (dep.input()->getNode() == n) {
            // dep within node
            unsigned depCommon = std::min(dep0, dep1),
                     depMax = std::max(dep0, dep1);
            invariant(nLoops >= depMax);
            // input and output, no relative shift of shared loops possible
            // but indices may of course differ.
            for (ptrdiff_t d = 0; d < E.numRow(); ++d) {
              MutPtrVector<int64_t> x = A(rank, _);
              x[last] = E(d, 0);
              foundNonZeroOffset |= x[last] != 0;
              ptrdiff_t j = 0;
              for (; j < depCommon; ++j)
                x[L - j] = E(d, j + numSyms) + E(d, j + numSyms + dep0);
              if (dep0 != dep1) {
                ptrdiff_t offset = dep0 > dep1 ? numSyms : numSyms + dep0;
                for (; j < depMax; ++j) x[L - j] = E(d, j + offset);
              }
              for (; j < nLoops; ++j) x[L - j] = 0;
              rank = math::NormalForm::updateForNewRow(A(_(0, rank + 1), _));
            }
          } else {
            // dep between nodes
            // is forward means other -> mem, else mem <- other
            unsigned offset = dep.isForward() ? numSyms + dep0 : numSyms,
                     numDep = dep.isForward() ? dep1 : dep0;
            for (ptrdiff_t d = 0; d < E.numRow(); ++d) {
              MutPtrVector<int64_t> x = A(rank, _);
              x[last] = E(d, 0);
              foundNonZeroOffset |= x[last] != 0;
              ptrdiff_t j = 0;
              for (; j < numDep; ++j) x[L - j] = E(d, j + offset);
              for (; j < nLoops; ++j) x[L - j] = 0;
              rank = math::NormalForm::updateForNewRow(A(_(0, rank + 1), _));
            }
          }
        }
        for (Dependence dep : m->outputEdges(deps)) {
          if (dep.output()->getNode() == n) continue;
          const DepPoly *depPoly = dep.getDepPoly();
          unsigned numSyms = depPoly->getNumSymbols(),
                   dep0 = depPoly->getDim0(), dep1 = depPoly->getDim1();
          PtrMatrix<int64_t> E = depPoly->getE();
          // is forward means mem -> other, else other <- mem
          unsigned offset = dep.isForward() ? numSyms : numSyms + dep0,
                   numDep = dep.isForward() ? dep0 : dep1;
          for (ptrdiff_t d = 0; d < E.numRow(); ++d) {
            MutPtrVector<int64_t> x = A(rank, _);
            x[last] = E(d, 0);
            foundNonZeroOffset |= x[last] != 0;
            ptrdiff_t j = 0;
            for (; j < numDep; ++j) x[L - j] = E(d, j + offset);
            for (; j < nLoops; ++j) x[L - j] = 0;
            rank = math::NormalForm::updateForNewRow(A(_(0, rank + 1), _));
          }
        }
      }
    }
    if (!foundNonZeroOffset) return allocator.rollback(p0);
    bool nonZero = false;
    // matrix A is reasonably diagonalized, should indicate
    ptrdiff_t c = 0;
    for (ptrdiff_t r = 0; r < rank; ++r) {
      int64_t off = A(r, last);
      if (off == 0) continue;
      for (; c < nLoops; ++c) {
        if (A(r, c) != 0) break;
        offs[L - c] = 0;
      }
      if (c == nLoops) return;
      int64_t Arc = A(r, c), x = off / Arc;
      if (x * Arc != off) continue;
      offs[L - c++] = x; // decrement loop `L-c` by `x`
      nonZero = true;
    }
    if (!nonZero) return allocator.rollback(p0);
    allocator.rollback(p1);
    for (; c < nLoops; ++c) offs[L - c] = 0;
    node->setOffsets(offs.data());
    // now we iterate over the edges again
    // perhaps this should be abstracted into higher order functions that
    // iterate over the edges?
    for (ScheduledNode *n = node; n; n = n->getNext()) {
      for (Addr *m = n->getStore(); m;
           m = llvm::cast_or_null<Addr>(m->getChild())) {
        for (Dependence d : m->inputEdges(deps)) {
          d.copySimplices(&allocator); // in case it is aliased
          DepPoly *depPoly = d.getDepPoly();
          unsigned numSyms = depPoly->getNumSymbols(),
                   dep0 = depPoly->getDim0(), dep1 = depPoly->getDim1();
          MutPtrMatrix<int64_t> satL = d.getSatLambda();
          MutPtrMatrix<int64_t> bndL = d.getBndLambda();
          bool pick = d.isForward(), repeat = d.input()->getNode() == n;
          while (true) {
            unsigned offset = pick ? numSyms + dep0 : numSyms,
                     numDep = pick ? dep1 : dep0;
            for (ptrdiff_t l = 0; l < numDep; ++l) {
              int64_t mlt = offs[l];
              if (mlt == 0) continue;
              satL(0, _) -= mlt * satL(offset + l, _);
              bndL(0, _) -= mlt * bndL(offset + l, _);
            }
            if (!repeat) break;
            repeat = false;
            pick = !pick;
          }
        }
        for (Dependence d : m->outputEdges(deps)) {
          if (d.output()->getNode() == n) continue; // handled above
          d.copySimplices(&allocator); // we don't want to copy twice
          DepPoly *depPoly = d.getDepPoly();
          unsigned numSyms = depPoly->getNumSymbols(),
                   dep0 = depPoly->getDim0(), dep1 = depPoly->getDim1();
          MutPtrMatrix<int64_t> satL = d.getSatLambda();
          MutPtrMatrix<int64_t> bndL = d.getBndLambda();
          unsigned offset = d.isForward() ? numSyms : numSyms + dep0,
                   numDep = d.isForward() ? dep0 : dep1;
          for (size_t l = 0; l < numDep; ++l) {
            int64_t mlt = offs[l];
            if (mlt == 0) continue;
            satL(0, _) -= mlt * satL(offset + l, _);
            bndL(0, _) -= mlt * bndL(offset + l, _);
          }
        }
      }
    }
  }
  // returns a `1` for each level containing a dependency
  auto optOrth(ScheduledNode *nodes, unsigned maxDepth) -> Result {
    // check for orthogonalization opportunities
    bool tryOrth = false;
    for (ScheduledNode *node : nodes->getVertices()) {
      for (Dependence edge : node->inputEdges(deps)) {
        // this edge's output is `node`
        // we want edges whose input is also `node`,
        // i.e. edges that are within the node
        if (edge.input()->getNode() != node) continue;
        DensePtrMatrix<int64_t> indMat = edge.getInIndMat();
        // check that we haven't already scheduled on an earlier
        // iteration of this loop, and that the indmats are the same
        if (node->phiIsScheduled(0) || (indMat != edge.getOutIndMat()))
          continue;
        ptrdiff_t r = math::NormalForm::rank(indMat);
        if (r == edge.getInCurrentDepth()) continue;
        // TODO handle linearly dependent acceses, filtering them out
        if (r != ptrdiff_t(indMat.numRow())) continue;
        node->schedulePhi(indMat, r);
        tryOrth = true;
      }
    }
    if (tryOrth) {
      if (Result r = optimize(nodes, 0, maxDepth)) return r;
      for (ScheduledNode *n : nodes->getVertices()) n->unschedulePhi();
    }
    return optimize(nodes, 0, maxDepth);
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  static constexpr auto numParams(Dependence edge)
    -> math::SVector<unsigned, 4> {
    return math::SVector<unsigned, 4>{edge.getNumLambda(), edge.getDynSymDim(),
                                      edge.getNumConstraints(), 1};
  }
  static constexpr auto countAuxParamsAndConstraints(IR::Dependencies deps,
                                                     ScheduledNode *nodes,
                                                     unsigned depth)
    -> math::SVector<unsigned, 4> {
    math::SVector<unsigned, 4> params{};
    assert(allZero(params));
    for (ScheduledNode *node : nodes->getVertices())
      for (Dependence d : node->inputEdges(deps))
        if (d.isActive(depth)) params += numParams(d);
    return params;
  }
  static constexpr auto countAuxAndStash(IR::Dependencies deps,
                                         ScheduledNode *nodes, unsigned depth)
    -> math::SVector<unsigned, 4> {
    math::SVector<unsigned, 4> params{};
    assert(allZero(params));
    for (ScheduledNode *node : nodes->getVertices())
      for (Dependence d : node->inputEdges(deps))
        if (d.isActive(depth)) params += numParams(d.stashSatLevel(depth));
    return params;
  }
  static constexpr auto
  setScheduleMemoryOffsets(Dependencies deps, ScheduledNode *nodes, unsigned d)
    -> std::array<unsigned, 3> {
    // C, lambdas, omegas, Phis
    unsigned numOmegaCoefs = 0, numPhiCoefs = 0, numSlack = 0;
    for (ScheduledNode *node : nodes->getVertices()) {
      // note, we had d > node->getNumLoops() for omegas earlier; why?
      // TODO: audit edge-sat and skip checks; e.g. are edges being deactivated
      // when depth exceeds the number of loops?
      if ((d >= node->getNumLoops()) || (!node->hasActiveEdges(deps, d)))
        continue;
      numOmegaCoefs = node->updateOmegaOffset(numOmegaCoefs);
      if (node->phiIsScheduled(d)) continue;
      numPhiCoefs = node->updatePhiOffset(numPhiCoefs);
      ++numSlack;
    }
    return {numOmegaCoefs, numPhiCoefs, numSlack};
  }
  static constexpr auto calcCoefs(Dependencies deps, ScheduledNode *nodes,
                                  unsigned d) -> CoefCounts {
    auto [numOmegaCoefs, numPhiCoefs, numSlack] =
      setScheduleMemoryOffsets(deps, nodes, d);
    auto [numLambda, numBounding, numConstraints, numActiveEdges] =
      countAuxParamsAndConstraints(deps, nodes, d);
    return {numOmegaCoefs, numPhiCoefs,    numSlack,      numLambda,
            numBounding,   numConstraints, numActiveEdges};
  }
  static constexpr auto calcCoefsStash(Dependencies deps, ScheduledNode *nodes,
                                       unsigned d) -> CoefCounts {
    auto [numOmegaCoefs, numPhiCoefs, numSlack] =
      setScheduleMemoryOffsets(deps, nodes, d);
    auto [numLambda, numBounding, numConstraints, numActiveEdges] =
      countAuxAndStash(deps, nodes, d);
    return {numOmegaCoefs, numPhiCoefs,    numSlack,      numLambda,
            numBounding,   numConstraints, numActiveEdges};
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto optimize(ScheduledNode *nodes, unsigned d,
                              unsigned maxDepth) -> Result {
    if (d >= maxDepth) return Result::independent();
    if (Result r = solveGraph(nodes, maxDepth, false)) {
      unsigned descend = d + 1;
      if (descend == maxDepth) return r;
      if (Result n = optimize(nodes, descend, maxDepth)) {
        if ((r == Result::dependent()) &&
            (n == Result::dependent())) // try and sat all this level
          return optimizeSatDep(nodes, d, maxDepth, r & n);
        return r & n;
      }
    }
    return breakGraph(nodes, d);
  }
  /// solveGraph(ScheduledNode *nodes, unsigned depth, bool satisfyDeps)
  /// solve the `nodes` graph at depth `d`
  /// if `satisfyDeps` is true, then we are trying to satisfy dependencies at
  /// this level
  ///
  [[nodiscard]] auto solveGraph(ScheduledNode *nodes, unsigned depth,
                                bool satisfyDeps) -> Result {
    CoefCounts counts{calcCoefs(deps, nodes, depth)};
    return solveGraph(nodes, depth, satisfyDeps, counts);
  }
  [[nodiscard]] auto solveGraph(ScheduledNode *nodes, unsigned depth,
                                bool satisfyDeps, CoefCounts counts) -> Result {
    if (counts.numLambda == 0) {
      setSchedulesIndependent(nodes, depth);
      return checkEmptySatEdges(nodes, depth);
    }
    // TODO: sat Deps should check which stashed ones to satisfy
    // use `edge->isCondIndep()`/`edge->preventsReodering()` to check
    // which edges should be satisfied on this level if `satisfyDeps`
    auto omniSimplex =
      instantiateOmniSimplex(nodes, depth, satisfyDeps, counts);
    if (omniSimplex->initiateFeasible()) return {};
    auto sol = omniSimplex->rLexMinStop(counts.numLambda + counts.numSlack);
    assert(sol.size() == counts.numBounding + counts.numActiveEdges +
                           counts.numPhiCoefs + counts.numOmegaCoefs);
    updateSchedules(nodes, depth, counts, sol);
    return deactivateSatisfiedEdges(
      nodes, depth, counts,
      sol[_(counts.numPhiCoefs + counts.numOmegaCoefs, end)]);
  }
  void setSchedulesIndependent(ScheduledNode *nodes, unsigned depth) {
    // IntMatrix A, N;
    for (ScheduledNode *node : nodes->getVertices()) {
      if ((depth >= node->getNumLoops()) || node->phiIsScheduled(depth))
        continue;
      assert(!node->hasActiveEdges(deps, depth)); //  numLambda==0
      setDepFreeSchedule(node, depth);
    }
  }
  static void setDepFreeSchedule(ScheduledNode *node, unsigned depth) {
    node->getOffsetOmega(depth) = 0;
    if (node->phiIsScheduled(depth)) return;
    // we'll check the null space of the phi's so far
    // and then search for array indices
    if (depth == 0) {
      // for now, if depth == 0, we just set last active
      MutPtrVector<int64_t> phiv{node->getSchedule(0)};
      phiv[_(0, last)] << 0;
      phiv[last] = 1;
      return;
    }
    // auto s = allocator->scope(); // TODO: use bumpalloc
    DenseMatrix<int64_t> nullSpace; // d x lfull
    DenseMatrix<int64_t> A{node->getPhi()(_(0, depth), _).transpose()};
    math::NormalForm::nullSpace11(nullSpace, A);
    invariant(unsigned(nullSpace.numRow()), node->getNumLoops() - depth);
    // Now, we search index matrices for schedules not in the null space of
    // existing phi. This is because we're looking to orthogonalize a
    // memory access if possible, rather than setting a schedule
    // arbitrarily.
    // Here, we collect candidates for the next schedule
    DenseMatrix<int64_t> candidates{
      math::DenseDims{0, node->getNumLoops() + 1}};
    Vector<int64_t> indv;
    indv.resizeForOverwrite(node->getNumLoops());
    for (Addr *mem : node->localAddr()) {
      PtrMatrix<int64_t> indMat = mem->indexMatrix(); // lsub x d
      A.resizeForOverwrite(
        math::DenseDims{nullSpace.numRow(), indMat.numCol()});
      A = nullSpace(_, _(0, indMat.numRow())) * indMat;
      // we search A for rows that aren't all zero
      for (ptrdiff_t d = 0; d < A.numCol(); ++d) {
        if (allZero(A(_, d))) continue;
        indv << indMat(_, d);
        bool found = false;
        for (ptrdiff_t j = 0; j < candidates.numRow(); ++j) {
          if (candidates(j, _(0, last)) != indv) continue;
          found = true;
          ++candidates(j, 0);
          break;
        }
        if (!found) {
          candidates.resize(candidates.numRow() + 1);
          assert(candidates(last, 0) == 0);
          candidates(last, _(1, end)) << indv;
        }
      }
    }
    if (Row R = candidates.numRow()) {
      // >= 1 candidates, pick the one with with greatest lex, favoring
      // number of repetitions (which were placed in first index)
      ptrdiff_t i = 0;
      for (ptrdiff_t j = 1; j < candidates.numRow(); ++j)
        if (candidates(j, _) > candidates(i, _)) i = j;
      node->getSchedule(depth) << candidates(i, _(1, end));
      return;
    }
    // do we want to pick the outermost original loop,
    // or do we want to pick the outermost lex null space?
    node->getSchedule(depth) << 0;
    for (ptrdiff_t c = 0; c < nullSpace.numCol(); ++c) {
      if (allZero(nullSpace(_, c))) continue;
      node->getSchedule(depth)[c] = 1;
      return;
    }
    invariant(false);
  }
  void updateSchedules(ScheduledNode *nodes, unsigned depth, CoefCounts counts,
                       Simplex::Solution sol) {
#ifndef NDEBUG
    if (counts.numPhiCoefs > 0)
      assert(std::ranges::any_of(
        sol, [](math::Rational s) -> bool { return s != 0; }));
#endif
    unsigned o = counts.numOmegaCoefs;
    for (ScheduledNode *node : nodes->getVertices()) {
      if (depth >= node->getNumLoops()) continue;
      if (!node->hasActiveEdges(deps, depth)) {
        setDepFreeSchedule(node, depth);
        continue;
      }
      math::Rational sOmega = sol[node->getOmegaOffset()];
      // TODO: handle s.denominator != 1
      if (!node->phiIsScheduled(depth)) {
        auto phi = node->getSchedule(depth);
        auto s = sol[node->getPhiOffsetRange() + o];
        int64_t baseDenom = sOmega.denominator;
        int64_t l = math::lcm(s.denomLCM(), baseDenom);
#ifndef NDEBUG
        for (ptrdiff_t i = 0; i < phi.size(); ++i)
          assert(((s[i].numerator * l) / (s[i].denominator)) >= 0);
#endif
        if (l == 1) {
          node->getOffsetOmega(depth) = sOmega.numerator;
          for (ptrdiff_t i = 0; i < phi.size(); ++i) phi[i] = s[i].numerator;
        } else {
          node->getOffsetOmega(depth) = (sOmega.numerator * l) / baseDenom;
          for (ptrdiff_t i = 0; i < phi.size(); ++i)
            phi[i] = (s[i].numerator * l) / (s[i].denominator);
        }
        assert(!(allZero(phi)));
      } else {
        node->getOffsetOmega(depth) = sOmega.numerator;
      }
#ifndef NDEBUG
      if (!node->phiIsScheduled(depth)) {
        int64_t l = sol[node->getPhiOffsetRange() + o].denomLCM();
        for (ptrdiff_t i = 0; i < node->getPhi().numCol(); ++i)
          assert(node->getPhi()(depth, i) ==
                 sol[node->getPhiOffsetRange() + o][i] * l);
      }
#endif
    }
  }
  [[nodiscard]] auto deactivateSatisfiedEdges(ScheduledNode *nodes,
                                              unsigned depth, CoefCounts counts,
                                              Simplex::Solution sol) -> Result {
    if (allZero(sol[_(begin, counts.numBounding + counts.numActiveEdges)]))
      return checkEmptySatEdges(nodes, depth);
    ptrdiff_t w = 0, u = counts.numActiveEdges;
    // TODO: update the deactivated edge handling
    // must consider it w/ respect to `optimize`, `breakGraph`, and
    // `optimizeSatDep`
    // We want an indicator of which edges to try and eagerly satisfy
    // `optimizeSatDep`; this ought to just be the `edge->satLevel()`
    // so the flags we return here only need to be an indicator
    // of whether we had to deactivate an edge on level `depth`.
    //
    // We don't set `deactivated=1 for `checkEmptySat` as we still have `w == 0`
    // and `u == 0`, meaning it is still a parallizable loop -- so we haven't
    // lost anything! The idea of trying to consolidate dependencies into one
    // loop is, if we must already execute this loop in order, we should try and
    // cover as many dependencies at that time as possible.
    Result result{Result::Independent};
    for (ScheduledNode *outNode : nodes->getVertices()) {
      for (Dependence edge : outNode->inputEdges(deps)) {
        if (edge.isInactive(depth)) continue;
        Col uu = u + edge.getNumDynamicBoundingVar();
        if ((sol[w++] != 0) || (anyNEZero(sol[_(u, uu)]))) {
          edge.setSatLevelLP(depth);
          result = Result::dependent();
        } else {
          ScheduledNode *inNode = edge.input()->getNode();
          DensePtrMatrix<int64_t> inPhi = inNode->getPhi()(_(0, depth + 1), _),
                                  outPhi =
                                    outNode->getPhi()(_(0, depth + 1), _);
          edge.checkEmptySat(&allocator, inNode->getLoopNest(),
                             inNode->getOffset(), inPhi, outNode->getLoopNest(),
                             outNode->getOffset(), outPhi);
        }
        u = ptrdiff_t(uu);
      }
    }
    return result;
  }
  auto checkEmptySatEdges(ScheduledNode *nodes, unsigned depth) -> Result {
    for (ScheduledNode *outNode : nodes->getVertices()) {
      for (Dependence edge : outNode->inputEdges(deps)) {
        if (edge.isSat(depth)) continue;
        ScheduledNode *inNode = edge.input()->getNode();
        invariant(edge.output()->getNode(), outNode);
        DensePtrMatrix<int64_t> inPhi = inNode->getPhi()(_(0, depth + 1), _),
                                outPhi = outNode->getPhi()(_(0, depth + 1), _);
        edge.checkEmptySat(&allocator, inNode->getLoopNest(),
                           inNode->getOffset(), inPhi, outNode->getLoopNest(),
                           outNode->getOffset(), outPhi);
      }
    }
    return Result::independent();
  }
  /// What happens to our ScheduledNode linked list after `breakGraph`?
  /// N0 -> N1 -> N2 -> N3 -> N4 -> N5
  /// may become
  /// N2 -> N1
  /// N4 -> N3 -> N5
  /// N0
  /// where `components` pointer points N2 -> N4 -> N0 -> `nullptr`
  /// Our original `ScheduledNode` head, `N0` thereby loses its connection.
  /// Another level of recursion may then split N4, N3, and N5, so their
  /// components link. Now `N4` doesn't point to `N0` anymore, either, but `N3`.
  /// For this reason, we cache temporary info in `Backup`
  /// And additionally, the `ScheduledNode`s hold their `originalNext`.
  using BackupSchedule =
    math::ResizeableView<std::pair<poly::AffineSchedule, ScheduledNode *>,
                         unsigned>;
  using BackupSat = math::ResizeableView<std::array<uint8_t, 2>, unsigned>;
  using Backup = std::pair<BackupSchedule, BackupSat>;
  auto stashFitCore(ScheduledNode *nodes) -> Backup {
    BackupSchedule old{&allocator, 0, 8};
    BackupSat sat{&allocator, 0, 32};
    for (ScheduledNode *node : nodes->getVertices()) {
      old.emplace_backa(&allocator, node->getSchedule().copy(&allocator), node);
      for (int32_t dID : node->outputEdgeIds(deps)) {
        std::array<uint8_t, 2> &stash = deps.satLevelPair(Dependence::ID{dID});
        sat.emplace_backa(&allocator, stash);
        stash[1] = stash[0];
        stash[0] = std::numeric_limits<uint8_t>::max();
      }
    }
    return {old, sat};
  }
  auto stashFit(ScheduledNode *nodes, unsigned depth)
    -> std::pair<Backup, CoefCounts> {
    return {stashFitCore(nodes), calcCoefsStash(deps, nodes, depth)};
  }
  void popStash(Backup backup) {
    // reconnect nodes, in case they became disconnected in breakGraph
    // because we go in reverse order, connections should be the same
    // so the original `nodes` should be restored.
    ScheduledNode *n = nullptr;
    BackupSchedule old{backup.first};
    for (auto &it : std::ranges::reverse_view(old)) {
      n = it.second->setNext(n);
      n->getSchedule() << it.first; // copy over
    }
    BackupSat sat{backup.second};
    ptrdiff_t i = 0;
    for (ScheduledNode *node : n->getVertices())
      for (int32_t dID : node->outputEdgeIds(deps))
        deps.satLevelPair(Dependence::ID{dID}) = sat[i++];
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto optimizeSatDep(ScheduledNode *nodes, unsigned depth,
                                    unsigned maxDepth, Result backupResult)
    -> Result {
    // if we're here, there are satisfied deps in both
    // depSatLevel and depSatNest
    // what we want to know is, can we satisfy all the deps
    // in depSatNest?
    // backup in case we fail
    // activeEdges was the old original; swap it in
    // we don't create long lasting allocations
    auto scope = allocator.scope();
    auto [old, counts] = stashFit(nodes, depth);
    if (Result depSat = solveGraph(nodes, depth, true, counts))
      if (Result depSatN = optimize(nodes, depth + 1, maxDepth))
        return depSat & depSatN;
    popStash(old);
    return backupResult;
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto tryFuse(ScheduledNode *n0, ScheduledNode *n1, unsigned depth) -> Result {
    auto s = allocator.scope();
    auto old0 = stashFitCore(n0); // FIXME: stash dep sat level
    auto old1 = stashFitCore(n1); // FIXME: stash dep sat level
    ScheduledNode *n = n0->fuse(n1);
    if (Result depSat = solveSplitGraph(n, depth))
      if (Result depSatN = optimize(n, depth + 1, depth + 1))
        return depSat & depSatN;
    popStash(old0);
    popStash(old1);
    return Result::failure();
  }
  auto satisfySplitEdges(ScheduledNode *nodes, unsigned depth) -> Result {
    auto s = allocator.scope();
    dict::aset<ScheduledNode *> graph{&allocator};
    for (ScheduledNode *node : nodes->getVertices()) graph.insert(node);
    bool found = false;
    for (ScheduledNode *node : nodes->getVertices()) {
      for (Dependence edge : node->inputEdges(deps)) {
        if (!graph.count(edge.input()->getNode())) {
          edge.setSatLevelParallel(depth);
          found = true;
        }
      }
    }
    return (found) ? Result::dependent() : Result::independent();
  }
  auto solveSplitGraph(ScheduledNode *nodes, unsigned depth) -> Result {
    Result sat = satisfySplitEdges(nodes, depth);
    Result opt = solveGraph(nodes, depth, false, calcCoefs(deps, nodes, depth));
    if (!opt) return opt;
    return opt & sat;
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  [[nodiscard]] auto breakGraph(ScheduledNode *node, unsigned d) -> Result {
    // Get a top sorting of SCC's; because we couldn't solve the graph
    // with these dependencies fused, we'll try splitting them.
    ScheduledNode *components =
      graph::stronglyConnectedComponents(ScheduleGraph(d), node);
    if (components->getNextComponent() == nullptr) return {};
    // components are sorted in topological order.
    // We split all of them, solve independently,
    // and then try to fuse again after if/where optimal schedules
    // allow it.
    Result res{Result::Independent};
    for (auto *g : components->getComponents())
      if (Result sat = solveSplitGraph(g, d)) res &= sat;
      else return Result::failure();
    // We find we can successfully solve by splitting all legal splits.
    // Next, we want to try and re-fuse as many as we can.
    // We could try and implement a better algorithm in the future, but for now
    // we take a single greedy pass over the components.
    // On each iteration, we either fuse our seed with the current component, or
    // swap them. Thus, on each iteration, we're trying to merge each component
    // with the topologically previous one (and those that one has fused with).
    auto range = components->getComponents();
    auto it = range.begin();
    ScheduledNode *seed = *it;
    int64_t unfusedOffset = 0;
    for (auto e = decltype(range)::end(); ++it != e;) {
      if (auto opt = tryFuse(seed, *it, d)) res &= opt;
      else {
        for (ScheduledNode *v : seed->getVertices())
          v->getFusionOmega(d) = unfusedOffset;
        ++unfusedOffset;
      }
      seed = *it; // if fused, seed was appended to `*it`
    }
    for (ScheduledNode *v : seed->getVertices())
      v->getFusionOmega(d) = unfusedOffset;
    return res;
  }
  /// For now, we instantiate a dense simplex specifying the full problem.
  ///
  /// Eventually, the plan is to generally avoid instantiating the
  /// omni-simplex first, we solve individual problems
  ///
  /// The order of variables in the simplex is:
  /// C, lambdas, slack, omegas, Phis, w, u
  /// where
  /// C: constraints, rest of matrix * variables == C
  /// lambdas: farkas multipliers
  /// slack: slack variables from independent phi solution constraints
  /// omegas: scheduling offsets
  /// Phis: scheduling rotations
  /// w: bounding offsets, independent of symbolic variables
  /// u: bounding offsets, dependent on symbolic variables
  auto instantiateOmniSimplex(ScheduledNode *nodes, unsigned d,
                              bool satisfyDeps, CoefCounts counts)
    -> std::unique_ptr<Simplex> {
    auto [numOmegaCoefs, numPhiCoefs, numSlack, numLambda, numBounding,
          numConstraints, numActiveEdges] = counts;
    auto omniSimplex = Simplex::create(
      numConstraints + numSlack, numBounding + numActiveEdges + numPhiCoefs +
                                   numOmegaCoefs + numSlack + numLambda);
    auto C{omniSimplex->getConstraints()};
    C << 0;
    // layout of omniSimplex:
    // Order: C, then rev-priority to minimize
    // C, lambdas, slack, omegas, Phis, w, u
    // rows give constraints; each edge gets its own
    // numBounding = num u
    // numActiveEdges = num w
    Row c = 0;
    Col l = 1, o = 1 + numLambda + numSlack, p = o + numOmegaCoefs,
        w = p + numPhiCoefs, u = w + numActiveEdges;
    for (ScheduledNode *inNode : nodes->getVertices()) {
      for (Dependence edge : inNode->outputEdges(deps, d)) {
        ScheduledNode *outNode = edge.output()->getNode();
        const auto [satPp, satPc] = edge.satPhiCoefs();
        const auto [bndPp, bndPc] = edge.bndPhiCoefs();
        math::StridedVector<int64_t> satC{edge.getSatConstants()},
          satW{edge.getSatW()}, bndC{edge.getBndConstants()};
        math::PtrMatrix<int64_t> satL{edge.getSatLambda()},
          bndL{edge.getBndLambda()}, satO{edge.getSatOmegaCoefs()},
          bndO{edge.getBndOmegaCoefs()}, bndWU{edge.getBndCoefs()};
        const ptrdiff_t numSatConstraints = satC.size(),
                        numBndConstraints = bndC.size();
        const Col nPc = satPc.numCol(), nPp = satPp.numCol();
        invariant(nPc, bndPc.numCol());
        invariant(nPp, bndPp.numCol());
        Row cc = c + numSatConstraints;
        Row ccc = cc + numBndConstraints;

        Col ll = l + satL.numCol();
        Col lll = ll + bndL.numCol();
        C(_(c, cc), _(l, ll)) << satL;
        C(_(cc, ccc), _(ll, lll)) << bndL;
        l = lll;
        // bounding
        C(_(cc, ccc), w++) << bndWU(_, 0);
        Col uu = u + bndWU.numCol() - 1;
        C(_(cc, ccc), _(u, uu)) << bndWU(_, _(1, end));
        u = uu;
        if (!satisfyDeps || !edge.stashedPreventsReordering(d))
          C(_(c, cc), 0) << satC;
        else C(_(c, cc), 0) << satC + satW;
        C(_(cc, ccc), 0) << bndC;
        // now, handle Phi and Omega
        // phis are not constrained to be 0
        if (outNode == inNode) {
          if (d < outNode->getNumLoops()) {
            if (nPc == nPp) {
              if (outNode->phiIsScheduled(d)) {
                // add it constants
                auto sch = outNode->getSchedule(d);
                C(_(c, cc), 0) -=
                  satPc * sch[_(0, nPc)] + satPp * sch[_(0, nPp)];
                C(_(cc, ccc), 0) -=
                  bndPc * sch[_(0, nPc)] + bndPp * sch[_(0, nPp)];
              } else {
                // FIXME: phiChild = [14:18), 4 cols
                // while Dependence seems to indicate 2
                // loops why the disagreement?
                auto po = outNode->getPhiOffset() + p;
                C(_(c, cc), _(po, po + nPc)) << satPc + satPp;
                C(_(cc, ccc), _(po, po + nPc)) << bndPc + bndPp;
              }
            } else if (outNode->phiIsScheduled(d)) {
              // add it constants
              // note that loop order in schedule goes
              // inner -> outer
              // so we need to drop inner most if one has less
              auto sch = outNode->getSchedule(d);
              auto schP = sch[_(0, nPp)];
              auto schC = sch[_(0, nPc)];
              C(_(c, cc), 0) -= satPc * schC + satPp * schP;
              C(_(cc, ccc), 0) -= bndPc * schC + bndPp * schP;
            } else if (nPc < nPp) {
              // Pp has more cols, so outer/leftmost overlap
              auto po = outNode->getPhiOffset() + p, poc = po + nPc,
                   pop = po + nPp;
              C(_(c, cc), _(po, poc)) << satPc + satPp(_, _(0, nPc));
              C(_(cc, ccc), _(po, poc)) << bndPc + bndPp(_, _(0, nPc));
              C(_(c, cc), _(poc, pop)) << satPp(_, _(nPc, end));
              C(_(cc, ccc), _(poc, pop)) << bndPp(_, _(nPc, end));
            } else /* if (nPc > nPp) */ {
              auto po = outNode->getPhiOffset() + p, poc = po + nPc,
                   pop = po + nPp;
              C(_(c, cc), _(po, pop)) << satPc(_, _(0, nPp)) + satPp;
              C(_(cc, ccc), _(po, pop)) << bndPc(_, _(0, nPp)) + bndPp;
              C(_(c, cc), _(pop, poc)) << satPc(_, _(nPp, end));
              C(_(cc, ccc), _(pop, poc)) << bndPc(_, _(nPp, end));
            }
            C(_(c, cc), outNode->getOmegaOffset() + o)
              << satO(_, 0) + satO(_, 1);
            C(_(cc, ccc), outNode->getOmegaOffset() + o)
              << bndO(_, 0) + bndO(_, 1);
          }
        } else {
          if (d < edge.getOutCurrentDepth())
            updateConstraints(C, outNode, satPc, bndPc, d, c, cc, ccc, p);
          if (d < edge.getInCurrentDepth()) {
            if (d < edge.getOutCurrentDepth() && !inNode->phiIsScheduled(d) &&
                !outNode->phiIsScheduled(d)) {
              invariant(inNode->getPhiOffset() != outNode->getPhiOffset());
            }
            updateConstraints(C, inNode, satPp, bndPp, d, c, cc, ccc, p);
          }
          // Omegas are included regardless of rotation
          if (d < edge.getOutCurrentDepth()) {
            if (d < edge.getInCurrentDepth())
              invariant(inNode->getOmegaOffset() != outNode->getOmegaOffset());
            C(_(c, cc), outNode->getOmegaOffset() + o)
              << satO(_, edge.isForward());
            C(_(cc, ccc), outNode->getOmegaOffset() + o)
              << bndO(_, edge.isForward());
          }
          if (d < edge.getInCurrentDepth()) {
            C(_(c, cc), inNode->getOmegaOffset() + o)
              << satO(_, !edge.isForward());
            C(_(cc, ccc), inNode->getOmegaOffset() + o)
              << bndO(_, !edge.isForward());
          }
        }
        c = ccc;
      }
    }
    invariant(size_t(l), size_t(1 + numLambda));
    invariant(size_t(c), size_t(numConstraints));
    addIndependentSolutionConstraints(omniSimplex.get(), nodes, d, counts);
    return omniSimplex;
  }
  static void updateConstraints(MutPtrMatrix<int64_t> C,
                                const ScheduledNode *node,
                                PtrMatrix<int64_t> sat, PtrMatrix<int64_t> bnd,
                                unsigned d, Row c, Row cc, Row ccc, Col p) {
    invariant(sat.numCol(), bnd.numCol());
    if (node->phiIsScheduled(d)) {
      // add it constants
      auto sch = node->getSchedule(d)[_(0, sat.numCol())];
      // order is inner <-> outer
      // so we need the end of schedule if it is larger
      C(_(c, cc), 0) -= sat * sch;
      C(_(cc, ccc), 0) -= bnd * sch;
    } else {
      // add it to C
      auto po = node->getPhiOffset() + p;
      C(_(c, cc), _(po, po + sat.numCol())) << sat;
      C(_(cc, ccc), _(po, po + bnd.numCol())) << bnd;
    }
  }
  void addIndependentSolutionConstraints(NotNull<Simplex> omniSimplex,
                                         const ScheduledNode *nodes, unsigned d,
                                         CoefCounts counts) {
    // omniSimplex->setNumCons(omniSimplex->getNumCons() +
    //                                memory.size());
    // omniSimplex->reserveExtraRows(memory.size());
    auto C{omniSimplex->getConstraints()};
    ptrdiff_t i = ptrdiff_t{C.numRow()} - counts.numSlack, s = counts.numLambda,
              o = 1 + counts.numSlack + counts.numLambda + counts.numOmegaCoefs;
    if (d == 0) {
      // add ones >= 0
      for (const ScheduledNode *node : nodes->getVertices()) {
        if (node->phiIsScheduled(d) || (!node->hasActiveEdges(deps, d)))
          continue;
        C(i, 0) = 1;
        C(i, node->getPhiOffsetRange() + o) << 1;
        C(i++, ++s) = -1; // for >=
      }
    } else {
      DenseMatrix<int64_t> A, N;
      for (const ScheduledNode *node : nodes->getVertices()) {
        if (node->phiIsScheduled(d) || (d >= node->getNumLoops()) ||
            (!node->hasActiveEdges(deps, d)))
          continue;
        A.resizeForOverwrite(Row{ptrdiff_t(node->getPhi().numCol())}, Col{d});
        A << node->getPhi()(_(0, d), _).transpose();
        math::NormalForm::nullSpace11(N, A);
        // we add sum(NullSpace,dims=1) >= 1
        // via 1 = sum(NullSpace,dims=1) - s, s >= 0
        C(i, 0) = 1;
        MutPtrVector<int64_t> cc{C(i, node->getPhiOffsetRange() + o)};
        // sum(N,dims=1) >= 1 after flipping row signs to be lex > 0
        for (ptrdiff_t m = 0; m < N.numRow(); ++m)
          cc += N(m, _) * lexSign(N(m, _));
        C(i++, ++s) = -1; // for >=
      }
    }
    invariant(ptrdiff_t(omniSimplex->getNumCons()), i);
    assert(!allZero(omniSimplex->getConstraints()(last, _)));
  }
  [[nodiscard]] static constexpr auto lexSign(PtrVector<int64_t> x) -> int64_t {
    for (auto a : x)
      if (a) return 2 * (a > 0) - 1;
    invariant(false);
    return 0;
  }

  //
  //
  //
  //
  //
  // Old junk:
  /// L is the inner-most loop getting dropped, i.e. it is the level at which
  /// the TreeResult rejected
  // Note this is based on the assumption that original loops are in
  // outer<->inner order. With that assumption, using lexSign on the null
  // space will tend to preserve the original traversal order.

  static auto summarizeMemoryAccesses(llvm::raw_ostream &os,
                                      ScheduledNode *nodes)
    -> llvm::raw_ostream & {
    os << "MemoryAccesses:\n";
    for (const Addr *m : nodes->eachAddr()) {
      os << "Inst: " << *m->getInstruction()
         << "\nOrder: " << m->getFusionOmega() << "\nLoop:" << *m->getAffLoop()
         << "\n";
    }
    return os;
  }
  friend inline auto operator<<(llvm::raw_ostream &os, ScheduledNode *nodes)
    -> llvm::raw_ostream & {
    os << "\nLoopBlock graph:\n";
    size_t i = 0;
    for (ScheduledNode *v : nodes->getVertices()) {
      os << "v_" << i++ << ":\nmem =\n";
      for (const Addr *m : v->localAddr()) os << *m->getInstruction() << "\n";
      os << v << "\n";
    }
    // BitSet
    os << "\nLoopBlock Edges:";
    for (ScheduledNode *inNode : nodes->getVertices()) {
      poly::AffineSchedule sin = inNode->getSchedule();
      for (Dependence *edge : nodes->outputEdges()) {
        os << "\n\n\tEdge = " << edge;
        ScheduledNode *outNode = edge->output()->getNode();
        os << "Schedule In: s.getPhi() =" << sin.getPhi()
           << "\ns.getFusionOmega() = " << sin.getFusionOmega()
           << "\ns.getOffsetOmega() = " << sin.getOffsetOmega();
        poly::AffineSchedule sout = outNode->getSchedule();
        os << "\n\nSchedule Out: s.getPhi() =" << sout.getPhi()
           << "\ns.getFusionOmega() = " << sout.getFusionOmega()
           << "\ns.getOffsetOmega() = " << sout.getOffsetOmega();

        os << "\n\n";
      }
    }
    os << "\nLoopBlock schedule:\n";
    for (Addr *mem : nodes->eachAddr()) {
      os << "Ref = " << *mem->getArrayPointer();
      ScheduledNode *node = mem->getNode();
      poly::AffineSchedule s = node->getSchedule();
      os << "s.getPhi()" << s.getPhi()
         << "\ns.getFusionOmega() = " << s.getFusionOmega()
         << "\ns.getOffsetOmega() = " << s.getOffsetOmega() << "\n";
    }
    return os << "\n";
  }
};

} // namespace poly::lp
