#pragma once
#include "Math.hpp"
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>
struct Predicate {
    [[no_unique_address]] llvm::Value *condition;
    [[no_unique_address]] bool flip{false};
    Predicate operator!() { return {condition, !flip}; }
    Predicate(llvm::Value *condition, bool flip = false)
        : condition(condition), flip(flip) {}
};
struct Predicates {
    [[no_unique_address]] llvm::SmallVector<Predicate, 3> pred;
    Predicates operator&(llvm::Value *cond) {
        Predicates newPreds;
        newPreds.pred.reserve(pred.size() + 1);
        for (auto p : pred)
            newPreds.pred.push_back(p);
        newPreds.pred.emplace_back(cond);
        return newPreds;
    }
    Predicates &operator&=(llvm::Value *cond) {
        pred.emplace_back(cond);
        return *this;
    }
    Predicates &dropLastCondition() {
        pred.pop_back();
        return *this;
    }
    Predicates &flipLastCondition() {
        pred.back() = !pred.back();
        return *this;
    }
};
struct PredicatedBasicBlock {
    [[no_unique_address]] Predicates predicates;
    [[no_unique_address]] llvm::BasicBlock *basicBlock;
    // PredicatedBasicBlock(const PredicatedBasicBlock &) = default;
    PredicatedBasicBlock() = default;
    PredicatedBasicBlock(llvm::BasicBlock *basicBlock)
        : predicates(Predicates{}), basicBlock(basicBlock) {}
    PredicatedBasicBlock(Predicates predicates, llvm::BasicBlock *basicBlock)
        : predicates(std::move(predicates)), basicBlock(basicBlock) {}
    PredicatedBasicBlock &dropLastCondition() {
        predicates.dropLastCondition();
        return *this;
    }
};

struct PredicatedChain {
    llvm::SmallVector<PredicatedBasicBlock> chain;
    PredicatedChain() = default;
    PredicatedChain(llvm::BasicBlock *basicBlock)
        : chain({PredicatedBasicBlock{basicBlock}}){};
    PredicatedChain &conditionOnLastPred() {
        for (auto &&c : chain)
            c.dropLastCondition();
        return *this;
    }
    void push_back(llvm::BasicBlock *BB) {
        chain.emplace_back(Predicates{}, BB);
    }
    void emplace_back(Predicates p, llvm::BasicBlock *BB) {
        chain.emplace_back(std::move(p), BB);
    }
    bool contains(llvm::BasicBlock *BB) {
        for (auto &&c : chain)
            if (c.basicBlock == BB)
                return true;
        return false;
    }
    void reverse() { std::ranges::reverse(chain); }
    void clear() { chain.clear(); }
    void truncate(size_t i) { chain.truncate(i); }
    auto begin() { return chain.begin(); }
    auto end() { return chain.end(); }
    auto rbegin() { return chain.rbegin(); }
    auto rend() { return chain.rend(); }
};
