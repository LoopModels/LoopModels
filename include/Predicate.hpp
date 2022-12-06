#pragma once
#include "./BitSets.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <llvm/ADT/SmallPtrSet.h>
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
    enum MatchResult { NoMatch, Match, MatchAndFlip };
    MatchResult match(const Predicate &p) const {
        if (condition == p.condition)
            return flip == p.flip ? Match : MatchAndFlip;
        return NoMatch;
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
    void clear() { pred.clear(); }
    bool empty() const { return pred.empty(); }
    bool emptyIntersection(const Predicates &p) const {
        for (auto a : *this)
            for (auto b : p)
                if (a.match(b) == Predicate::MatchAndFlip)
                    return true;
        return false;
    }
    /// Returns a new predicate if the intersection is non-empty
    std::optional<Predicates> operator&(const Predicates &p) const {
        // auto x = llvm::Intrinsic::sqrt;
        Predicates ret;
        BitSet pmatch;
        for (auto a : *this) {
            for (size_t i = 0; i < p.pred.size(); ++i) {
                auto b = p.pred[i];
                if (a.condition == b.condition) {
                    if (a.flip != b.flip)
                        return {};
                    pmatch.insert(i);
                }
            }
            ret.pred.push_back(a);
        }
        // Add the remaining predicates from p
        for (size_t i = 0; i < p.pred.size(); ++i)
            if (!pmatch[i])
                ret.pred.push_back(p.pred[i]);
        return ret;
    }
    /// Returns a new predicate if the union is can be expressed as
    /// an intersection of predicates from `a` or `b` in `a | b` expression
    std::optional<Predicates> operator|(Predicates p) const {
        switch (size()) {
        case 0:
            return *this;
        case 1:
            switch (p.size()) {
            case 0:
                return p;
            case 1:
                switch (pred[0].match(p.pred[0])) {
                case Predicate::Match:
                    return p;
                case Predicate::MatchAndFlip:
                    return Predicates{};
                case Predicate::NoMatch:
                    break;
                }
            default:
                break;
            }
        default:
            break;
        }
        return {};
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
    auto begin() { return basicBlock->begin(); }
    auto end() { return basicBlock->end(); }
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
        assert(!contains(BB));
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
        for (auto it = chain.begin(); it != chain.end(); ++it) {
            if (it->basicBlock == BB) {
                if (it->predicates == p) {
                    chain.erase(it);
                } else if (auto np = it->predicates | p) {
                    chain.erase(it);
                    p = std::move(*np);
                }
            }
        }
        chain.emplace_back(std::move(p), BB);
    }
    static bool contains(llvm::ArrayRef<PredicatedBasicBlock> chain,
                         llvm::BasicBlock *BB) {
        for (auto &&pbb : chain)
            if (pbb.basicBlock == BB)
                return true;
        return false;
    }
    bool contains(llvm::BasicBlock *BB) { return contains(chain, BB); }
    void reverse() {
        for (size_t i = 0; i < (chain.size() >> 1); ++i)
            std::swap(chain[i], chain[chain.size() - 1 - i]);
        // std::ranges::reverse not support by libc++ yet.
        // std::ranges::reverse(chain);
    }
    void clear() { chain.clear(); }
    void truncate(size_t i) { chain.truncate(i); }
    auto begin() { return chain.begin(); }
    auto end() { return chain.end(); }
    auto rbegin() { return chain.rbegin(); }
    auto rend() { return chain.rend(); }
    auto size() const { return chain.size(); }
    auto &operator[](size_t i) { return chain[i]; }
    auto &operator[](size_t i) const { return chain[i]; }
    auto &back() { return chain.back(); }
    auto &back() const { return chain.back(); }
    auto &front() { return chain.front(); }
    auto &front() const { return chain.front(); }

    bool containsPredicates() const {
        for (auto &&pbb : chain)
            if (!pbb.predicates.empty())
                return true;
        return false;
    }
};
