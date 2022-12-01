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

enum struct PredicateRelation : uint8_t {
    Any = 0,
    True = 1,
    False = 2,
    Empty = 3,
};

[[maybe_unused]] static constexpr PredicateRelation
operator&(PredicateRelation a, PredicateRelation b) {
    return static_cast<PredicateRelation>(static_cast<uint8_t>(a) |
                                          static_cast<uint8_t>(b));
}
[[maybe_unused]] static constexpr PredicateRelation
operator|(PredicateRelation a, PredicateRelation b) {
    return static_cast<PredicateRelation>(static_cast<uint8_t>(a) &
                                          static_cast<uint8_t>(b));
}


/// PredicateRelations
/// A type for performing set algebra on predicates, representing sets
/// Note:
/// Commutative:
///     a | b == b | a
///     a & b == b & a
/// Distributive:
///     a | (b & c) == (a | b) & (a | c)
///     a & (b | c) == (a & b) | (a & c)
/// Associative:
///    a | (b | c) == (a | b) | c
///    a & (b & c) == (a & b) & c
/// Idempotent:
///    a | a == a
///    a & a == a
/// The internal representation can be interpreted as the intersection
/// of a vector of predicates.
/// This makes intersection operations efficient, but means we
/// may need to allocate new instructions to represent unions.
/// Unions are needed for merging divergent control flow branches.
/// For union calculation, we'd simplify:
/// (a & b) | (a & c) == a & (b | c)
/// If c == !b, then
/// (a & b) | (a & !b) == a & (b | !b) == a & True == a
/// Generically:
/// (a & b) | (c & d) == ((a & b) | c) & ((a & b) | d)
/// == (a | c) & (b | c) & (a | d) & (b | d)
struct PredicateRelations {
    [[no_unique_address]] llvm::SmallVector<uint64_t, 1> relations;
    PredicateRelation operator[](size_t index) const {
        return static_cast<PredicateRelation>(
            (relations[index / 32] >> (2 * (index % 32))) & 3);
    }
    struct Reference {
        [[no_unique_address]] uint64_t *rp;
        [[no_unique_address]] size_t index;
        operator PredicateRelation() const {
            return static_cast<PredicateRelation>((*rp) >> index);
        }
        Reference &operator=(PredicateRelation relation) {
            *this->rp = (*this->rp & ~(3 << index)) |
                        (static_cast<uint64_t>(relation) << index);
            return *this;
        }
    };

    Reference operator[](size_t index) {
        return {&relations[index / 32], 2 * (index % 32)};
    }
    size_t size() const { return relations.size() * 32; }
    size_t relationSize() const { return relations.size(); }
    // FIXME: over-optimistic
    // (!a & !b) U (a & b) = a == b
    // (!a & b) U a = b
    PredicateRelations predUnion(const PredicateRelations &other) const {
        if (relationSize() < other.relationSize())
            return other.predUnion(*this);
        // other.relationSize() <= relationSize()
        PredicateRelations result;
        result.relations.resize(other.relationSize());
        // `&` because `0` is `Any`
        // and `Any` is the preferred default initialization
        for (size_t i = 0; i < other.relationSize(); i++)
            result.relations[i] = relations[i] & other.relations[i];
        return result;
    }
    static void intersectImpl(PredicateRelations &c,
                              const PredicateRelations &a,
                              const PredicateRelations &b) {
        assert(a.relationSize() >= b.relationSize());
        c.relations.resize(a.relationSize());
        // `&` because `0` is `Any`
        // and `Any` is the preferred default initialization
        for (size_t i = 0; i < b.relationSize(); i++)
            c.relations[i] = a.relations[i] | b.relations[i];
        for (size_t i = b.relationSize(); i < a.relationSize(); i++)
            c.relations[i] = a.relations[i];
    }
    PredicateRelations predIntersect(const PredicateRelations &other) const {
        PredicateRelations result;
        if (relationSize() < other.relationSize()) {
            intersectImpl(result, other, *this);
        } else {
            intersectImpl(result, *this, other);
        }
        return result;
    }

    static constexpr bool isEmpty(uint64_t x) {
        return ((x & (x >> 1)) & 0x5555555555555555) != 0;
    }
    bool isEmpty() const {
        for (uint64_t x : relations)
            if (isEmpty(x))
                return true;
        return false;
    }
    bool emptyIntersection(const PredicateRelations &other) const {
        if (relationSize() < other.relationSize())
            return other.emptyIntersection(*this);
        // other.relationSize() <= relationSize()
        for (size_t i = 0; i < other.relationSize(); i++)
            if (isEmpty(relations[i] | other.relations[i]))
                return true;
        for (size_t i = other.relationSize(); i < relations.size(); i++)
            if (isEmpty(relations[i]))
                return true;
        return false;
    }
};

struct BlockPredicates {
    // TODO: use internal IR for predicates
    // the purpose of this would be to allow for union calculations.
    llvm::SmallVector<llvm::Value *> predicates;
    llvm::DenseMap<llvm::BasicBlock *, PredicateRelations> blockPredicates;
};

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
};
