#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/Delinearization.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/FMF.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/InstructionCost.h>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Containers/Pair.cxx"
#include "Dicts/Dict.cxx"
#include "Dicts/Trie.cxx"
#include "IR/Address.cxx"
#include "IR/Array.cxx"
#include "IR/BBPredPath.cxx"
#include "IR/Instruction.cxx"
#include "IR/Node.cxx"
#include "IR/Phi.cxx"
#include "IR/Predicate.cxx"
#include "IR/TreeResult.cxx"
#include "IR/Users.cxx"
#include "Math/Constructors.cxx"
#include "Math/ManagedArray.cxx"
#include "Support/LLVMUtils.cxx"
#include "Target/Machine.cxx"
#include "Utilities/Invariant.cxx"
#include "Utilities/ListRanges.cxx"
#else
export module IR:Cache;

import Arena;
import ArrayConstructors;
import Invariant;
import ListRange;
import LLVMUtils;
import ManagedArray;
import Pair;
import TargetMachine;
import Trie;
import :Address;
import :Array;
import :BBPredPath;
import :Dict;
import :Instruction;
import :Node;
import :Phi;
import :Predicate;
import :TreeResult;
import :Users;
#endif

#ifdef USE_MODULE
export namespace IR {
#else
namespace IR {
#endif
using dict::map;

constexpr auto visit(auto &&vis, IR::Node *N) {
  switch (N->getKind()) {
  case Node::VK_Load: [[fallthrough]];
  case Node::VK_Stow: return vis(llvm::cast<Addr>(N));
  case Node::VK_Loop: return vis(llvm::cast<Loop>(N));
  case Node::VK_Exit: return vis(llvm::cast<Exit>(N));
  case Node::VK_FArg: return vis(llvm::cast<FunArg>(N));
  case Node::VK_CVal: return vis(llvm::cast<CVal>(N));
  case Node::VK_Cint: return vis(llvm::cast<Cint>(N));
  case Node::VK_Bint: return vis(llvm::cast<Bint>(N));
  case Node::VK_Cflt: return vis(llvm::cast<Cflt>(N));
  case Node::VK_Bflt: return vis(llvm::cast<Bflt>(N));
  case Node::VK_PhiN: return vis(llvm::cast<Phi>(N));
  case Node::VK_Func: [[fallthrough]];
  case Node::VK_Call: [[fallthrough]];
  case Node::VK_Oprn: return vis(llvm::cast<Compute>(N));
  }
  std::unreachable();
}

[[nodiscard]] inline auto
getOperands(Instruction *I) -> math::PtrVector<Value *> {
  if (const auto *C = llvm::dyn_cast<Compute>(I)) return C->getOperands();
  if (const auto *P = llvm::dyn_cast<Phi>(I)) return P->getOperands();
  if (I->getKind() == Node::VK_Stow)
    return {llvm::cast<Addr>(I)->getStoredValPtr(), math::length(1z)};
  return {nullptr, math::Length<>{}};
}
[[nodiscard]] inline auto getOperand(Instruction *I, unsigned i) -> Value * {
  if (const auto *C = llvm::dyn_cast<Compute>(I)) return C->getOperand(i);
  if (const auto *P = llvm::dyn_cast<Phi>(I)) return P->getOperands()[i];
  invariant(I->getKind() == Node::VK_Stow);
  invariant(i == 0);
  return llvm::cast<Addr>(I)->getStoredVal();
}
[[nodiscard]] inline auto getOperand(const Instruction *I,
                                     unsigned i) -> const Value * {
  if (const auto *C = llvm::dyn_cast<Compute>(I)) return C->getOperand(i);
  if (const auto *P = llvm::dyn_cast<Phi>(I)) return P->getOperands()[i];
  invariant(I->getKind() == Node::VK_Stow);
  invariant(i == 0);
  return llvm::cast<Addr>(I)->getStoredVal();
}
[[nodiscard]] inline auto getNumOperands(const Instruction *I) -> unsigned {
  if (const auto *C = llvm::dyn_cast<Compute>(I)) return C->getNumOperands();
  if (llvm::isa<IR::Phi>(I)) return 2;
  return I->getKind() == Node::VK_Stow;
}
[[nodiscard]] inline auto
commutativeOperandsFlag(const Instruction *I) -> uint8_t {
  if (const auto *C = llvm::dyn_cast<Compute>(I))
    return C->commuatativeOperandsFlag();
  return 0;
}

[[nodiscard]] inline auto
getIdentifier(const Instruction *I) -> Instruction::Identifier {
  llvm::Intrinsic::ID id;
  switch (I->getKind()) {
  case Node::VK_Load: id = llvm::Instruction::Load; break;
  case Node::VK_Stow: id = llvm::Instruction::Store; break;
  case Node::VK_PhiN: id = llvm::Instruction::PHI; break;
  case Node::VK_Call: [[fallthrough]];
  case Node::VK_Oprn: id = llvm::cast<Compute>(I)->getOpId(); break;
  default: id = llvm::Intrinsic::not_intrinsic;
  };
  return {id, I->getKind(), I->getType()};
}

inline void setOperands(Arena<> *alloc, Instruction *I,
                        math::PtrVector<Value *> x) {
  if (auto *C = llvm::dyn_cast<Compute>(I)) return C->setOperands(alloc, x);
  if (auto *P = llvm::dyn_cast<Phi>(I)) return P->setOperands(x);
  invariant(I->getKind() == Node::VK_Stow);
  static_cast<Addr *>(I)->setVal(alloc, x[0]);
}
using CostKind = llvm::TargetTransformInfo::TargetCostKind;
template <size_t N, bool TTI>
[[nodiscard]] inline auto getCost(Instruction *I, target::Machine<TTI> target,
                                  unsigned W, std::array<CostKind, N> costKinds)
  -> std::array<llvm::InstructionCost, N> {
  if (const auto *A = llvm::dyn_cast<Addr>(I))
    return A->calculateCostContiguousLoadStore(target, W, costKinds);
  if (llvm::isa<IR::Phi>(I)) return std::array<llvm::InstructionCost, N>{};
  return llvm::cast<Compute>(I)->calcCost(target, W, costKinds);
}

template <bool TTI>
[[nodiscard]] inline auto getCost(Instruction *I, target::Machine<TTI> target,
                                  unsigned W,
                                  CostKind costKind) -> llvm::InstructionCost {
  return getCost<1, TTI>(I, target, W, std::array<CostKind, 1>{costKind})[0];
}

class Cache {
  map<InstByValue, Compute *> inst_cse_map_;
  map<LoopInvariant::Identifier, LoopInvariant *> const_map_;
  alloc::OwningArena<> alloc_;
  Arrays ir_arrays_;
  Compute *free_inst_list_{nullptr}; // positive numOps/complete, but empty
  llvm::Module *mod_;
  auto allocateInst(unsigned numOps) -> Compute * {
    // Required to not explicitly end lifetime
    static_assert(std::is_trivially_destructible_v<Compute>);
    // Scan free list
    for (Compute *C = free_inst_list_; C;
         C = static_cast<Compute *>(C->getNext())) {
      if (C->getNumOperands() != numOps) continue;
      if (C == free_inst_list_)
        free_inst_list_ = llvm::cast_or_null<Compute>(C->getNext());
      invariant(C != free_inst_list_);
      C->removeFromList();
      return C;
    }
    // not found, allocate
    return static_cast<Compute *>(
      alloc_.allocate(sizeof(Compute) + sizeof(Value *) * numOps));
  }
  constexpr void free(Compute *rm) {
    rm->removeFromList();
    rm->setNext(free_inst_list_);
    free_inst_list_ = rm;
  }

  auto getCSE(Compute *C) -> Compute *& {
    return inst_cse_map_[InstByValue{C}];
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto createValue(llvm::Value *v, Predicate::Map *M, LLVMIRBuilder LB,
                   TreeResult tr,
                   Value *&n) -> containers::Pair<Value *, TreeResult> {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v))
      return createInstruction(i, M, LB, tr, n);
    if (auto *c = llvm::dyn_cast<llvm::ConstantInt>(v))
      return {createConstant(c, n), tr};
    if (auto *c = llvm::dyn_cast<llvm::ConstantFP>(v))
      return {createConstant(c, n), tr};
    return {createConstantVal(v, n), tr};
  }
  /// `user` is a user of `oldNode`
  /// Scan's `user`'s operands for instances of `oldNode`, replacing them with
  /// `NewNode`.
  // NOLINTNEXTLINE(misc-no-recursion)
  constexpr void replaceUsesByUser(Value *oldNode, Value *newNode,
                                   Instruction *user) {
    if (auto *A = llvm::dyn_cast<Addr>(user)) {
      // could be load or store; either are predicated
      // we might have `if (b) store(b)`, hence need both checks?
      bool is_pred = A->getPredicate() == oldNode,
           is_stored = A->isStore() && A->getStoredVal() == oldNode;
      invariant(is_pred || is_stored); // at least one must be true!
      if (is_pred) A->setPredicate(newNode);
      if (is_stored) A->setVal(getAllocator(), newNode);
    } else {
      auto *I = llvm::cast<IR::Instruction>(user);
      auto *C = llvm::dyn_cast<Compute>(I);
      math::MutPtrVector<Value *> ops;
      if (C) ops = C->getOperands();
      else ops = llvm::cast<Phi>(I)->getOperands();
      for (Value *&o : ops)
        if (o == oldNode) o = newNode;
      if (C) user = cse(C);
    }
    if (newNode->getKind() != Node::VK_Stow) newNode->addUser(&alloc_, user);
  }

  static void addSymbolic(math::Vector<int64_t> &offsets,
                          llvm::SmallVector<const llvm::SCEV *, 3> &symbols,
                          const llvm::SCEV *S, int64_t x = 1) {
    if (auto *j = std::ranges::find(symbols, S); j != symbols.end()) {
      offsets[std::distance(symbols.begin(), j)] += x;
    } else {
      symbols.push_back(S);
      offsets.push_back(x);
    }
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  static auto blackListAllDependentLoops(const llvm::SCEV *S) -> uint64_t {
    uint64_t flag{0};
    if (const auto *x = llvm::dyn_cast<const llvm::SCEVNAryExpr>(S)) {
      if (const auto *y = llvm::dyn_cast<const llvm::SCEVAddRecExpr>(x))
        flag |= uint64_t(1) << y->getLoop()->getLoopDepth();
      for (size_t i = 0; i < x->getNumOperands(); ++i)
        flag |= blackListAllDependentLoops(x->getOperand(i));
    } else if (const auto *c = llvm::dyn_cast<const llvm::SCEVCastExpr>(S)) {
      for (size_t i = 0; i < c->getNumOperands(); ++i)
        flag |= blackListAllDependentLoops(c->getOperand(i));
      return flag;
    } else if (const auto *d = llvm::dyn_cast<const llvm::SCEVUDivExpr>(S)) {
      for (size_t i = 0; i < d->getNumOperands(); ++i)
        flag |= blackListAllDependentLoops(d->getOperand(i));
      return flag;
    }
    return flag;
  }
  static auto blackListAllDependentLoops(const llvm::SCEV *S,
                                         size_t numPeeled) -> uint64_t {
    return blackListAllDependentLoops(S) >> (numPeeled + 1);
  }
  // translates scev S into loops and symbols
  auto // NOLINTNEXTLINE(misc-no-recursion)
  fillAffineIndices(MutPtrVector<int64_t> v, int64_t *coffset,
                    math::Vector<int64_t> &offsets,
                    llvm::SmallVector<const llvm::SCEV *, 3> &symbolicOffsets,
                    const llvm::SCEV *S, llvm::ScalarEvolution *SE, int64_t mlt,
                    size_t numPeeled) -> uint64_t {
    using ::utils::getConstantInt;
    uint64_t black_list{0};
    if (const auto *x = llvm::dyn_cast<const llvm::SCEVAddRecExpr>(S)) {
      const llvm::Loop *L = x->getLoop();
      size_t depth = L->getLoopDepth();
      if (depth <= numPeeled) {
        // we effectively have an offset
        // we'll add an
        addSymbolic(offsets, symbolicOffsets, S, 1);
        for (size_t i = 1; i < x->getNumOperands(); ++i)
          black_list |= blackListAllDependentLoops(x->getOperand(i));

        return black_list;
      }
      // outermost loop has loopInd 0
      ptrdiff_t loop_ind = ptrdiff_t(depth) - ptrdiff_t(numPeeled + 1);
      if (x->isAffine()) {
        if (loop_ind >= 0) {
          if (auto c = getConstantInt(x->getOperand(1))) {
            // we want the innermost loop to have index 0
            v[loop_ind] += *c;
            return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                     x->getOperand(0), SE, mlt, numPeeled);
          }
          black_list |= (uint64_t(1) << uint64_t(loop_ind));
        }
        // we separate out the addition
        // the multiplication was either peeled or involved
        // non-const multiple
        black_list |= fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                        x->getOperand(0), SE, mlt, numPeeled);
        // and then add just the multiple here as a symbolic offset
        const llvm::SCEV *add_rec = SE->getAddRecExpr(
          SE->getZero(x->getOperand(0)->getType()), x->getOperand(1),
          x->getLoop(), x->getNoWrapFlags());
        addSymbolic(offsets, symbolicOffsets, add_rec, mlt);
        return black_list;
      }
      if (loop_ind >= 0) black_list |= (uint64_t(1) << uint64_t(loop_ind));
    } else if (std::optional<int64_t> c = getConstantInt(S)) {
      *coffset += *c;
      return 0;
    } else if (const auto *ar = llvm::dyn_cast<const llvm::SCEVAddExpr>(S)) {
      return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                               ar->getOperand(0), SE, mlt, numPeeled) |
             fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                               ar->getOperand(1), SE, mlt, numPeeled);
    } else if (const auto *m = llvm::dyn_cast<const llvm::SCEVMulExpr>(S)) {
      if (auto op0 = getConstantInt(m->getOperand(0))) {
        return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                 m->getOperand(1), SE, mlt * (*op0), numPeeled);
      }
      if (auto op1 = getConstantInt(m->getOperand(1))) {
        return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                 m->getOperand(0), SE, mlt * (*op1), numPeeled);
      }
    } else if (const auto *ca = llvm::dyn_cast<llvm::SCEVCastExpr>(S))
      return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                               ca->getOperand(0), SE, mlt, numPeeled);
    addSymbolic(offsets, symbolicOffsets, S, mlt);
    return black_list | blackListAllDependentLoops(S, numPeeled);
  }
  static void extendDensePtrMatCols(Arena<> *alloc,
                                    MutDensePtrMatrix<int64_t> &A,
                                    math::Row<> R, math::Col<> C) {
    MutDensePtrMatrix<int64_t> B{math::matrix<int64_t>(alloc, A.numRow(), C)};
    for (ptrdiff_t j = 0; j < R; ++j) {
      B[j, _(0, A.numCol())] << A[j, _];
      B[j, _(A.numCol(), end)] << 0;
    }
    std::swap(A, B);
  }
  void setOperands(Compute *op, PtrVector<Value *> ops) {
    size_t N = ops.size();
    MutPtrVector<Value *> operands{op->getOperands()};
    for (size_t n = 0; n < N; ++n) {
      Value *operand = operands[n] = ops[n];
      operand->addUser(&alloc_, op);
    }
  }
  auto createArrayRefImpl(llvm::Instruction *loadOrStore,
                          const llvm::SCEV *accessFn, int numLoops,
                          const llvm::SCEV *elSz, LLVMIRBuilder LB,
                          TreeResult tr, LoopInvariant *array,
                          llvm::Value *arrayVal,
                          Value *&t) -> containers::Pair<Value *, TreeResult> {
    llvm::SmallVector<const llvm::SCEV *, 3> subscripts, sizes;
    llvm::delinearize(*LB.SE_, accessFn, subscripts, sizes, elSz);
    ptrdiff_t num_dims = std::ssize(subscripts);
    invariant(num_dims, std::ssize(sizes));
    if (!num_dims) return {t = zeroDimRef(loadOrStore, array, 0), tr};
    int num_peeled = tr.rejectDepth;
    numLoops -= num_peeled;
    math::IntMatrix<math::StridedDims<>> Rt{
      math::StridedDims<>{math::row(num_dims), math::col(numLoops)}, 0};
    llvm::SmallVector<const llvm::SCEV *, 3> symbolic_offsets;
    uint64_t black_list{0};
    math::Vector<int64_t> coffsets{math::length(num_dims), 0};
    MutDensePtrMatrix<int64_t> offs_mat{nullptr,
                                        DenseDims<>{math::row(num_dims), {}}};
    {
      math::Vector<int64_t> offsets;
      for (ptrdiff_t i = 0; i < num_dims; ++i) {
        offsets << 0;
        black_list |=
          fillAffineIndices(Rt[i, _], &coffsets[i], offsets, symbolic_offsets,
                            subscripts[i], LB.SE_, 1, num_peeled);
        if (offsets.size() > offs_mat.numCol())
          extendDensePtrMatCols(&alloc_, offs_mat, math::row(i),
                                math::col(offsets.size()));
        offs_mat[i, _] << offsets;
      }
    }
    int num_extra_loops_to_peel = 64 - std::countl_zero(black_list);

    unsigned n_off = symbolic_offsets.size();
    Addr *op;
    llvm::SCEVExpander expdr(*LB.SE_, dataLayout(), "ConstructLoop");
    llvm::Instruction *loc;
    if (auto *I = llvm::dyn_cast<llvm::Instruction>(arrayVal)) loc = I;
    else loc = &loadOrStore->getFunction()->front().front();
    loc = &*expdr.findInsertPointAfter(loc, loadOrStore);
    auto c = alloc_.checkpoint();
    MutPtrVector<Value *> szv = math::vector<IR::Value *>(&alloc_, num_dims);
    for (unsigned i = 0; i < num_dims; ++i) {
      const llvm::SCEV *s = sizes[i];
      llvm::Value *S = expdr.expandCodeFor(s, s->getType(), loc);
      szv[i] = getValueOutsideLoop(S, LB);
    }
    auto [ar, found] = ir_arrays_.emplace_back(array, szv);
    if (found) alloc_.rollback(c);
    op = Addr::construct(&alloc_, ar, loadOrStore,
                         Rt[_, _(num_extra_loops_to_peel, end)], n_off,
                         coffsets, offs_mat.data(), tr.maxDepth);
    for (unsigned i = 0; i < n_off; ++i) {
      const llvm::SCEV *s = symbolic_offsets[i];
      llvm::Value *S = expdr.expandCodeFor(s, s->getType(), loc);
      op->getSymbolicOffsets()[i] = getValueOutsideLoop(S, LB);
    }
    t = op;
    tr.addAddr(op);
    tr.rejectDepth += num_extra_loops_to_peel;
    return {op, tr};
  } // alloc is short alloc
  [[nodiscard]] inline auto // NOLINTNEXTLINE(misc-no-recursion)
  descendBlock(Arena<> *alloc, dict::InlineTrie<llvm::BasicBlock *> &visited,
               Predicate::Map &predMap, llvm::BasicBlock *BBsrc,
               llvm::BasicBlock *BBdst, Predicate::Intersection predicate,
               llvm::BasicBlock *BBhead, llvm::Loop *L, LLVMIRBuilder LB,
               TreeResult &tr) -> Predicate::Map::Destination {
    if (BBsrc == BBdst) {
      assert(!predMap.contains(BBsrc));
      predMap.insert({BBsrc, Predicate::Set{predicate}});
      return Predicate::Map::Destination::Reached;
    }
    if (L && (!(L->contains(BBsrc)))) {
      // oops, we have skipped the preheader and escaped the loop.
      return Predicate::Map::Destination::Returned;
    }
    if (visited.contains(BBsrc)) {
      // FIXME: This is terribly hacky.
      // if `BBsrc == BBhead`, then we assume we hit a path that
      // bypasses the following loop, e.g. there was a loop guard.
      //
      // Thus, we return `Returned`, indicating that it was a
      // non-fatal dead-end. Otherwise, we check if it seems to have
      // led to a live, non-empty path.
      // TODO: should we union the predicates in case of returned?
      if ((BBsrc != BBhead) && predMap.find(BBsrc) != predMap.end())
        return Predicate::Map::Destination::Reached;
      return Predicate::Map::Destination::Returned;
    }
    // Inserts a tombstone to indicate that we have visited BBsrc, but
    // not actually reached a destination.
    visited.insert(alloc, BBsrc);
    const llvm::Instruction *I = BBsrc->getTerminator();
    if (!I) return Predicate::Map::Destination::Unknown;
    if (llvm::isa<llvm::ReturnInst>(I))
      return Predicate::Map::Destination::Returned;
    if (llvm::isa<llvm::UnreachableInst>(I))
      return Predicate::Map::Destination::Unreachable;
    const auto *BI = llvm::dyn_cast<llvm::BranchInst>(I);
    if (!BI) return Predicate::Map::Destination::Unknown;
    if (BI->isUnconditional()) {
      auto rc = descendBlock(alloc, visited, predMap, BI->getSuccessor(0),
                             BBdst, predicate, BBhead, L, LB, tr);
      if (rc == Predicate::Map::Destination::Reached)
        predMap.reach(alloc, BBsrc, predicate);
      return rc;
    }
    // We have a conditional branch.
    llvm::Value *cond = BI->getCondition();
    // We need to check both sides of the branch and add a predicate.
    ptrdiff_t pred_ind = addPredicate(alloc, &predMap, cond, LB, tr);
    auto rc0 =
      descendBlock(alloc, visited, predMap, BI->getSuccessor(0), BBdst,
                   predicate.intersect(pred_ind, Predicate::Relation::True),
                   BBhead, L, LB, tr);
    if (rc0 == Predicate::Map::Destination::Unknown) // bail
      return Predicate::Map::Destination::Unknown;
    auto rc1 =
      descendBlock(alloc, visited, predMap, BI->getSuccessor(1), BBdst,
                   predicate.intersect(pred_ind, Predicate::Relation::False),
                   BBhead, L, LB, tr);
    if ((rc0 == Predicate::Map::Destination::Returned) ||
        (rc0 == Predicate::Map::Destination::Unreachable)) {
      if (rc1 == Predicate::Map::Destination::Reached) {
        //  we're now assuming that !cond
        predMap.assume(
          Predicate::Intersection(pred_ind, Predicate::Relation::False));
        predMap.reach(alloc, BBsrc, predicate);
      }
      return rc1;
    }
    if ((rc1 == Predicate::Map::Destination::Returned) ||
        (rc1 == Predicate::Map::Destination::Unreachable)) {
      if (rc0 == Predicate::Map::Destination::Reached) {
        //  we're now assuming that cond
        predMap.assume(
          Predicate::Intersection(pred_ind, Predicate::Relation::True));
        predMap.reach(alloc, BBsrc, predicate);
      }
      return rc0;
    }
    if (rc0 != rc1) return Predicate::Map::Destination::Unknown;
    if (rc0 == Predicate::Map::Destination::Reached)
      predMap.reach(alloc, BBsrc, predicate);
    return rc0;
  }

public:
  // TODO:
  // 1. see why L->contains(BBsrc) does not work; does it only contain BBs
  // in it directly, and not nested another loop deeper?
  // 2. We are ignoring cycles for now; we must ensure this is done
  // correctly
  /// We bail if there are more than 32 conditions; control flow that
  /// branchy is probably not worth trying to vectorize.
  [[nodiscard]] inline auto
  descend(Arena<> *alloc, llvm::BasicBlock *BBsrc, llvm::BasicBlock *BBdst,
          llvm::Loop *L, LLVMIRBuilder LB,
          TreeResult &tr) -> std::optional<Predicate::Map> {
    auto p = alloc->checkpoint();
    std::optional<Predicate::Map> pred_map{{alloc}};
    dict::InlineTrie<llvm::BasicBlock *> visited{};
    if (descendBlock(alloc, visited, *pred_map, BBsrc, BBdst, {}, BBsrc, L, LB,
                     tr) == Predicate::Map::Destination::Reached)
      return pred_map;
    pred_map = std::nullopt;
    alloc->rollback(p);
    return pred_map;
  }

  Cache(llvm::Module *m) : mod_(m) {}
  [[nodiscard]] auto dataLayout() const -> const llvm::DataLayout & {
    return mod_->getDataLayout();
  }
  [[nodiscard]] auto getContext() const -> llvm::LLVMContext & {
    return mod_->getContext();
  }
  /// complete the operands
  // NOLINTNEXTLINE(misc-no-recursion)
  auto complete(Compute *I, Predicate::Map *M, LLVMIRBuilder LB,
                TreeResult tr) -> containers::Pair<Compute *, TreeResult> {
    auto *i = I->getLLVMInstruction();
    unsigned n_ops = I->numCompleteOps();
    auto ops = I->getOperands();
    for (unsigned j = 0; j < n_ops; ++j) {
      auto *op = i->getOperand(j);
      auto [v, tret] = getValue(op, M, LB, tr);
      tr = tret;
      ops[j] = v;
      v->addUser(&alloc_, I);
    }
    return {cse(I), tr};
  }
  // update list of incompletes
  auto completeInstructions(Predicate::Map *M, LLVMIRBuilder LB, TreeResult tr)
    -> containers::Pair<Compute *, TreeResult> {
    Compute *completed = nullptr;
    for (Compute *I = tr.incomplete; I;
         I = static_cast<Compute *>(I->getNext())) {
      if (!M->contains(I->getLLVMInstruction())) continue;
      I->removeFromList();
      auto [ct, trt] = complete(I, M, LB, tr);
      completed = static_cast<Compute *>(ct->setNext(completed));
      tr = trt;
    }
    return {completed, tr};
  }
  /// Get the cache's allocator.
  /// This is a long-lived bump allocator, mass-freeing after each
  /// sub-tree optimization.
  constexpr auto getAllocator() -> Arena<> * { return &alloc_; }
  /// try to remove `I` as a duplicate
  /// this travels downstream;
  /// if `I` is eliminated, all users of `I`
  /// get updated, making them CSE-candidates.
  /// In this manner, we travel downstream through users.
  // NOLINTNEXTLINE(misc-no-recursion)
  auto cse(Compute *I) -> Compute * {
    Compute *&cse = getCSE(I);
    if (cse == nullptr || (cse == I)) return cse = I; // update ref
    replaceAllUsesWith(I, cse);
    free(I);
    return cse;
  }
  /// void replaceUsesByUsers(Value *oldNode, Value *newNode)
  /// The name is confusing. This iterates through oldNode's users
  /// (i.e. things using oldNode), and  swaps the `oldNode` for `newNode` in
  /// those user's operands.
  /// It checks if those users are `newNode` itself, if
  /// so, it does not modify. This allows replacing `x` with `f(x)`, for
  /// example. For example, we may wish to replace all uses of `x` with
  /// `ifelse(cond, x, y)`. That feature is used for control flow merging.
  // NOLINTNEXTLINE(misc-no-recursion)
  constexpr auto replaceUsesByUsers(Value *oldNode, Value *newNode) -> bool {
    invariant(oldNode->getKind() == Node::VK_Load ||
              oldNode->getKind() >= Node::VK_Func);
    Users &users = oldNode->getUsers();
    Instruction *found_new_node{nullptr};
    for (Instruction *user : users)
      if (user == newNode) found_new_node = user;
      else replaceUsesByUser(oldNode, newNode, user);
    users.clear();
    if (found_new_node) users.pushKnownUnique(&alloc_, found_new_node);
    return found_new_node != nullptr;
  }
  /// replaceAllUsesWith(Value *oldNode, Value *newNode)
  /// replaces all uses of `oldNode` with `newNode`
  /// updating the operands of all users of `oldNode`
  /// and the `users` of all operands of `oldNode`
  // NOLINTNEXTLINE(misc-no-recursion)
  void replaceAllUsesWith(Instruction *oldNode, Value *newNode) {
    invariant(oldNode->getKind() == Node::VK_Load ||
              oldNode->getKind() >= Node::VK_Func);
    // `replaceAllUsesWith` invalidates `oldNode`
    // thus, `newNode` should not be one of its users!!!
    // If we're inserting `newNode` as a user between
    // `oldNode` and its users, we should be calling
    // `replaceUsesByUsers`.
    // These are rather different operations, so it doesn't make
    // sense to dynamically be doing one or the other.
    invariant(!replaceUsesByUsers(oldNode, newNode));
    // every operand of oldNode needs their users updated
    if (auto *I = llvm::dyn_cast<Compute>(oldNode)) {
      for (Value *&n : I->getOperands()) n->removeFromUsers(oldNode);
    } else {
      invariant(oldNode->getKind() == Node::VK_Load);
      if (Value *p = static_cast<Addr *>(oldNode)->getPredicate())
        p->removeFromUsers(oldNode);
    }
  }

  /// Here, we have a set of methods that take a `Predicate::Map* M` and a
  /// `TreeResult` argument, returning a `Value*` of some kind and a
  /// `TreeResult` Any operands that are not in `M` will be left incomplete, and
  /// added to the incomplete list of the `TreeResult` argument. If `M` is
  /// `nullptr`, then all operands will be left incomplete.
  // NOLINTNEXTLINE(misc-no-recursion)
  auto getValue(llvm::Value *v, Predicate::Map *M, LLVMIRBuilder LB,
                TreeResult tr) -> containers::Pair<Value *, TreeResult> {
    Value *&n = (*LB.llvmToInternalMap_)[v];
    if (n) return {n, tr};
    // by reference, so we can update in creation
    return createValue(v, M, LB, tr, n);
  }
  auto getValue(llvm::Instruction *I, Predicate::Map *M, LLVMIRBuilder LB,
                TreeResult tr) -> containers::Pair<Instruction *, TreeResult> {
    auto [v, tret] = getValue(static_cast<llvm::Value *>(I), M, LB, tr);
    return {llvm::cast<Instruction>(v), tret};
  }
  auto getValueOutsideLoop(llvm::Value *v,
                           LLVMIRBuilder LB) -> LoopInvariant * {
    Value *&n = (*LB.llvmToInternalMap_)[v];
    if (n) return static_cast<LoopInvariant *>(n);
    // by reference, so we can update in creation
    return createConstantVal(v, n);
  }

  // NOLINTNEXTLINE(misc-no-recursion)
  auto createInstruction(llvm::Instruction *I, Predicate::Map *M,
                         LLVMIRBuilder LB, TreeResult tr,
                         Value *&t) -> containers::Pair<Value *, TreeResult> {
    auto *load = llvm::dyn_cast<llvm::LoadInst>(I);
    auto *store = llvm::dyn_cast<llvm::StoreInst>(I);
    if (!load && !store) return createCompute(I, M, LB, tr, t);
    auto *ptr = load ? load->getPointerOperand() : store->getPointerOperand();
    llvm::Loop *L = LB.LI_->getLoopFor(I->getParent());
    auto [v, tret] = createArrayRef(I, L, ptr, M, LB, tr, t);
    t = v;
    if (Addr *A = llvm::dyn_cast<Addr>(v); store && A) {
      // only Computes may be incomplete, so we unconditionally get the store
      // value
      auto [v2, tret2] = getValue(store->getValueOperand(), M, LB, tret);
      A->setVal(getAllocator(), v2);
      tret = tret2;
    }
    return {v, tret};
  }

  // NOLINTNEXTLINE(misc-no-recursion)
  auto createCompute(llvm::Instruction *I, Predicate::Map *M, LLVMIRBuilder LB,
                     TreeResult tr,
                     Value *&t) -> containers::Pair<Compute *, TreeResult> {
    auto [id, kind] = Compute::getIDKind(I);
    int num_ops = int(I->getNumOperands());
    Compute *n =
      std::construct_at(allocateInst(num_ops), kind, I, id, -num_ops);
    t = n;
    if (M && M->contains(I)) {
      auto [v, tret] = complete(n, M, LB, tr);
      n = v;
      tr = tret;
    } else tr.addIncomplete(n);
    return {n, tr};
  }

  auto zeroDimRef(llvm::Instruction *loadOrStore,
                  llvm::SCEVUnknown const *arrayPtr, unsigned numLoops,
                  LLVMIRBuilder LB) -> Addr * {
    return zeroDimRef(loadOrStore,
                      getValueOutsideLoop(arrayPtr->getValue(), LB), numLoops);
  }
  auto zeroDimRef(llvm::Instruction *loadOrStore, LoopInvariant *ap,
                  unsigned numLoops) -> Addr * {
    auto [array, f] = ir_arrays_.emplace_back(ap, {nullptr, {}});
    return Addr::zeroDim(&alloc_, array, loadOrStore, numLoops);
  }
  // create Addr
  auto getArrayRef(llvm::Instruction *loadOrStore, llvm::Loop *L,
                   llvm::Value *ptr, Predicate::Map *M, LLVMIRBuilder LB,
                   TreeResult tr) -> containers::Pair<Value *, TreeResult> {
    Value *&n = (*LB.llvmToInternalMap_)[loadOrStore];
    if (n) return {n, tr};
    return createArrayRef(loadOrStore, L, ptr, M, LB, tr, n);
  }
  // create Addr
  auto createArrayRef(llvm::Instruction *loadOrStore, llvm::Value *ptr,
                      Predicate::Map *M, LLVMIRBuilder LB, TreeResult tr,
                      Value *&t) -> containers::Pair<Value *, TreeResult> {
    llvm::Loop *L = LB.LI_->getLoopFor(loadOrStore->getParent());
    return createArrayRef(loadOrStore, L, ptr, M, LB, tr, t);
  }
  // create Addr
  // There is a recursive callchain because of `getValue` for stored value
  // NOLINTNEXTLINE(misc-no-recursion)
  auto createArrayRef(llvm::Instruction *loadOrStore, llvm::Loop *L,
                      llvm::Value *ptr, Predicate::Map *M, LLVMIRBuilder LB,
                      TreeResult tr,
                      Value *&t) -> containers::Pair<Value *, TreeResult> {
    const llvm::SCEV *el_sz = LB.SE_->getElementSize(loadOrStore),
                     *access_fn = LB.SE_->getSCEVAtScope(ptr, L);
    int num_loops = int(L->getLoopDepth());
    if (ptr)
      return createArrayRef(loadOrStore, access_fn, num_loops, el_sz, M, LB, tr,
                            t);
    tr.rejectDepth = std::max(tr.rejectDepth, num_loops);
    return {t = alloc_.create<CVal>(loadOrStore), tr};
  }
  // There is a recursive callchain because of `getValue` for stored value
  // NOLINTNEXTLINE(misc-no-recursion)
  auto createArrayRef(llvm::Instruction *loadOrStore,
                      const llvm::SCEV *accessFn, int numLoops,
                      const llvm::SCEV *elSz, Predicate::Map *M,
                      LLVMIRBuilder LB, TreeResult tr,
                      Value *&t) -> containers::Pair<Value *, TreeResult> {
    // https://llvm.org/doxygen/Delinearization_8cpp_source.html#l00582
    const auto *array_ptr =
      llvm::dyn_cast<llvm::SCEVUnknown>(LB.SE_->getPointerBase(accessFn));
    // Do not delinearize if we cannot find the base pointer.
    if (!array_ptr) {
      tr.rejectDepth = std::max(tr.rejectDepth, numLoops);
      return {t = alloc_.create<CVal>(loadOrStore), tr};
    }
    llvm::Value *array_val = array_ptr->getValue();
    LoopInvariant *array = getValueOutsideLoop(array_val, LB);
    accessFn = LB.SE_->getMinusSCEV(accessFn, array_ptr);
    auto [A, trnew] = createArrayRefImpl(loadOrStore, accessFn, numLoops, elSz,
                                         LB, tr, array, array_val, t);
    if (auto *store = llvm::dyn_cast<llvm::StoreInst>(loadOrStore)) {
      auto [sv, trs] = getValue(store->getValueOperand(), M, LB, trnew);
      tr = trs;
      Stow(A).setVal(getAllocator(), sv);
    } else tr = trnew;
    return {A, tr};
  }

  template <size_t N>
  auto createCompute(llvm::Intrinsic::ID opId, Node::ValKind opk,
                     std::array<Value *, N> ops, llvm::Type *typ,
                     llvm::FastMathFlags fmf) -> Compute * {
    Compute *op = std::construct_at(allocateInst(N), opk, opId, N, typ, fmf);
    setOperands(op, ops);
    return cse(op);
  }
  auto createCompute(llvm::Intrinsic::ID opId, Node::ValKind opk,
                     PtrVector<Value *> ops, llvm::Type *typ,
                     llvm::FastMathFlags fmf) -> Compute * {
    unsigned N = ops.size();
    Compute *op = std::construct_at(allocateInst(N), opk, opId, N, typ, fmf);
    setOperands(op, ops);
    return cse(op);
  }
  template <size_t N>
  auto createOperation(llvm::Intrinsic::ID opId, std::array<Value *, N> ops,
                       llvm::Type *typ, llvm::FastMathFlags fmf) -> Compute * {
    return createCompute(opId, Node::VK_Oprn, ops, typ, fmf);
  }
  auto createOperation(llvm::Intrinsic::ID opId, PtrVector<Value *> ops,
                       llvm::Type *typ, llvm::FastMathFlags fmf) -> Compute * {
    size_t N = ops.size();
    Compute *op =
      std::construct_at(allocateInst(N), Node::VK_Oprn, opId, N, typ, fmf);
    setOperands(op, ops);
    return cse(op);
  }
  // The intended use is to modify the copied operation, and then call `cse`
  // after the modifications to try and simplify.
  auto copyCompute(Compute *A) -> Compute * {
    Compute *B = createCompute(A->getOpId(), A->getKind(), A->getOperands(),
                               A->getType(), A->getFastMathFlags());
    setOperands(B, A->getOperands());
    return B;
  }
  auto similarCompute(Compute *A, PtrVector<Value *> ops) -> Compute * {
    invariant(ptrdiff_t(A->getNumOperands()), ops.size());
    return createCompute(A->getOpId(), A->getKind(), ops, A->getType(),
                         A->getFastMathFlags());
  }
  template <size_t N>
  auto getOperation(llvm::Intrinsic::ID opId, std::array<Value *, N> ops,
                    llvm::Type *typ, llvm::FastMathFlags fmf) -> Compute * {
    Compute *op = createOperation(opId, ops, typ, fmf);
    Compute *&cse = getCSE(op);
    if (cse == nullptr || (cse == op)) return cse = op; // update ref
    free(op);
    return cse;
  }
  auto createFBinOp(llvm::Intrinsic::ID opid, Value *a, Value *b,
                    llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    llvm::Type *T = a->getType();
    invariant(T == b->getType());
    invariant(T->isDoubleTy() || T->isFloatTy() || T->isBFloatTy() ||
              T->isHalfTy() || T->isFP128Ty());
    Compute *ret = createOperation(opid, std::array<Value *, 2>{a, b}, T, fmf);
    invariant(ret != a);
    invariant(ret != b);
    return ret;
  }

  auto createFAdd(Value *a, Value *b,
                  llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    return createFBinOp(llvm::Instruction::FAdd, a, b, fmf);
  }
  auto createFSub(Value *a, Value *b,
                  llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    return createFBinOp(llvm::Instruction::FSub, a, b, fmf);
  }
  auto createFMul(Value *a, Value *b,
                  llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    return createFBinOp(llvm::Instruction::FMul, a, b, fmf);
  }
  auto createFDiv(Value *a, Value *b,
                  llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    return createFBinOp(llvm::Instruction::FDiv, a, b, fmf);
  }
  static void assertFloatingPoint(llvm::Type *T) {
    invariant(T->isDoubleTy() || T->isFloatTy() || T->isBFloatTy() ||
              T->isHalfTy() || T->isFP128Ty());
  }
  auto createFNeg(Value *a, llvm::FastMathFlags fmf =
                              llvm::FastMathFlags::getFast()) -> Compute * {
    llvm::Type *T = a->getType();
    assertFloatingPoint(T);
    return createOperation(llvm::Instruction::FNeg, std::array<Value *, 1>{a},
                           T, fmf);
  }
  auto createSItoFP(Value *a, llvm::FastMathFlags fmf =
                                llvm::FastMathFlags::getFast()) -> Compute * {
    llvm::Type *FP = llvm::Type::getDoubleTy(getContext());
    return createSItoFP(a, FP, fmf);
  }
  auto createSItoFP(Value *a, llvm::Type *FP,
                    llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    llvm::Type *T = a->getType();
    invariant(T->isIntegerTy());
    assertFloatingPoint(FP);
    return createOperation(llvm::Instruction::SIToFP, std::array<Value *, 1>{a},
                           FP, fmf);
  }
  auto createUItoFP(Value *a, llvm::FastMathFlags fmf =
                                llvm::FastMathFlags::getFast()) -> Compute * {
    llvm::Type *FP = llvm::Type::getDoubleTy(getContext());
    return createUItoFP(a, FP, fmf);
  }
  auto createUItoFP(Value *a, llvm::Type *FP,
                    llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    llvm::Type *T = a->getType();
    invariant(T->isIntegerTy());
    assertFloatingPoint(FP);
    return createOperation(llvm::Instruction::UIToFP, std::array<Value *, 1>{a},
                           FP, fmf);
  }
  auto createSqrt(Value *a, llvm::FastMathFlags fmf =
                              llvm::FastMathFlags::getFast()) -> Compute * {
    llvm::Type *T = a->getType();
    invariant(T->isDoubleTy() || T->isFloatTy() || T->isBFloatTy() ||
              T->isHalfTy() || T->isFP128Ty());
    return createCompute(llvm::Intrinsic::sqrt, Node::VK_Call,
                         std::array<Value *, 1>{a}, T, fmf);
  }
  auto createBinOp(llvm::Intrinsic::ID opid, Value *a, Value *b,
                   llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    llvm::Type *T = a->getType();
    invariant(T == b->getType());
    invariant(T->isIntegerTy());
    return createOperation(opid, std::array<Value *, 2>{a, b}, T, fmf);
  }

  auto createAdd(Value *a, Value *b,
                 llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    return createBinOp(llvm::Instruction::Add, a, b, fmf);
  }
  auto createSub(Value *a, Value *b,
                 llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    return createBinOp(llvm::Instruction::Sub, a, b, fmf);
  }
  auto createMul(Value *a, Value *b,
                 llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    return createBinOp(llvm::Instruction::Mul, a, b, fmf);
  }
  auto createSDiv(Value *a, Value *b,
                  llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    return createBinOp(llvm::Instruction::SDiv, a, b, fmf);
  }
  auto createUDiv(Value *a, Value *b,
                  llvm::FastMathFlags fmf = llvm::FastMathFlags::getFast())
    -> Compute * {
    return createBinOp(llvm::Instruction::UDiv, a, b, fmf);
  }
  /// Creates a `Phi` when hoisting `Load* a` and `Stow* b` out of a
  /// loop. For example, we go from
  /// for (int i=0; i<I; ++i){
  ///   for (int j=0; j<J; ++j) A[i] = foo(A[i]);
  /// }
  /// to
  /// for (int i=0; i<I; ++i){
  ///   w = A[i];
  ///   for (int j=0; j<J; ++j){
  ///     x = phi(w,y);
  ///     y = foo(x);
  ///   }
  ///   // z = phi(w,y);
  ///   A[i] = x;
  /// }
  /// The semantics of our `phi` nodes are that if `J<=0` such that the `j` loop
  /// does not iterate, `x = w`. That is, it works as if we had an equivalent
  /// `z = phi` definition.
  void createPhiPair(Addr *a, Addr *b, Loop *L) {
    invariant(a->getType() == b->getType());
    invariant(a->isLoad());
    invariant(b->isStore());
    // note, `create<Phi>(a,b,L)` sets `a` and `b->getStoredVal()` to
    // `getOperands()`, but does not update users.
    Loop *P = a->getLoop();
    invariant(P == L->getLoop());
    Phi *phi_accu = alloc_.create<Phi>(a, b, L);
    phi_accu->setNext(L->getChild())->setParent(L);
    Phi *phi_join = alloc_.create<Phi>(a, b, L);
    // phiJoin->insertAhead(L);
    // phiJoin->setParent(P);
    auto *v = llvm::cast<Instruction>(phi_accu->getOperand(1));
    invariant(v == b->getStoredVal());
    invariant(phi_join->getOperand(1) == b->getStoredVal());
    // a was just hoisted out into parent loop of `P`
    Users &usersa = a->getUsers();
    Users &usersv = v->getUsers();
    {
      // auto scope = alloc.scope();
      math::Vector<Instruction *> keep{};
      keep.reserve(std::max(usersa.size(), usersv.size()));
      // math::ResizeableView<Instruction *, math::Length<>> keep{
      //   &alloc, {}, math::capacity(std::max(usersa.size(), usersv.size()))};
      // a's uses within the loop are replaced by phiAccu
      for (Instruction *user : usersa)
        if (L->contains(user)) replaceUsesByUser(a, phi_accu, user);
        else keep.push_back_within_capacity(user);
      usersa.clear();
      for (Instruction *user : keep) usersa.push_back_within_capacity(user);
      keep.clear();
      // v's uses outside of the loop are replaced by phiJoin
      for (Instruction *user : usersv)
        if (L->contains(user)) keep.push_back_within_capacity(user);
        else replaceUsesByUser(v, phi_join, user);
      usersv.clear();
      for (Instruction *user : keep) usersv.push_back_within_capacity(user);
    }
    usersa.push_back(&alloc_, phi_accu);
    usersa.push_back(&alloc_, phi_join);
    usersv.push_back(&alloc_, phi_accu);
    usersv.push_back(&alloc_, phi_join);
  }
  auto createConstant(llvm::ConstantInt *c, Value *&n) -> LoopInvariant * {
    LoopInvariant *cnst =
      (c->getBitWidth() <= 64)
        ? (LoopInvariant *)createConstant(c->getType(), c->getSExtValue())
        : (LoopInvariant *)alloc_.create<Bint>(c, c->getType());
    n = cnst;
    return cnst;
  }
  auto createConstant(llvm::ConstantFP *f, Value *&n) -> LoopInvariant * {
    Bflt *cnst = alloc_.create<Bflt>(f, f->getType());
    n = cnst;
    return cnst;
  }
  auto createConstant(llvm::ConstantFP *f) -> Bflt * {
    return alloc_.create<Bflt>(f, f->getType());
  }
  auto createConstant(map<llvm::Value *, Value *> *llvmToInternalMap,
                      llvm::ConstantFP *f) -> Bflt * {
    Value *&n = (*llvmToInternalMap)[f];
    if (n) return static_cast<Bflt *>(n);
    Bflt *cnst = alloc_.create<Bflt>(f, f->getType());
    n = cnst;
    return cnst;
  }
  // auto createConstant(llvm::Type *typ, int64_t v) -> Cint * {
  //   Cint *c = alloc.create<Cint>(v, typ);
  //   constMap[LoopInvariant::Identifier(typ, v)] = c;
  //   return static_cast<Cint *>(c);
  // }
  auto createConstant(llvm::Type *typ, long long v) -> Cint * {
    LoopInvariant *&c = const_map_[LoopInvariant::Identifier(typ, v)];
    if (!c) c = alloc_.create<Cint>(v, typ);
    return static_cast<Cint *>(c);
  }
  auto createConstant(llvm::Type *typ, long v) -> Cint * {
    return createConstant(typ, (long long)v);
  }
  auto createConstant(llvm::Type *typ, int v) -> Cint * {
    return createConstant(typ, (long long)v);
  }
  auto getArgument(llvm::Type *typ, int64_t number) -> FunArg * {
    LoopInvariant *&c = const_map_[LoopInvariant::Identifier(
      typ, LoopInvariant::Argument{number})];
    if (!c) c = alloc_.create<FunArg>(number, typ);
    return static_cast<FunArg *>(c);
  }
  auto createConstant(llvm::Type *typ, double v) -> Cflt * {
    LoopInvariant *&c = const_map_[LoopInvariant::Identifier(typ, v)];
    if (!c) c = alloc_.create<Cflt>(v, typ);
    return static_cast<Cflt *>(c);
  }
  auto createConstantVal(llvm::Value *val, Value *&n) -> CVal * {
    LoopInvariant *&c = const_map_[LoopInvariant::Identifier(val)];
    if (!c) c = alloc_.create<CVal>(val);
    n = c;
    return static_cast<CVal *>(c);
  }
  auto createCondition(Predicate::Relation rel, Compute *instr,
                       bool swap = false) -> Value * {
    switch (rel) {
    case Predicate::Relation::Any:
      return Cint::create(&alloc_, 1, instr->getType());
    case Predicate::Relation::Empty:
      return Cint::create(&alloc_, 0, instr->getType());
    case Predicate::Relation::False: swap = !swap; [[fallthrough]];
    case Predicate::Relation::True: return swap ? negate(instr) : instr;
    }
  }
  static auto getFastMathFlags(Value *V) -> llvm::FastMathFlags {
    if (auto *C = llvm::dyn_cast<Compute>(V)) return C->getFastMathFlags();
    return {};
  }
  auto negate(Value *V) -> Value * {
    // first, check if its parent is a negation
    if (auto op = Operation(V); op &&
                                op.isInstruction(llvm::Instruction::Xor) &&
                                (op.getNumOperands() == 2)) {
      // !x where `x isa bool` is represented as `x ^ true`
      auto *op0 = op.getOperand(0);
      auto *op1 = op.getOperand(1);
      if (isConstantOneInt(op1)) return op0;
      if (isConstantOneInt(op0)) return op1;
    }
    Cint *one = createConstant(V->getType(), 1);
    return createOperation(llvm::Instruction::Xor,
                           std::array<Value *, 2>{V, one}, V->getType(),
                           getFastMathFlags(V));
  }
  auto createCondition(Predicate::Intersection pred, UList<Value *> *predicates,
                       bool swap) -> Value * {
    size_t pop_count = pred.popCount();
    // 0: Any; no restriction
    // 1: True; requires single predicate is true
    if (pop_count == 0) return createConstant((*predicates)[0]->getType(), 1);
    if (pop_count == 1) {
      ptrdiff_t ind = pred.getFirstIndex();
      Value *I = (*predicates)[ind];
      return swap ? negate(I) : I;
    }
    // we have more than one instruction
    ptrdiff_t ind = pred.getFirstIndex();
    Value *J = (*predicates)[ind];
    ind = pred.getNextIndex(ind);
    // we keep I &= predicates[ind] until ind is invalid
    // ind will be >= 32 when it is invalid
    // getNextIndex will return a valid answer at least once, because
    // popCount > 1
    // there may be a better order than folding from the left
    // e.g. a binary tree could allow for more out of order execution
    // but I think a later pass should handle that sort of associativity
    do {
      J = getOperation(llvm::Instruction::And,
                       std::array<Value *, 2>{J, (*predicates)[ind]},
                       J->getType(), getFastMathFlags(J));
      ind = pred.getNextIndex(ind);
    } while (ind < 32);
    return J;
  }
  auto createSelect(Predicate::Intersection P, Value *A, Value *B,
                    UList<Value *> *pred) -> Compute * {
    // What I need here is to take the union of the predicates to form
    // the predicates of the new select instruction. Then, for the
    // select's `cond` instruction, I need something to indicate when to
    // take one path and not the other. We know the intersection is
    // empty, so -- why is it empty? We need something to slice that.
    // E.g.
    /// if *A = [(a & b) | (c & d)]
    ///    *B = [(e & f) | (g & h)]
    /// then
    /// [(a & b) | (c & d)] & [(e & f) | (g & h)] =
    ///   [(a & b) & (e & f)] |
    ///   [(a & b) & (g & h)] |
    ///   [(c & d) & (e & f)] |
    ///   [(c & d) & (g & h)]
    /// for this to be empty, we need to have
    ///   [(a & b) & (e & f)] =
    ///   [(a & b) & (g & h)] =
    ///   [(c & d) & (e & f)] =
    ///   [(c & d) & (g & h)] = 0
    /// Suggestion: loop over union elements,
    /// and take the set of all of the conditions for
    /// each side.
    /// Then use the simpler of these two to determine the direction of
    /// the select.
    assert(!P.empty() && "No conflict between predicates");
    bool swap = P.countFalse() <= P.countTrue();
    Value *cond = createCondition(P, pred, swap);
    Value *op1 = swap ? B : A;
    Value *op2 = swap ? A : B;
    llvm::Type *typ = A->getType();
    llvm::FastMathFlags fmf;
    if (typ->isFloatingPointTy()) {
      fmf |= getFastMathFlags(A);
      fmf |= getFastMathFlags(B);
    }
    return getOperation(llvm::Instruction::Select,
                        std::array<Value *, 3>{cond, op1, op2}, typ, fmf);
  }
  // adds predicate P to address A
  void addPredicate(Addr *A, Predicate::Set P, Predicate::Map *M) {
    if (P.empty()) return;
    auto *predicates = M->getPredicates();
    // the set is a union of intersections
    // so we materialize it in the most naive way; TODO: less naive?
    Value *pred =
      P.transform_reduce(nullptr, [&](Value *acc, Predicate::Intersection I) {
        Value *v = createCondition(I, predicates, false);
        if (acc)
          v = createOperation(llvm::Instruction::Or,
                              std::array<Value *, 2>{acc, v}, acc->getType(),
                              getFastMathFlags(acc));
        return v;
      });
    A->setPredicate(pred);
  }
  [[nodiscard]] auto addPredicate(Arena<> *alloc, Predicate::Map *m,
                                  llvm::Value *value, LLVMIRBuilder LB,
                                  TreeResult &tr) -> ptrdiff_t {
    auto [I, tret] = getValue(value, nullptr, LB, tr);
    tr = tret;
    // assert(predicates->count <= 32 && "too many predicates");
    ptrdiff_t i = 0;
    for (auto *U = m->getPredicates(); U != nullptr; U = U->getNext())
      for (ptrdiff_t j = 0, N = U->getHeadCount(); j < N; ++i, ++j)
        if ((*U)[j] == I) return i;
    m->getPredicates()->push_ordered(alloc, I);
    return i;
  }

  auto push_array(IR::Value *base, PtrVector<IR::Value *> sizes) -> Array {
    auto c = alloc_.checkpoint();
    ptrdiff_t L = sizes.size();
    MutPtrVector<IR::Value *> sz = math::vector<IR::Value *>(&alloc_, L);
    std::copy_n(sizes.begin(), L, sz.begin());
    auto [a, f] = ir_arrays_.emplace_back(base, sz);
    if (f) alloc_.rollback(c);
    return a;
  }
};

constexpr auto getAlloc(Cache &cache) -> Arena<> * {
  return cache.getAllocator();
}
constexpr auto getDataLayout(Cache &cache) -> const llvm::DataLayout & {
  return cache.dataLayout();
}

} // namespace IR
