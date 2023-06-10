#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/Instruction.hpp"
#include "IR/Node.hpp"

namespace poly::IR {
using dict::map;
class Cache {
  map<llvm::Value *, Node *> llvmToInternalMap;
  map<InstByValue, Inst *> instCSEMap;
  BumpAlloc<> alloc;
  Inst *loopInvariants{nullptr}; // negative numOps/incomplete
  Inst *freeList{nullptr};       // positive numOps/complete, but empty
  auto allocateInst(unsigned numOps) -> Inst * {
    // because we allocate children before parents
    for (Inst *I = freeList; I; I = I->getNext()) {
      if (I->getNumOperands() != numOps) continue;
      I->removeFromList();
      return I;
    }
    // not found, allocate
    return static_cast<Inst *>(
      alloc.allocate(sizeof(Inst) + sizeof(Node *) * numOps, alignof(Inst)));
  }
  void addLoopInstr(llvm::Loop *L) {
    for (Inst *I = loopInvariants; I; I = I->getNext()) {
      if (!L->isLoopInvariant(I->getLLVMInstruction())) continue;
      I->removeFromList();
      complete(I);
    }
  }
  /// complete the operands
  void complete(Inst *I, llvm::Loop *L) {
    auto *i = I->getLLVMInstruction();
    unsigned nOps = I->numCompleteOps();
    auto ops = I->getOperands();
    for (unsigned j = 0; j < nOps; ++j) {
      auto *op = i->getOperand(j);
      auto *v = getValue(op, L);
      ops[j] = v;
      v->addUser(alloc, I);
    }
    Inst *&cse = getCSE(i);
    if (cse && cse != I) {
      I->replaceAllUsesWith(cse);
      I->removeFromList();
      I->setNext(freeList);
      freeList = I;
    } else cse = I;
  }
  auto getCSE(Inst *I) -> Inst *& { return instCSEMap[InstByValue{I}]; }
  auto getValue(llvm::Value *v, llvm::Loop *L) -> Node * {
    auto &n = llvmToInternalMap[v];
    if (n) return n;
    return createValue(v, L);
  }
  auto createValue(llvm::Value *v, llvm::Loop *L) -> Node * {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v))
      return createInstruction(i, L);
    else if (auto *c = llvm::dyn_cast<llvm::ConstantInt>(v))
      return createConstantInt(c);
    else if (auto *c = llvm::dyn_cast<llvm::ConstantFP>(v))
      return createConstantFP(c);
    else return createConstantVal(v);
  }
  auto createInstruction(llvm::Instruction *i, llvm::Loop *L) -> Node * {
    auto [id, kind] = Inst::getIDKind(i);
    int numOps = i->getNumOperands();
    Inst *n = std::construct_at(allocInst(numOps), kind, i, id, -numOps);
    llvmToInternalMap[i] = n;
    if (L->isLoopInvariant(i)) {
      n->setNext(loopInvariants);
      loopInvariants = n;
    } else complete(n, L);
    return n;
  }
  auto operator[](llvm::Value *v) -> Intr * {
    auto f = llvmToInternalMap.find(v);
    if (f != llvmToInternalMap.end()) return f->second;
    return nullptr;
  }
  auto operator[](UniqueIdentifier uid) -> Intr * {
    if (auto f = argMap.find(uid); f != argMap.end()) return f->second;
    return nullptr;
  }
  auto argMapLoopup(Intrinsic idt) -> Intr * {
    UniqueIdentifier uid{idt, {nullptr, unsigned(0)}};
    return (*this)[uid];
  }
  auto argMapLoopup(Intrinsic idt, Intr *op) -> Intr * {
    std::array<Intr *, 1> ops;
    ops[0] = op;
    MutPtrVector<Intr *> opsRef(ops);
    UniqueIdentifier uid{idt, opsRef};
    return (*this)[uid];
  }
  template <size_t N>
  auto argMapLoopup(Intrinsic idt, std::array<Intr *, N> ops) -> Intr * {
    MutPtrVector<Intr *> opsRef(ops);
    UniqueIdentifier uid{idt, opsRef};
    return (*this)[uid];
  }
  auto argMapLoopup(Intrinsic idt, Intr *op0, Intr *op1) -> Intr * {
    return argMapLoopup<2>(idt, {op0, op1});
  }
  auto argMapLoopup(Intrinsic idt, Intr *op0, Intr *op1, Intr *op2) -> Intr * {
    return argMapLoopup<3>(idt, {op0, op1, op2});
  }
  auto createInstruction(BumpAlloc<> &alloc, UniqueIdentifier uid,
                         llvm::Type *typ) -> Intr * {
    auto *i = new (alloc) Intr(alloc, uid, typ);
    if (!i->operands.empty())
      for (auto *op : i->operands) op->users.insert(i);
    argMap.insert({uid, i});
    return i;
  }
  auto getInstruction(BumpAlloc<> &alloc, UniqueIdentifier uid,
                      llvm::Type *typ) {
    if (auto *i = (*this)[uid]) return i;
    return createInstruction(alloc, uid, typ);
  }
  auto getInstruction(BumpAlloc<> &alloc, UniqueIdentifier uid, llvm::Type *typ,
                      Predicate::Set pred) {
    if (auto *i = (*this)[uid]) return i;
    auto *i = createInstruction(alloc, uid, typ);
    i->predicates = std::move(pred);
    return i;
  }
  auto getInstruction(BumpAlloc<> &alloc, Intrinsic idt, llvm::Type *typ) {
    UniqueIdentifier uid{idt, {nullptr, unsigned(0)}};
    return getInstruction(alloc, uid, typ);
  }
  auto getInstruction(BumpAlloc<> &alloc, Intrinsic idt, Intr *op0,
                      llvm::Type *typ) {
    // stack allocate for check
    if (auto *i = argMapLoopup(idt, op0)) return i;
    auto **opptr = alloc.allocate<Intr *>(1);
    opptr[0] = op0;
    MutPtrVector<Intr *> ops(opptr, 1);
    UniqueIdentifier uid{idt, ops};
    return createInstruction(alloc, uid, typ);
  }
  template <size_t N>
  auto getInstruction(BumpAlloc<> &alloc, Intrinsic idt,
                      std::array<Intr *, N> ops, llvm::Type *typ) {
    // stack allocate for check
    if (auto *i = argMapLoopup(idt, ops)) return i;
    auto **opptr = alloc.allocate<Intr *>(2);
    for (size_t n = 0; n < N; n++) opptr[n] = ops[n];
    MutPtrVector<Intr *> mops(opptr, N);
    UniqueIdentifier uid{idt, mops};
    return createInstruction(alloc, uid, typ);
  }
  auto getInstruction(BumpAlloc<> &alloc, Intrinsic idt, Intr *op0, Intr *op1,
                      llvm::Type *typ) {
    return getInstruction<2>(alloc, idt, {op0, op1}, typ);
  }
  auto getInstruction(BumpAlloc<> &alloc, Intrinsic idt, Intr *op0, Intr *op1,
                      Intr *op2, llvm::Type *typ) {
    return getInstruction<3>(alloc, idt, {op0, op1, op2}, typ);
  }

  /// This is the API for creating new instructions
  auto getInstruction(BumpAlloc<> &alloc, llvm::Instruction *instr) -> Intr * {
    if (Intr *i = (*this)[instr]) return i;
    UniqueIdentifier uid{getUniqueIdentifier(alloc, *this, instr)};
    auto *i = getInstruction(alloc, uid, instr->getType());
    llvmToInternalMap[instr] = i;
    return i;
  }
  auto getInstruction(BumpAlloc<> &alloc, Predicate::Map &predMap,
                      llvm::Instruction *instr) -> Intr *;
  // NOLINTNEXTLINE(misc-no-recursion)
  auto getInstruction(BumpAlloc<> &alloc, llvm::Value *v) -> Intr * {
    if (Intr *i = (*this)[v]) return i;
    UniqueIdentifier uid{getUniqueIdentifier(alloc, *this, v)};
    auto *i = getInstruction(alloc, uid, v->getType());
    llvmToInternalMap[v] = i;
    return i;
  }
  // if not in predMap, then operands don't get added, and
  // it won't be added to the argMap
  auto getInstruction(BumpAlloc<> &alloc, Predicate::Map &predMap,
                      llvm::Value *v) -> Intr *;
  [[nodiscard]] auto contains(llvm::Value *v) const -> bool {
    return llvmToInternalMap.count(v);
  }
  auto createConstant(BumpAlloc<> &alloc, llvm::Type *typ, int64_t c)
    -> Intr * {
    UniqueIdentifier uid{Identifier(c), {nullptr, unsigned(0)}};
    auto argMatch = argMap.find(uid);
    if (argMatch != argMap.end()) return argMatch->second;
    return new (alloc) Intr(alloc, uid, typ);
  }
  auto eslgetConstant(BumpAlloc<> &alloc, llvm::Type *typ, int64_t c)
    -> Intr * {
    UniqueIdentifier uid{Identifier(c), {nullptr, unsigned(0)}};
    if (auto *i = (*this)[uid]) return i;
    return createConstant(alloc, typ, c);
  }
  auto createCondition(BumpAlloc<> &alloc, Predicate::Relation rel, Intr *instr,
                       bool swap = false) -> Node * {
    switch (rel) {
    case Predicate::Relation::Any:
      return Cint::create(alloc, 1, instr->getType());
    case Predicate::Relation::Empty:
      return Cint::create(alloc, 0, instr->getType());
    case Predicate::Relation::False: swap = !swap; [[fallthrough]];
    case Predicate::Relation::True:
      return swap ? instr->negate(alloc, *this) : instr;
    }
  }
  auto createCondition(BumpAlloc<> &alloc, Predicate::Intersection pred,
                       bool swap) -> Intr * {
    size_t popCount = pred.popCount();

    if (popCount == 0) return getConstant(alloc, predicates[0]->getType(), 1);
    if (popCount == 1) {
      size_t ind = pred.getFirstIndex();
      Intr *J = predicates[ind];
      return swap ? J->negate(alloc, *this) : J;
    }
    // we have more than one instruction
    auto And = Intrinsic(Intrinsic::OpCode{llvm::Instruction::And});
    size_t ind = pred.getFirstIndex();
    Intr *J = predicates[ind];
    ind = pred.getNextIndex(ind);
    // we keep I &= predicates[ind] until ind is invalid
    // ind will be >= 32 when it is invalid
    // getNextIndex will return a valid answer at least once, because
    // popCount > 1
    // there may be a better order than folding from the left
    // e.g. a binary tree could allow for more out of order execution
    // but I think a later pass should handle that sort of associativity
    do {
      J = getInstruction(alloc, And, J, predicates[ind], J->getType());
      ind = pred.getNextIndex(ind);
    } while (ind < 32);
    return J;
  }
  auto createSelect(BumpAlloc<> &alloc, Intr *A, Intr *B) -> Intr * {
    auto idt = Intrinsic(Intrinsic::OpCode{llvm::Instruction::Select});
    // TODO: make predicate's instruction vector shared among all in
    // LoopTree?
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
    Predicate::Intersection P = A->predicates.getConflict(B->predicates);
    assert(!P.isEmpty() && "No conflict between predicates");
    bool swap = P.countFalse() <= P.countTrue();
    Intr *cond = createCondition(alloc, P, swap);
    Intr *op1 = swap ? B : A;
    Intr *op2 = swap ? A : B;
    Intr *S = getInstruction(alloc, idt, cond, op1, op2, A->getType());
    S->predicates |= A->predicates;
    S->predicates |= B->predicates;
    return S;
  }
  /// completeInstruction
  /// when filling a predMap, we may initially not complete an instruction
  /// if it didn't appear inside the predMap if it is added later, we then
  /// need to finish adding its operands.
  auto completeInstruction(BumpAlloc<> &, Predicate::Map &, llvm::Instruction *)
    -> Intr *;
  Node *addParents(BumpAlloc<> &alloc, Stow *a, llvm::Loop *L) {
    llvm::StoreInst *J = a->getInstruction();
    if (!L->contains(J->getParent())) return;
    auto ops{instr->operands()};
    auto *OI = *ops.begin();
    Instr *p = cache.getInstruction(alloc, *OI);
  }
  Node *addParents(BumpAlloc<> &alloc, Inst *a, llvm::Loop *L) {
    llvm::StoreInst *J = a->getInstruction();
    if (!L->contains(J->getParent())) return;
    llvm::Use *U = J->getOperandList();
    unsigned numOperands = J->getNumOperands();
    for (unsigned i = 0; i < numOperands; ++i) {
      llvm::Value *V = U[i].get();
      if (!L->contains(V->getParent())) continue;
      addValue(alloc, V, L);
    }
  }
  Node *addParents(BumpAlloc<> &alloc, Node *a, llvm::Loop *L,
                   Node *outOfLoop) {
    if (auto *S = llvm::dyn_cast<Stow>(a))
      return addParents(alloc, S, L, outOfLoop);
    if (auto *I = llvm::dyn_cast<Inst>(a))
      return addParents(alloc, I, L, outOfLoop);
    return outOfLoop;
  }
  void addValue(BumpAlloc<> &alloc, llvm::Value *V, llvm::Loop *L) {}
};

} // namespace poly::IR
