#pragma once
#include "BitSets.hpp"
#include "Macro.hpp"
#include "Math.hpp"
#include <cstddef>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>
struct Predicate {
    [[no_unique_address]] llvm::Value *condition;
    [[no_unique_address]] bool flip{false};
    Predicate operator!() { return {condition, !flip}; }
    Predicate(llvm::Value *condition, bool flip = false)
        : condition(condition), flip(flip) {}
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const Predicate &pred) {
        if (pred.flip)
            os << "!";
        return os << *pred.condition;
    }
    bool operator==(const Predicate &p) const {
        return (condition == p.condition) && (flip == p.flip);
    }
};
struct Predicates {
    [[no_unique_address]] llvm::SmallVector<Predicate, 3> pred;
    size_t size() const { return pred.size(); }
    Predicates operator&(llvm::Value *cond) {
        Predicates newPreds;
        newPreds.pred.reserve(pred.size() + 1);
        bool dontPushCond = false;
        for (auto p : pred)
            if (p.condition == cond)
                dontPushCond = p.flip;
            else
                newPreds.pred.push_back(p);
        if (!dontPushCond)
            newPreds.pred.emplace_back(cond);
        return newPreds;
    }
    Predicates &operator&=(llvm::Value *cond) {
        for (auto it = pred.begin(); it != pred.end(); ++it) {
            if (it->condition == cond) {
                if (it->flip)
                    pred.erase(it);
                return *this;
            }
        }
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
    auto begin() { return pred.begin(); }
    auto end() { return pred.end(); }
    auto begin() const { return pred.begin(); }
    auto end() const { return pred.end(); }
    llvm::Optional<Predicates> operator&(const Predicates p) const {
        Predicates ret;
        BitSet pmatch;
        for (auto a : *this) {
            for (size_t i = 0; i < p.pred.size(); ++i) {
                auto b = p.pred[i];
                if (a.condition == b.condition) {
                    if (a.flip != b.flip) {
                        return {};
                    } else {
                        pmatch.insert(i);
                    }
                }
            }
            ret.pred.push_back(a);
        }
        for (size_t i = 0; i < p.pred.size(); ++i)
            if (!pmatch[i])
                ret.pred.push_back(p.pred[i]);
        return ret;
    }
    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const Predicates &pred) {
        os << "[";
        for (size_t i = 0; i < pred.size(); ++i) {
            if (i)
                os << ", ";
            os << pred.pred[i];
        }
        os << "]";
        return os;
    }
    bool operator==(const Predicates &p) const {
        if (size() != p.size())
            return false;
        // TODO: sort to avoid O(N^2)?
        for (auto a : *this) {
            bool matched = false;
            for (auto b : p)
                if (a == b) {
                    matched = true;
                    break;
                }
            if (!matched)
                return false;
        }
        return true;
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
    bool operator==(const PredicatedBasicBlock &pbb) const {
        return (basicBlock == pbb.basicBlock) && (predicates == pbb.predicates);
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
#ifndef NDEBUG
        SHOWLN(BB);
        for (auto &&p : chain)
            assert(BB != p.basicBlock);
#endif
        chain.emplace_back(Predicates{}, BB);
    }
    void emplace_back(Predicates p, llvm::BasicBlock *BB) {
#ifndef NDEBUG
        SHOWLN(BB);
        for (auto &&pbb : chain)
            assert(!((BB == pbb.basicBlock) && (p == pbb.predicates)));
#endif
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
