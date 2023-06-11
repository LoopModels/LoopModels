#pragma once

#include "Dicts/BumpMapSet.hpp"
#include "IR/Address.hpp"
#include "IR/Instruction.hpp"
#include "IR/Node.hpp"
#include <cstddef>
#include <llvm/IR/FMF.h>

namespace poly::IR {
using dict::map;
class Cache {
  map<llvm::Value *, Node *> llvmToInternalMap;
  map<InstByValue, Inst *> instCSEMap;
  map<std::pair<llvm::Type *, int64_t>, Cint *> cintMap;
  BumpAlloc<> alloc;
  Inst *loopInvariants{nullptr};        // negative numOps/incomplete
  Inst *freeInstList{nullptr};          // positive numOps/complete, but empty
  UList<Node *> *freeListList{nullptr}; // TODO: make use of these
  auto allocateInst(unsigned numOps) -> Inst * {
    // because we allocate children before parents
    for (Inst *I = freeInstList; I; I = static_cast<Inst *>(I->getNext())) {
      if (I->getNumOperands() != numOps) continue;
      I->removeFromList();
      return I;
    }
    // not found, allocate
    return static_cast<Inst *>(
      alloc.allocate(sizeof(Inst) + sizeof(Node *) * numOps, alignof(Inst)));
  }
  void addLoopInstr(llvm::Loop *L) {
    for (Inst *I = loopInvariants; I; I = static_cast<Inst *>(I->getNext())) {
      if (!L->isLoopInvariant(I->getLLVMInstruction())) continue;
      I->removeFromList();
      complete(I, L);
    }
  }
  /// complete the operands
  auto complete(Inst *I, llvm::Loop *L) -> Inst * {
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
  auto getCSE(Inst *I) -> Inst *& { return instCSEMap[InstByValue{I}]; }
  // try to remove `I` as a duplicate
  // this travels downstream;
  // if `I` is eliminated, all users of `I`
  // get updated, making them CSE-candiates.
  // In this manner, we travel downstream through users.
  auto cse(Inst *I) -> Inst * {
    Inst *&cse = getCSE(I);
    if (cse == nullptr || (cse == I)) return cse = I; // update ref
    replaceAllUsesWith(I, cse);
    I->removeFromList();
    I->setNext(freeInstList);
    freeInstList = I;
    return cse;
  }
  auto getValue(llvm::Value *v, llvm::Loop *L) -> Node * {
    Node *&n = llvmToInternalMap[v];
    if (n) return n;
    return createValue(v, L, n);
  }
  auto createValue(llvm::Value *v, llvm::Loop *L, Node *&n) -> Node * {
    if (auto *i = llvm::dyn_cast<llvm::Instruction>(v))
      return createInstruction(i, L, n);
    if (auto *c = llvm::dyn_cast<llvm::ConstantInt>(v))
      return createConstant(c, n);
    if (auto *c = llvm::dyn_cast<llvm::ConstantFP>(v)) return createConstant(c);
    return createConstantVal(v, n);
  }
  constexpr void replaceAllUsesWith(Inst *oldNode, Node *newNode) {
    oldNode->getUsers()->forEach([=, this](Node *user) {
      if (Inst *I = llvm::dyn_cast<Inst>(user)) {
        for (Node *&o : I->getOperands())
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
      UList<Node *> *tnode = newNode->getUsers();
      newNode->setUsers(tnode->pushUnique(alloc, user));
    });
    // every operand of oldNode needs their users updated
    for (Node *n : oldNode->getOperands()) n->removeFromUsers(oldNode);
    if (freeListList) freeListList->append(oldNode->getUsers());
    else freeListList = oldNode->getUsers();
    oldNode->setUsers(nullptr);
    oldNode->setNext(freeInstList);
    freeInstList = oldNode;
  }
  auto createInstruction(llvm::Instruction *i, llvm::Loop *L, Node *&t)
    -> Node * {
    auto [id, kind] = Inst::getIDKind(i);
    int numOps = i->getNumOperands();
    Inst *n = std::construct_at(allocateInst(numOps), kind, i, id, -numOps);
    t = n;
    if (L->isLoopInvariant(i)) {
      n->setNext(loopInvariants);
      loopInvariants = n;
    } else n = complete(n, L);
    return n;
  }
  template <size_t N>
  auto createOperation(llvm::Intrinsic::ID opId, std::array<Node *, N> ops,
                       llvm::Type *typ, llvm::FastMathFlags fmf) -> Node * {
    Inst *op =
      std::construct_at(allocateInst(ops), Node::VK_Oprn, opId, ops, typ, fmf);
    std::copy_n(ops.begin(), N, op->getOperands().begin());
    return op;
  }
  auto operator[](llvm::Value *v) -> Node * {
    auto f = llvmToInternalMap.find(v);
    if (f != llvmToInternalMap.end()) return f->second;
    return nullptr;
  }
  auto createConstant(llvm::ConstantInt *c, Node *&n) -> Cnst * {
    Cnst *cnst = (c->getBitWidth() <= 64)
                   ? (Cnst *)createConstant(c->getType(), c->getSExtValue())
                   : (Cnst *)alloc.create<Bint>(c, c->getType());
    n = cnst;
    return cnst;
  }
  auto createConstant(llvm::ConstantFP *f) -> Bflt * {
    Node *&n = llvmToInternalMap[f];
    if (n) return static_cast<Bflt *>(n);
    Bflt *cnst = alloc.create<Bflt>(f, f->getType());
    n = cnst;
    return cnst;
  }
  auto createConstant(llvm::Type *typ, int64_t v) -> Cint * {
    Cint *c = alloc.create<Cint>(v, typ);
    cintMap[{typ, v}] = c;
    return c;
  }
  auto createConstant(llvm::Type *typ, double v) -> Cflt * {
    return alloc.create<Cflt>(v, typ);
  }
  auto createConstantVal(llvm::Value *val, Node *&n) -> CVal * {
    CVal *v = alloc.create<CVal>(val);
    n = v;
    return v;
  }
  auto getConstant(llvm::Type *typ, int64_t v) -> Cint * {
    Cint *&c = cintMap[{typ, v}];
    if (!c) c = createConstant(typ, v);
    return c;
  }
  auto createCondition(Predicate::Relation rel, Inst *instr, bool swap = false)
    -> Node * {
    switch (rel) {
    case Predicate::Relation::Any:
      return Cint::create(alloc, 1, instr->getType());
    case Predicate::Relation::Empty:
      return Cint::create(alloc, 0, instr->getType());
    case Predicate::Relation::False: swap = !swap; [[fallthrough]];
    case Predicate::Relation::True: return swap ? negate(instr) : instr;
    }
  }
  auto negate(Inst *I) -> Node * {
    // first, check if its parent is a negation
    if (Operation op = Operation(I); op &&
                                     op.isInstruction(llvm::Instruction::Xor) &&
                                     (op.getNumOperands() == 2)) {
      // !x where `x isa bool` is represented as `x ^ true`
      auto *op0 = I->getOperand(0);
      auto *op1 = I->getOperand(1);
      if (isConstantOneInt(op1)) return op0;
      if (isConstantOneInt(op0)) return op1;
    }
    Cint *one = getConstant(I->getType(), 1);
    return createOperation(llvm::Instruction::Xor,
                           std::array<Node *, 2>{I, one}, I->getType(),
                           I->getFastMathFlags());
  }
  auto createCondition(Predicate::Intersection pred, bool swap) -> Intr * {
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
