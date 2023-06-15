#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/Instruction.hpp"
#include "IR/Node.hpp"
#include "IR/Predicate.hpp"
#include <cstddef>
#include <llvm/IR/FMF.h>

namespace poly::IR {
using dict::map;
class Cache {
  map<llvm::Value *, Value *> llvmToInternalMap;
  map<InstByValue, Compute *> instCSEMap;
  map<Cnst::Identifier, Cnst *> constMap;
  BumpAlloc<> alloc;
  Compute *loopInvariants{nullptr}; // negative numOps/incomplete
  Compute *freeInstList{nullptr};   // positive numOps/complete, but empty
  UList<Instruction *> *freeListList{nullptr}; // TODO: make use of these
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
  void addLoopInstr(llvm::Loop *L) {
    for (Compute *I = loopInvariants; I;
         I = static_cast<Compute *>(I->getNext())) {
      if (!L->isLoopInvariant(I->getLLVMInstruction())) continue;
      I->removeFromList();
      complete(I, L);
    }
  }
  /// complete the operands
  // NOLINTNEXTLINE(misc-no-recursion)
  auto complete(Compute *I, llvm::Loop *L) -> Compute * {
    auto *i = I->getLLVMInstruction();
    unsigned nOps = I->numCompleteOps();
    auto ops = I->getOperands();
    for (unsigned j = 0; j < nOps; ++j) {
      auto *op = i->getOperand(j);
      auto *v = getValue(op, L);
      ops[j] = v;
      v->addUser(alloc, I);
    }
    return cse(I);
  }
  auto getCSE(Compute *I) -> Compute *& { return instCSEMap[InstByValue{I}]; }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto createValue(llvm::Value *v, llvm::Loop *L, Value *&n) -> Value * {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v))
      return createInstruction(i, L, n);
    if (auto *c = llvm::dyn_cast<llvm::ConstantInt>(v))
      return createConstant(c, n);
    if (auto *c = llvm::dyn_cast<llvm::ConstantFP>(v)) return createConstant(c);
    return createConstantVal(v, n);
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

public:
  // try to remove `I` as a duplicate
  // this travels downstream;
  // if `I` is eliminated, all users of `I`
  // get updated, making them CSE-candiates.
  // In this manner, we travel downstream through users.
  auto cse(Compute *I) -> Compute * {
    Compute *&cse = getCSE(I);
    if (cse == nullptr || (cse == I)) return cse = I; // update ref
    replaceAllUsesWith(I, cse);
    I->removeFromList();
    I->setNext(freeInstList);
    freeInstList = I;
    return cse;
  }
  constexpr auto getAllocator() -> BumpAlloc<> & { return alloc; }
  // NOLINTNEXTLINE(misc-no-recursion)
  auto getValue(llvm::Value *v, llvm::Loop *L) -> Value * {
    Value *&n = llvmToInternalMap[v];
    if (n) return n;
    return createValue(v, L, n);
  }
  auto getValue(llvm::Instruction *I, llvm::Loop *L) -> Instruction * {
    return llvm::cast<Instruction>(getValue(static_cast<llvm::Value *>(I), L));
  }
  constexpr void replaceAllUsesWith(Addr *oldNode, Value *newNode) {
    invariant(oldNode->isLoad());
    replaceUsesByUsers(oldNode, newNode);
    // every operand of oldNode needs their users updated
    if (Value *p = oldNode->getPredicate()) p->removeFromUsers(oldNode);
  }
  constexpr void replaceAllUsesWith(Compute *oldNode, Value *newNode) {
    invariant(oldNode->getKind() >= Node::VK_Func);
    replaceUsesByUsers(oldNode, newNode);
    // every operand of oldNode needs their users updated
    for (Value *n : oldNode->getOperands()) n->removeFromUsers(oldNode);
    oldNode->setNext(freeInstList);
    freeInstList = oldNode;
  }
  constexpr void replaceAllUsesWith(Value *oldNode, Value *newNode) {
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

  // NOLINTNEXTLINE(misc-no-recursion)
  auto createInstruction(llvm::Instruction *i, llvm::Loop *L, Value *&t)
    -> Compute * {
    auto [id, kind] = Compute::getIDKind(i);
    int numOps = int(i->getNumOperands());
    Compute *n = std::construct_at(allocateInst(numOps), kind, i, id, -numOps);
    t = n;
    if (L->isLoopInvariant(i)) {
      n->setNext(loopInvariants);
      loopInvariants = n;
    } else n = complete(n, L);
    return n;
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
