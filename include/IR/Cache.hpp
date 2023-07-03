#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/BBPredPath.hpp"
#include "IR/Instruction.hpp"
#include "IR/Node.hpp"
#include "IR/Predicate.hpp"
#include "Utilities/ListRanges.hpp"
#include <cstddef>
#include <llvm/Analysis/Delinearization.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/FMF.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/Casting.h>

namespace poly::IR {
using dict::map;
/// The `TreeResult` gives the result of parsing a loop tree.
/// The purpose of the `TreeResult` is to accumulate results while
/// building the loop tree, in particular, the `Addr`s so far,
/// the incomplete instructions we must complete as we move out,
/// and how many outer loop layers we are forced to reject.
///
/// We parse `Addr`s specifically inside the `TurboLoop` parse block function,
/// and add the appropriate `omega` value then.
///
/// Fields:
/// - `Addr* load`: a linked list giving the loads of the loop tree.
/// These contain ordering information, which is enough for the linear program
/// to deduce the orders of memory accesses, and perform an analysis.
/// Note that adding loads and stores always pushes to the front.
/// Thus, old `TreeResult`s are not invalidated; they just start at the middle
/// of the grown list.
/// - `Addr* stow`: same as load, but for stores.
/// - 'Instruction* incomplete': a linked list giving the nodes that we
/// stopped exploring due to not being inside the loop nest, and thus may need
/// to have their parents filled out.
/// - `size_t rejectDepth` is how many outer loops were rejected, due to to
/// failure to produce an affine representation of the loop or memory
/// accesses. Either because an affine representation is not possible, or
/// because our analysis failed and needs improvement.
///
/// We use setChild for setting the last load/stow/incomplete
/// only the very first is guaranteed to be correct, as we
/// do not update the old when concatenating
struct TreeResult {
  /// `Addr`s, sorted `[stow..., load...]`
  /// stow's getChild() points to last stow
  /// load's getChild() points to last load
  Addr *addr{nullptr};
  Compute *incomplete{nullptr};
  unsigned rejectDepth{0};
  unsigned maxDepth{0};
  [[nodiscard]] constexpr auto reject(unsigned depth) const -> bool {
    return (depth < rejectDepth) || (addr == nullptr);
  }
  [[nodiscard]] constexpr auto accept(size_t depth) const -> bool {
    // depth >= rejectDepth && stow != nullptr
    return !reject(depth);
  }
  constexpr void addIncomplete(Compute *I) {
    Node *last = incomplete ? incomplete->getChild() : I;
    incomplete = static_cast<Compute *>(I->setNext(incomplete));
    I->setChild(last);
  }
  // TODO: single load/stow list, but sorted
  // we accumulate `maxDepth` as we go
  // Newly constructed addrs have enough space for the max depth,
  // so we can resize mostly in place later.
  // we have all addr in a line
  constexpr void addAddr(Addr *A) {
    if (!addr || addr->isLoad()) addr = A->insertNextAddr(addr);
    else getLastStore()->insertNextAddr(A);
    if (addr->isLoad()) {
      Addr *L = addr->getNextAddr();
      addr->setChild(L ? L->getChild() : addr);
    } else addr->setChild(A);
  }
  [[nodiscard]] constexpr auto getAddr() const {
    return utils::ListRange(static_cast<Addr *>(addr), NextAddr{});
  }
  [[nodiscard]] constexpr auto getLoads() const {
    return utils::ListRange(getFirstLoad(), NextAddr{});
  }
  [[nodiscard]] constexpr auto getStores() const {
    Addr *S = (addr && addr->isStore()) ? addr : nullptr;
    return utils::ListRange(S, [](Addr *A) -> Addr * {
      Addr *S = A->getNextAddr();
      if (S && S->isStore()) return S;
      return nullptr;
    });
  }
  void setLoopNest(NotNull<poly::Loop> L) const {
    for (Addr *A : getAddr()) A->setLoopNest(L);
  }
  constexpr auto operator*=(TreeResult tr) -> TreeResult & {
    if (tr.addr) {
      if (addr && addr->isStore()) {
        // [this_stow..., other..., this_load...]
        Addr *LS = getLastStore(), *FL = LS->getNextAddr();
        LS->setNextAddr(tr.addr);
        tr.getLastAddr()->setNextAddr(FL);
      } else {
        // [other..., this_load...]
        tr.getLastAddr()->setNextAddr(addr);
        addr = tr.addr;
      }
    }
    incomplete = concatenate(incomplete, tr.incomplete);
    rejectDepth = std::max(rejectDepth, tr.rejectDepth);
    return *this;
  }

  [[nodiscard]] constexpr auto getLoop() const -> poly::Loop * {
    return (addr) ? addr->getLoop() : nullptr;
  }

private:
  static constexpr auto concatenate(Compute *A, Compute *B) -> Compute * {
    if (!A) return B;
    if (!B) return A;
    A->getChild()->setNext(B);
    A->setChild(B->getChild());
    return A;
  }
  [[nodiscard]] constexpr auto getFirstStore() const -> Addr * {
    return (addr && addr->isStore()) ? addr : nullptr;
  }
  [[nodiscard]] constexpr auto getLastStore() const -> Addr * {
    if (!addr || addr->isLoad()) return nullptr;
    return llvm::cast<Addr>(addr->getChild());
  }
  [[nodiscard]] constexpr auto getFirstLoad() const -> Addr * {
    if (!addr || addr->isLoad()) return addr;
    return llvm::cast<Addr>(addr->getChild())->getNextAddr();
  }
  [[nodiscard]] constexpr auto getLastLoad() const -> Addr * {
    Addr *L = getFirstLoad();
    return L ? llvm::cast<Addr>(L->getChild()) : nullptr;
  }
  [[nodiscard]] constexpr auto getLastAddr() const -> Addr * {
    if (!addr) return nullptr;
    // if (addr->isLoad()) return llvm::cast<Addr>(addr->getChild());
    Addr *C = llvm::cast<Addr>(addr->getChild());
    if (C->isLoad()) return C;
    Addr *L = C->getNextAddr();
    return L ? llvm::cast<Addr>(L->getChild()) : C;
  }
  struct NextAddr {
    constexpr auto operator()(Addr *A) const -> Addr * {
      return A->getNextAddr();
    }
  };
};

class Cache {
  map<llvm::Value *, Value *> llvmToInternalMap;
  map<InstByValue, Compute *> instCSEMap;
  map<Cnst::Identifier, Cnst *> constMap;
  utils::OwningArena<> alloc;
  llvm::LoopInfo *LI;
  llvm::ScalarEvolution *SE;
  Compute *freeInstList{nullptr}; // positive numOps/complete, but empty
  auto allocateInst(unsigned numOps) -> Compute * {
    // because we allocate children before parents
    for (Compute *I = freeInstList; I;
         I = static_cast<Compute *>(I->getNext())) {
      if (I->getNumOperands() != numOps) continue;
      I->removeFromList();
      return I;
    }
    // not found, allocate
    return static_cast<Compute *>(
      alloc.allocate(sizeof(Compute) + sizeof(Value *) * numOps));
  }

  auto getCSE(Compute *I) -> Compute *& { return instCSEMap[InstByValue{I}]; }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto createValue(llvm::Value *v, Predicate::Map *M, TreeResult tr, Value *&n)
    -> std::pair<Value *, TreeResult> {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v))
      return createInstruction(i, M, tr, n);
    if (auto *c = llvm::dyn_cast<llvm::ConstantInt>(v))
      return {createConstant(c, n), tr};
    if (auto *c = llvm::dyn_cast<llvm::ConstantFP>(v))
      return {createConstant(c, n), tr};
    return {createConstantVal(v, n), tr};
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  constexpr void replaceUsesByUsers(Value *oldNode, Value *newNode) {
    invariant(oldNode->getKind() == Node::VK_Load ||
              oldNode->getKind() >= Node::VK_Func);
    for (Instruction *user : oldNode->getUsers()) {
      // newNode may depend on old (e.g. when merging)
      if (user == newNode) continue;
      if (auto *I = llvm::dyn_cast<Compute>(user)) {
        for (Value *&o : I->getOperands())
          if (o == oldNode) o = newNode;
        user = cse(I); // operands changed, try to CSE
      } else {
        Addr *addr = llvm::cast<Addr>(user);
        // could be load or store; either are predicated
        // we might have `if (b) store(b)`, hence need both checks?
        bool isPred = addr->getPredicate() == oldNode,
             isStored = addr->isStore() && addr->getStoredVal() == oldNode;
        invariant(isPred || isStored); // at least one must be true!
        if (isPred) addr->setPredicate(newNode);
        if (isStored) addr->setVal(newNode);
      }
      if (newNode->getKind() != Node::VK_Stow) newNode->addUser(&alloc, user);
    }
    oldNode->getUsers().clear();
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
  static auto blackListAllDependentLoops(const llvm::SCEV *S, size_t numPeeled)
    -> uint64_t {
    return blackListAllDependentLoops(S) >> (numPeeled + 1);
  }
  // translates scev S into loops and symbols
  auto // NOLINTNEXTLINE(misc-no-recursion)
  fillAffineIndices(MutPtrVector<int64_t> v, int64_t *coffset,
                    math::Vector<int64_t> &offsets,
                    llvm::SmallVector<const llvm::SCEV *, 3> &symbolicOffsets,
                    const llvm::SCEV *S, int64_t mlt, size_t numPeeled)
    -> uint64_t {
    using ::poly::poly::getConstantInt;
    uint64_t blackList{0};
    if (const auto *x = llvm::dyn_cast<const llvm::SCEVAddRecExpr>(S)) {
      const llvm::Loop *L = x->getLoop();
      size_t depth = L->getLoopDepth();
      if (depth <= numPeeled) {
        // we effectively have an offset
        // we'll add an
        addSymbolic(offsets, symbolicOffsets, S, 1);
        for (size_t i = 1; i < x->getNumOperands(); ++i)
          blackList |= blackListAllDependentLoops(x->getOperand(i));

        return blackList;
      }
      // outermost loop has loopInd 0
      ptrdiff_t loopInd = ptrdiff_t(depth) - ptrdiff_t(numPeeled + 1);
      if (x->isAffine()) {
        if (loopInd >= 0) {
          if (auto c = getConstantInt(x->getOperand(1))) {
            // we want the innermost loop to have index 0
            v[loopInd] += *c;
            return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                     x->getOperand(0), mlt, numPeeled);
          }
          blackList |= (uint64_t(1) << uint64_t(loopInd));
        }
        // we separate out the addition
        // the multiplication was either peeled or involved
        // non-const multiple
        blackList |= fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                       x->getOperand(0), mlt, numPeeled);
        // and then add just the multiple here as a symbolic offset
        const llvm::SCEV *addRec = SE->getAddRecExpr(
          SE->getZero(x->getOperand(0)->getType()), x->getOperand(1),
          x->getLoop(), x->getNoWrapFlags());
        addSymbolic(offsets, symbolicOffsets, addRec, mlt);
        return blackList;
      }
      if (loopInd >= 0) blackList |= (uint64_t(1) << uint64_t(loopInd));
    } else if (std::optional<int64_t> c = getConstantInt(S)) {
      *coffset += *c;
      return 0;
    } else if (const auto *ar = llvm::dyn_cast<const llvm::SCEVAddExpr>(S)) {
      return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                               ar->getOperand(0), mlt, numPeeled) |
             fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                               ar->getOperand(1), mlt, numPeeled);
    } else if (const auto *m = llvm::dyn_cast<const llvm::SCEVMulExpr>(S)) {
      if (auto op0 = getConstantInt(m->getOperand(0))) {
        return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                 m->getOperand(1), mlt * (*op0), numPeeled);
      }
      if (auto op1 = getConstantInt(m->getOperand(1))) {
        return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                                 m->getOperand(0), mlt * (*op1), numPeeled);
      }
    } else if (const auto *ca = llvm::dyn_cast<llvm::SCEVCastExpr>(S))
      return fillAffineIndices(v, coffset, offsets, symbolicOffsets,
                               ca->getOperand(0), mlt, numPeeled);
    addSymbolic(offsets, symbolicOffsets, S, mlt);
    return blackList | blackListAllDependentLoops(S, numPeeled);
  }
  static void extendDensePtrMatCols(Arena<> *alloc,
                                    MutDensePtrMatrix<int64_t> &A, math::Row R,
                                    math::Col C) {
    MutDensePtrMatrix<int64_t> B{matrix<int64_t>(alloc, A.numRow(), C)};
    for (ptrdiff_t j = 0; j < R; ++j) {
      B(j, _(0, A.numCol())) << A(j, _);
      B(j, _(A.numCol(), end)) << 0;
    }
    std::swap(A, B);
  }
  void setOperands(Compute *op, PtrVector<Value *> ops) {
    size_t N = ops.size();
    MutPtrVector<Value *> operands{op->getOperands()};
    for (size_t n = 0; n < N; ++n) {
      Value *operand = operands[n] = ops[n];
      operand->addUser(&alloc, op);
    }
  }

public:
  [[nodiscard]] constexpr auto getScalarEvolution() const
    -> llvm::ScalarEvolution * {
    return SE;
  }
  /// complete the operands
  // NOLINTNEXTLINE(misc-no-recursion)
  auto complete(Compute *I, Predicate::Map *M, TreeResult tr)
    -> std::pair<Compute *, TreeResult> {
    auto *i = I->getLLVMInstruction();
    unsigned nOps = I->numCompleteOps();
    auto ops = I->getOperands();
    for (unsigned j = 0; j < nOps; ++j) {
      auto *op = i->getOperand(j);
      auto [v, tret] = getValue(op, M, tr);
      tr = tret;
      ops[j] = v;
      v->addUser(&alloc, I);
    }
    return {cse(I), tr};
  }
  // update list of incomplets
  inline auto completeInstructions(Predicate::Map *M, TreeResult tr)
    -> std::pair<Compute *, TreeResult> {
    Compute *completed = nullptr;
    for (Compute *I = tr.incomplete; I;
         I = static_cast<Compute *>(I->getNext())) {
      if (!M->contains(I->getLLVMInstruction())) continue;
      I->removeFromList();
      auto [ct, trt] = complete(I, M, tr);
      completed = static_cast<Compute *>(ct->setNext(completed));
      tr = trt;
    }
    return {completed, tr};
  }
  /// Get the cache's allocator.
  /// This is a long-lived bump allocator, mass-freeing after each
  /// sub-tree optimization.
  constexpr auto getAllocator() -> Arena<> * { return &alloc; }
  /// try to remove `I` as a duplicate
  /// this travels downstream;
  /// if `I` is eliminated, all users of `I`
  /// get updated, making them CSE-candiates.
  /// In this manner, we travel downstream through users.
  // NOLINTNEXTLINE(misc-no-recursion)
  auto cse(Compute *I) -> Compute * {
    Compute *&cse = getCSE(I);
    if (cse == nullptr || (cse == I)) return cse = I; // update ref
    replaceAllUsesWith(I, cse);
    I->removeFromList();
    I->setNext(freeInstList);
    freeInstList = I;
    return cse;
  }
  /// replaceAllUsesWith(Value *oldNode, Value *newNode)
  /// replaces all uses of `oldNode` with `newNode`
  /// updating the operands of all users of `oldNode`
  /// and the `users` of all operands of `oldNode`
  // NOLINTNEXTLINE(misc-no-recursion)
  constexpr void replaceAllUsesWith(Instruction *oldNode, Value *newNode) {
    invariant(oldNode->getKind() == Node::VK_Load ||
              oldNode->getKind() >= Node::VK_Func);
    replaceUsesByUsers(oldNode, newNode);
    // every operand of oldNode needs their users updated
    if (auto *I = llvm::dyn_cast<Compute>(oldNode)) {
      for (Value *&n : I->getOperands()) n->removeFromUsers(oldNode);
      I->setNext(freeInstList);
      freeInstList = I;
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
  auto getValue(llvm::Value *v, Predicate::Map *M, TreeResult tr)
    -> std::pair<Value *, TreeResult> {
    Value *&n = llvmToInternalMap[v];
    if (n) return {n, tr};
    // by reference, so we can update in creation
    return createValue(v, M, tr, n);
  }
  auto getValue(llvm::Instruction *I, Predicate::Map *M, TreeResult tr)
    -> std::pair<Instruction *, TreeResult> {
    auto [v, tret] = getValue(static_cast<llvm::Value *>(I), M, tr);
    return {llvm::cast<Instruction>(v), tret};
  }

  // NOLINTNEXTLINE(misc-no-recursion)
  auto createInstruction(llvm::Instruction *I, Predicate::Map *M, TreeResult tr,
                         Value *&t) -> std::pair<Value *, TreeResult> {
    auto *load = llvm::dyn_cast<llvm::LoadInst>(I);
    auto *store = llvm::dyn_cast<llvm::StoreInst>(I);
    if (!load && !store) return createCompute(I, M, tr, t);
    auto *ptr = load ? load->getPointerOperand() : store->getPointerOperand();
    llvm::Loop *L = LI->getLoopFor(I->getParent());
    auto [v, tret] = createArrayRef(I, L, ptr, tr);
    t = v;
    if (Addr *A = llvm::dyn_cast<Addr>(v); store && A) {
      // only Computes may be incomplete, so we unconditionally get the store
      // value
      auto [v2, tret2] = getValue(store->getValueOperand(), M, tret);
      A->setVal(v2);
      tret = tret2;
    }
    return {v, tret};
  }

  // NOLINTNEXTLINE(misc-no-recursion)
  auto createCompute(llvm::Instruction *I, Predicate::Map *M, TreeResult tr,
                     Value *&t) -> std::pair<Compute *, TreeResult> {
    auto [id, kind] = Compute::getIDKind(I);
    int numOps = int(I->getNumOperands());
    Compute *n = std::construct_at(allocateInst(numOps), kind, I, id, -numOps);
    t = n;
    if (M && M->contains(I)) {
      auto [v, tret] = complete(n, M, tr);
      n = v;
      tr = tret;
    } else tr.addIncomplete(n);
    return {n, tr};
  }

  auto zeroDimRef(llvm::Instruction *loadOrStore,
                  llvm::SCEVUnknown const *arrayPtr, unsigned numLoops)
    -> Addr * {
    return Addr::construct(&alloc, arrayPtr, loadOrStore, numLoops);
  }
  // create Addr
  auto getArrayRef(llvm::Instruction *loadOrStore, llvm::Loop *L,
                   llvm::Value *ptr, TreeResult tr)
    -> std::pair<Value *, TreeResult> {
    Value *&n = llvmToInternalMap[loadOrStore];
    if (n) return {n, tr};
    auto ret = createArrayRef(loadOrStore, L, ptr, tr);
    n = ret.first;
    return ret;
  }
  // create Addr
  auto createArrayRef(llvm::Instruction *loadOrStore, llvm::Value *ptr,
                      TreeResult tr) -> std::pair<Value *, TreeResult> {
    llvm::Loop *L = LI->getLoopFor(loadOrStore->getParent());
    return createArrayRef(loadOrStore, L, ptr, tr);
  }
  // create Addr
  auto createArrayRef(llvm::Instruction *loadOrStore, llvm::Loop *L,
                      llvm::Value *ptr, TreeResult tr)
    -> std::pair<Value *, TreeResult> {
    const auto *elSz = SE->getElementSize(loadOrStore);
    const llvm::SCEV *accessFn = SE->getSCEVAtScope(ptr, L);
    unsigned numLoops = L->getLoopDepth();
    if (!ptr) {
      tr.rejectDepth = std::max(tr.rejectDepth, numLoops);
      return {alloc.create<CVal>(loadOrStore), tr};
    }
    return createArrayRef(loadOrStore, accessFn, numLoops, elSz, tr);
  }
  auto createArrayRef(llvm::Instruction *loadOrStore,
                      const llvm::SCEV *accessFn, unsigned numLoops,
                      const llvm::SCEV *elSz, TreeResult tr)
    -> std::pair<Value *, TreeResult> {
    // https://llvm.org/doxygen/Delinearization_8cpp_source.html#l00582

    const llvm::SCEV *pb = SE->getPointerBase(accessFn);
    const auto *arrayPtr = llvm::dyn_cast<llvm::SCEVUnknown>(pb);
    // Do not delinearize if we cannot find the base pointer.
    if (!arrayPtr) {
      tr.rejectDepth = std::max(tr.rejectDepth, numLoops);
      return {alloc.create<CVal>(loadOrStore), tr};
    }
    accessFn = SE->getMinusSCEV(accessFn, arrayPtr);
    llvm::SmallVector<const llvm::SCEV *, 3> subscripts, sizes;
    llvm::delinearize(*SE, accessFn, subscripts, sizes, elSz);
    unsigned numDims = subscripts.size();
    invariant(size_t(numDims), sizes.size());
    if (numDims == 0) return {zeroDimRef(loadOrStore, arrayPtr, 0), tr};
    unsigned numPeeled = tr.rejectDepth;
    numLoops -= numPeeled;
    math::IntMatrix Rt{math::StridedDims{numDims, numLoops}, 0};
    llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets;
    uint64_t blackList{0};
    math::Vector<int64_t> coffsets{unsigned(numDims), 0};
    MutDensePtrMatrix<int64_t> offsMat{nullptr, DenseDims{numDims, 0}};
    {
      math::Vector<int64_t> offsets;
      for (ptrdiff_t i = 0; i < numDims; ++i) {
        offsets << 0;
        blackList |=
          fillAffineIndices(Rt(i, _), &coffsets[i], offsets, symbolicOffsets,
                            subscripts[i], 1, numPeeled);
        if (offsets.size() > offsMat.numCol())
          extendDensePtrMatCols(&alloc, offsMat, math::Row{i},
                                math::Col{offsets.size()});
        offsMat(i, _) << offsets;
      }
    }
    size_t numExtraLoopsToPeel = 64 - std::countl_zero(blackList);
    Addr *op = Addr::construct(&alloc, arrayPtr, loadOrStore,
                               Rt(_, _(numExtraLoopsToPeel, end)),
                               {std::move(sizes), std::move(symbolicOffsets)},
                               coffsets, offsMat.data(), numLoops, tr.maxDepth);
    tr.addAddr(op);
    tr.rejectDepth += numExtraLoopsToPeel;
    return {op, tr};
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
    invariant(A->getNumOperands(), ops.size());
    return createCompute(A->getOpId(), A->getKind(), ops, A->getType(),
                         A->getFastMathFlags());
  }
  template <size_t N>
  auto getOperation(llvm::Intrinsic::ID opId, std::array<Value *, N> ops,
                    llvm::Type *typ, llvm::FastMathFlags fmf) -> Compute * {
    Compute *op = createOperation(opId, ops, typ, fmf);
    Compute *&cse = getCSE(op);
    if (cse == nullptr || (cse == op)) return cse = op; // update ref
    op->setNext(freeInstList);
    freeInstList = op;
    return cse;
  }

  auto operator[](llvm::Value *v) -> Value * {
    auto f = llvmToInternalMap.find(v);
    if (f != llvmToInternalMap.end()) return f->second;
    return nullptr;
  }
  auto createConstant(llvm::ConstantInt *c, Value *&n) -> Cnst * {
    Cnst *cnst = (c->getBitWidth() <= 64)
                   ? (Cnst *)createConstant(c->getType(), c->getSExtValue())
                   : (Cnst *)alloc.create<Bint>(c, c->getType());
    n = cnst;
    return cnst;
  }
  auto createConstant(llvm::ConstantFP *f, Value *&n) -> Cnst * {
    Bflt *cnst = alloc.create<Bflt>(f, f->getType());
    n = cnst;
    return cnst;
  }
  auto createConstant(llvm::ConstantFP *f) -> Bflt * {
    Value *&n = llvmToInternalMap[f];
    if (n) return static_cast<Bflt *>(n);
    Bflt *cnst = alloc.create<Bflt>(f, f->getType());
    n = cnst;
    return cnst;
  }
  auto createConstant(llvm::Type *typ, int64_t v) -> Cint * {
    Cint *c = alloc.create<Cint>(v, typ);
    constMap[Cnst::Identifier(typ, v)] = c;
    return static_cast<Cint *>(c);
  }
  auto getConstant(llvm::Type *typ, int64_t v) -> Cint * {
    Cnst *&c = constMap[Cnst::Identifier(typ, v)];
    if (!c) c = createConstant(typ, v);
    return static_cast<Cint *>(c);
  }
  auto createConstant(llvm::Type *typ, double v) -> Cflt * {
    return alloc.create<Cflt>(v, typ);
  }
  auto createConstantVal(llvm::Value *val, Value *&n) -> CVal * {
    CVal *v = alloc.create<CVal>(val);
    n = v;
    return v;
  }
  auto createCondition(Predicate::Relation rel, Compute *instr,
                       bool swap = false) -> Value * {
    switch (rel) {
    case Predicate::Relation::Any:
      return Cint::create(&alloc, 1, instr->getType());
    case Predicate::Relation::Empty:
      return Cint::create(&alloc, 0, instr->getType());
    case Predicate::Relation::False: swap = !swap; [[fallthrough]];
    case Predicate::Relation::True: return swap ? negate(instr) : instr;
    }
  }
  auto negate(Value *I) -> Value * {
    // first, check if its parent is a negation
    if (auto op = Operation(I); op &&
                                op.isInstruction(llvm::Instruction::Xor) &&
                                (op.getNumOperands() == 2)) {
      // !x where `x isa bool` is represented as `x ^ true`
      auto *op0 = op.getOperand(0);
      auto *op1 = op.getOperand(1);
      if (isConstantOneInt(op1)) return op0;
      if (isConstantOneInt(op0)) return op1;
    }
    Cint *one = getConstant(I->getType(), 1);
    return createOperation(llvm::Instruction::Xor,
                           std::array<Value *, 2>{I, one}, I->getType(),
                           I->getFastMathFlags());
  }
  auto createCondition(Predicate::Intersection pred, UList<Value *> *predicates,
                       bool swap) -> Value * {
    size_t popCount = pred.popCount();
    // 0: Any; no restriction
    // 1: True; requires single predicate is true
    if (popCount == 0) return getConstant((*predicates)[0]->getType(), 1);
    if (popCount == 1) {
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
                       J->getType(), J->getFastMathFlags());
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
      fmf |= A->getFastMathFlags();
      fmf |= B->getFastMathFlags();
    }
    return getOperation(llvm::Instruction::Select,
                        std::array<Value *, 3>{cond, op1, op2}, typ, fmf);
  }
  // adds predicate P to address A
  void addPredicate(IR::Addr *A, Predicate::Set P, Predicate::Map *M) {
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
                              acc->getFastMathFlags());
        return v;
      });
    A->setPredicate(pred);
  }
};

namespace Predicate {
[[nodiscard]] inline auto Map::addPredicate(Arena<> *alloc, IR::Cache &cache,
                                            llvm::Value *value,
                                            IR::TreeResult &tr) -> size_t {
  auto [I, tret] = cache.getValue(value, nullptr, tr);
  tr = tret;
  // assert(predicates->count <= 32 && "too many predicates");
  size_t i = 0;
  for (auto *U = predicates; U != nullptr; U = U->getNext())
    for (ptrdiff_t j = 0, N = U->getHeadCount(); j < N; ++i, ++j)
      if ((*U)[j] == I) return i;
  predicates->push_ordered(alloc, I);
  return i;
}

[[nodiscard]] inline auto // NOLINTNEXTLINE(misc-no-recursion)
descendBlock(Arena<> *alloc, IR::Cache &cache,
             dict::aset<llvm::BasicBlock *> &visited, Map &predMap,
             llvm::BasicBlock *BBsrc, llvm::BasicBlock *BBdst,
             Intersection predicate, llvm::BasicBlock *BBhead, llvm::Loop *L,
             TreeResult &tr) -> Map::Destination {
  if (BBsrc == BBdst) {
    assert(!predMap.contains(BBsrc));
    predMap.insert({BBsrc, Set{predicate}});
    return Map::Destination::Reached;
  }
  if (L && (!(L->contains(BBsrc)))) {
    // oops, we have skipped the preheader and escaped the loop.
    return Map::Destination::Returned;
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
      return Map::Destination::Reached;
    return Map::Destination::Returned;
  }
  // Inserts a tombstone to indicate that we have visited BBsrc, but
  // not actually reached a destination.
  visited.insert(BBsrc);
  const llvm::Instruction *I = BBsrc->getTerminator();
  if (!I) return Map::Destination::Unknown;
  if (llvm::isa<llvm::ReturnInst>(I)) return Map::Destination::Returned;
  if (llvm::isa<llvm::UnreachableInst>(I)) return Map::Destination::Unreachable;
  const auto *BI = llvm::dyn_cast<llvm::BranchInst>(I);
  if (!BI) return Map::Destination::Unknown;
  if (BI->isUnconditional()) {
    auto rc = descendBlock(alloc, cache, visited, predMap, BI->getSuccessor(0),
                           BBdst, predicate, BBhead, L, tr);
    if (rc == Map::Destination::Reached) predMap.reach(alloc, BBsrc, predicate);
    return rc;
  }
  // We have a conditional branch.
  llvm::Value *cond = BI->getCondition();
  // We need to check both sides of the branch and add a predicate.
  size_t predInd = predMap.addPredicate(alloc, cache, cond, tr);
  auto rc0 =
    descendBlock(alloc, cache, visited, predMap, BI->getSuccessor(0), BBdst,
                 predicate.intersect(predInd, Relation::True), BBhead, L, tr);
  if (rc0 == Map::Destination::Unknown) // bail
    return Map::Destination::Unknown;
  auto rc1 =
    descendBlock(alloc, cache, visited, predMap, BI->getSuccessor(1), BBdst,
                 predicate.intersect(predInd, Relation::False), BBhead, L, tr);
  if ((rc0 == Map::Destination::Returned) ||
      (rc0 == Map::Destination::Unreachable)) {
    if (rc1 == Map::Destination::Reached) {
      //  we're now assuming that !cond
      predMap.assume(Intersection(predInd, Relation::False));
      predMap.reach(alloc, BBsrc, predicate);
    }
    return rc1;
  }
  if ((rc1 == Map::Destination::Returned) ||
      (rc1 == Map::Destination::Unreachable)) {
    if (rc0 == Map::Destination::Reached) {
      //  we're now assuming that cond
      predMap.assume(Intersection(predInd, Relation::True));
      predMap.reach(alloc, BBsrc, predicate);
    }
    return rc0;
  }
  if (rc0 != rc1) return Map::Destination::Unknown;
  if (rc0 == Map::Destination::Reached) predMap.reach(alloc, BBsrc, predicate);
  return rc0;
}
[[nodiscard]] inline auto Map::descend(Arena<> *alloc, IR::Cache &cache,
                                       llvm::BasicBlock *BBsrc,
                                       llvm::BasicBlock *BBdst, llvm::Loop *L,
                                       TreeResult &tr) -> std::optional<Map> {
  auto p = alloc->checkpoint();
  Map predMap{alloc};
  dict::aset<llvm::BasicBlock *> visited{alloc};
  if (descendBlock(alloc, cache, visited, predMap, BBsrc, BBdst, {}, BBsrc, L,
                   tr) == Destination::Reached)
    return predMap;
  alloc->rollback(p);
  return std::nullopt;
}

} // namespace Predicate
} // namespace poly::IR
