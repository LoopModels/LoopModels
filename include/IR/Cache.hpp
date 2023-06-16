#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/Instruction.hpp"
#include "IR/Node.hpp"
#include "IR/Predicate.hpp"
#include <cstddef>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/FMF.h>
#include <llvm/IR/Instructions.h>

namespace poly::IR {
using dict::map;
class Cache {
  map<llvm::Value *, Value *> llvmToInternalMap;
  map<InstByValue, Compute *> instCSEMap;
  map<Cnst::Identifier, Cnst *> constMap;
  BumpAlloc<> alloc;
  llvm::LoopInfo *LI;
  llvm::ScalarEvolution *SE;
  Compute *freeInstList{nullptr}; // positive numOps/complete, but empty
  UList<Instruction *> *freeListList{nullptr}; // TODO: make use of these
public:
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
  /// - `Addr* addr`: a linked list giving the addresses of the loop tree.
  /// These contain ordering information, which is enough for the linear program
  /// to deduce the orders of memory accesses, and perform an analysis.
  /// - 'Instruction* incomplete': a linked list giving the nodes that we
  /// stopped exploring due to not being inside the loop nest, and thus may need
  /// to have their parents filled out.
  /// - `size_t rejectDepth` is how many outer loops were rejected, due to to
  /// failure to produce an affine representation of the loop or memory
  /// accesses. Either because an affine representation is not possible, or
  /// because our analysis failed and needs improvement.
  ///
  struct TreeResult {
    Addr *addr{nullptr};
    Compute *incomplete{nullptr};
    size_t rejectDepth{0};
    [[nodiscard]] constexpr auto reject(size_t depth) const -> bool {
      return (depth < rejectDepth) || (addr == nullptr);
    }
    [[nodiscard]] constexpr auto accept(size_t depth) const -> bool {
      return !reject(depth);
    }
    // `addr` can be initialized by `nullptr`. If `other` returns `nullptr`, we
    // lose and leak the old pointer (leaking is okay so long as it was bump
    // allocated -- the bump allocator will free it). because `rejectDepth`
    // accumulates, we technically don't need to worry about checking for
    // `nullptr` every time, but may make sense to do so anyway.
    constexpr auto operator*=(TreeResult other) -> TreeResult & {
      rejectDepth = std::max(rejectDepth, other.rejectDepth);
      return *this *= other.addr;
    }
    constexpr auto operator*(TreeResult other) -> TreeResult {
      TreeResult result = *this;
      return result *= other;
    }
    constexpr auto operator*=(IR::Addr *other) -> TreeResult & {
      if (addr && other) addr->setPrev(other);
      addr = other;
      return *this;
    }
    constexpr void addIncomplete(Compute *I) {
      I->setNext(incomplete);
      incomplete = I;
    }
    constexpr void addAddr(Addr *A) {
      A->setNext(addr);
      addr = A;
    }
  };

private:
  auto allocateInst(unsigned numOps) -> Compute * {
    // because we allocate children before parents
    for (Compute *I = freeInstList; I;
         I = static_cast<Compute *>(I->getNext())) {
      if (I->getNumOperands() != numOps) continue;
      I->removeFromList();
      return I;
    }
    // not found, allocate
    return static_cast<Compute *>(alloc.allocate(
      sizeof(Compute) + sizeof(Value *) * numOps, alignof(Compute)));
  }
  // update list of loop invariants `I`.
  void addLoopInstr(Compute *I, llvm::Loop *L, TreeResult tr) {
    for (; I; I = static_cast<Compute *>(I->getNext())) {
      if (!L->isLoopInvariant(I->getInstruction())) continue;
      I->removeFromList();
      tr = complete(I, L, tr).second;
    }
  }
  // auto complete(Instruction *I, llvm::Loop *L) -> Instruction * {
  //   if (auto *C = llvm::dyn_cast<Compute>(I)) return complete(C, L);
  //   invariant(I->getKind() == Node::VK_Load);
  //   return I;
  // }

  /// complete the operands
  // NOLINTNEXTLINE(misc-no-recursion)
  auto complete(Compute *I, llvm::Loop *L, TreeResult tr)
    -> std::pair<Compute *, TreeResult> {
    auto *i = I->getLLVMInstruction();
    unsigned nOps = I->numCompleteOps();
    auto ops = I->getOperands();
    for (unsigned j = 0; j < nOps; ++j) {
      auto *op = i->getOperand(j);
      auto [v, tret] = getValue(op, L, tr);
      tr = tret;
      ops[j] = v;
      v->addUser(alloc, I);
    }
    return {cse(I), tr};
  }
  auto getCSE(Compute *I) -> Compute *& { return instCSEMap[InstByValue{I}]; }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto createValue(llvm::Value *v, llvm::Loop *L, TreeResult tr, Value *&n)
    -> std::pair<Value *, TreeResult> {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v))
      return createInstruction(i, L, tr, n);
    if (auto *c = llvm::dyn_cast<llvm::ConstantInt>(v))
      return {createConstant(c, n), tr};
    if (auto *c = llvm::dyn_cast<llvm::ConstantFP>(v))
      return {createConstant(c, n), tr};
    return {createConstantVal(v, n), tr};
  }
  constexpr void replaceUsesByUsers(Value *oldNode, Value *newNode) {
    invariant(oldNode->getKind() == Node::VK_Load ||
              oldNode->getKind() >= Node::VK_Func);
    oldNode->getUsers()->forEach([=, this](Instruction *user) {
      // newNode may depend on old (e.g. when merging)
      if (user == newNode) return;
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
      if (newNode->getKind() != Node::VK_Stow) newNode->addUser(alloc, user);
    });
    if (freeListList) freeListList->append(oldNode->getUsers());
    else freeListList = oldNode->getUsers();
    oldNode->setUsers(nullptr);
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
  static void extendDensePtrMatCols(BumpAlloc<> &alloc,
                                    MutDensePtrMatrix<int64_t> &A, math::Row R,
                                    math::Col C) {
    MutDensePtrMatrix<int64_t> B{matrix<int64_t>(alloc, A.numRow(), C)};
    for (ptrdiff_t j = 0; j < R; ++j) {
      B(j, _(0, A.numCol())) << A(j, _);
      B(j, _(A.numCol(), end)) << 0;
    }
    std::swap(A, B);
  }

public:
  /// Get the cache's allocator.
  /// This is a long-lived bump allocator, mass-freeing after each
  /// sub-tree optimization.
  constexpr auto getAllocator() -> BumpAlloc<> & { return alloc; }
  /// try to remove `I` as a duplicate
  /// this travels downstream;
  /// if `I` is eliminated, all users of `I`
  /// get updated, making them CSE-candiates.
  /// In this manner, we travel downstream through users.
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

  // Here, we have a set of methods that take a `llvm::Loop*` and a
  // `TreeResult` argument, returning a `Value*` of some kind and a `TreeResult`
  // NOLINTNEXTLINE(misc-no-recursion)
  auto getValue(llvm::Value *v, llvm::Loop *L, TreeResult tr)
    -> std::pair<Value *, TreeResult> {
    Value *&n = llvmToInternalMap[v];
    if (n) return {n, tr};
    // by reference, so we can update in creation
    return createValue(v, L, tr, n);
  }
  auto getValue(llvm::Instruction *I, llvm::Loop *L, TreeResult tr)
    -> std::pair<Instruction *, TreeResult> {
    auto [v, tret] = getValue(static_cast<llvm::Value *>(I), L, tr);
    return {llvm::cast<Instruction>(v), tret};
  }

  // NOLINTNEXTLINE(misc-no-recursion)
  auto createInstruction(llvm::Instruction *I, llvm::Loop *L, TreeResult tr,
                         Value *&t) -> std::pair<Value *, TreeResult> {
    auto *load = llvm::dyn_cast<llvm::LoadInst>(I);
    auto *store = llvm::dyn_cast<llvm::StoreInst>(I);
    if (!load && !store) return createCompute(I, L, tr, t);
    auto *ptr = load ? load->getPointerOperand() : store->getPointerOperand();
    // The loop `L` passed in is for stopping outward evaluation of deps
    // but createArrayRef wants the loop to which the addr belongs
    // we evaluate here, because we want to evaluate loop depth in
    // case of failure
    L = LI->getLoopFor(I->getParent());
    auto ret = createArrayRef(I, L, ptr, tr);
    t = ret.first;
    return ret;
  }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto createCompute(llvm::Instruction *I, llvm::Loop *L, TreeResult tr,
                     Value *&t) -> std::pair<Compute *, TreeResult> {
    auto [id, kind] = Compute::getIDKind(I);
    int numOps = int(I->getNumOperands());
    Compute *n = std::construct_at(allocateInst(numOps), kind, I, id, -numOps);
    t = n;
    if (L->isLoopInvariant(I)) tr.addIncomplete(n);
    else {
      auto [v, tret] = complete(n, L, tr);
      n = v;
      tr = tret;
    }
    return {n, tr};
  }

  auto zeroDimRef(llvm::Instruction *loadOrStore,
                  llvm::SCEVUnknown const *arrayPtr, unsigned numLoops)
    -> Addr * {
    return Addr::construct(alloc, arrayPtr, loadOrStore, numLoops);
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
      tr.rejectDepth = std::max(tr.rejectDepth, size_t(numLoops));
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
          extendDensePtrMatCols(alloc, offsMat, math::Row{i},
                                math::Col{offsets.size()});
        offsMat(i, _) << offsets;
      }
    }
    size_t numExtraLoopsToPeel = 64 - std::countl_zero(blackList);
    Addr *op = Addr::construct(alloc, arrayPtr, loadOrStore,
                               Rt(_, _(numExtraLoopsToPeel, end)),
                               {std::move(sizes), std::move(symbolicOffsets)},
                               coffsets, offsMat.data(), numLoops);
    tr.addAddr(op);
    tr.rejectDepth += numExtraLoopsToPeel;
    return {op, tr};
  }

  template <size_t N>
  auto createOperation(llvm::Intrinsic::ID opId, std::array<Value *, N> ops,
                       llvm::Type *typ, llvm::FastMathFlags fmf) -> Compute * {
    Compute *op =
      std::construct_at(allocateInst(N), Node::VK_Oprn, opId, N, typ, fmf);
    setOperands(op, ops);
    return op;
  }
  auto createOperation(llvm::Intrinsic::ID opId, PtrVector<Value *> ops,
                       llvm::Type *typ, llvm::FastMathFlags fmf) -> Compute * {
    size_t N = ops.size();
    Compute *op =
      std::construct_at(allocateInst(N), Node::VK_Oprn, opId, N, typ, fmf);
    setOperands(op, ops);
    return op;
  }
  void setOperands(Compute *op, PtrVector<Value *> ops) {
    size_t N = ops.size();
    MutPtrVector<Value *> operands{op->getOperands()};
    for (size_t n = 0; n < N; ++n) {
      Value *operand = operands[n] = ops[n];
      operand->addUser(alloc, op);
    }
  }
  // this should be modified, and then `cse`-called on int
  auto copyOperation(Compute *A) {
    Compute *B = createOperation(A->getOpId(), A->getOperands(), A->getType(),
                                 A->getFastMathFlags());
    setOperands(B, A->getOperands());
    return B;
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
      return Cint::create(alloc, 1, instr->getType());
    case Predicate::Relation::Empty:
      return Cint::create(alloc, 0, instr->getType());
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
      Value *J = (*predicates)[ind];
      return swap ? negate(J) : J;
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
};

} // namespace poly::IR
