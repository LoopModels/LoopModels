#pragma once
#include "./BitSets.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>
#include <llvm/Pass.h>
#include <llvm/Support/Allocator.h>
#include <optional>
#include <variant>
enum struct PredicateRelation : uint8_t {
    Any = 0,
    True = 1,
    False = 2,
    Empty = 3,
};

[[maybe_unused]] static constexpr auto operator&(PredicateRelation a,
                                                 PredicateRelation b)
    -> PredicateRelation {
    return static_cast<PredicateRelation>(static_cast<uint8_t>(a) |
                                          static_cast<uint8_t>(b));
}
[[maybe_unused]] static constexpr auto operator|(PredicateRelation a,
                                                 PredicateRelation b)
    -> PredicateRelation {
    return static_cast<PredicateRelation>(static_cast<uint8_t>(a) &
                                          static_cast<uint8_t>(b));
}

struct Instruction;

/// PredicateIntersection
struct Intersection {
    [[no_unique_address]] uint64_t predicates;
    constexpr auto operator[](size_t index) const -> PredicateRelation {
        assert(index < 32);
        return static_cast<PredicateRelation>((predicates >> (2 * (index))) &
                                              3);
    }
    void set(size_t index, PredicateRelation value) {
        assert(index < 32);
        index += index;
        uint64_t maskedOff = predicates & ~(3ULL << (index));
        predicates = maskedOff | static_cast<uint64_t>(value) << (index);
    }
    struct Reference {
        [[no_unique_address]] uint64_t &rp;
        [[no_unique_address]] size_t index;
        operator PredicateRelation() const {
            return static_cast<PredicateRelation>(rp >> index);
        }
        auto operator=(PredicateRelation relation) -> Reference & {
            this->rp = (this->rp & ~(3 << index)) |
                       (static_cast<uint64_t>(relation) << index);
            return *this;
        }
    };

    auto operator[](size_t index) -> Reference {
        return {predicates, 2 * index};
    }
    constexpr auto operator&(Intersection other) const -> Intersection {
        return {predicates | other.predicates};
    }
    auto operator&=(Intersection other) -> Intersection & {
        predicates |= other.predicates;
        return *this;
    }
    /// returns 00 if non-empty, 01 if empty
    static constexpr auto emptyMask(uint64_t x) -> uint64_t {
        return ((x & (x >> 1)) & 0x5555555555555555);
    }
    /// returns 11 if non-empty, 00 if empty
    static constexpr auto removeEmptyMask(uint64_t x) -> uint64_t {
        uint64_t y = emptyMask(x);
        return ~(y | (y << 1));
    }
    static constexpr auto isEmpty(uint64_t x) -> bool {
        return emptyMask(x) != 0;
    }
    /// returns `true` if the PredicateIntersection is empty, `false` otherwise
    [[nodiscard]] constexpr auto isEmpty() const -> bool {
        return isEmpty(predicates);
    }
    /// if the union between `this` and `other` can be expressed as an
    /// intersection of their constituents, return that intersection. Return an
    /// empty optional otherwise. The cases we handle are:
    /// (a & b) | a = a
    /// (a & b) | (a & !b) = a
    [[nodiscard]] constexpr auto compactUnion(Intersection other) const
        -> std::variant<std::monostate, Intersection,
                        std::pair<Intersection, Intersection>> {
        if (isEmpty())
            return other;
        else if (other.isEmpty())
            return *this;
        uint64_t x = predicates, y = other.predicates;
        // 010000 = 010100 & 010000
        uint64_t intersect = x & y;
        if (x == intersect || y == intersect) {
            return Intersection{intersect};
        }
        // 011100 = 010100 | 011000
        // 010000 = 010100 & 011000
        // we can't handle (a & b) | (a & !b & c) because
        // (a & b) | (a & !b & c) = a & (b | c)) = (a & b) | (a & c)
        // bit representation:
        // 010000 = 010100 & 011001
        // we thus check all bits equal after masking off `b`.
        // We could consider returning a pair of options, so we can return the
        // simplified expression.
        uint64_t bitUnion = x | y;
        uint64_t mask = emptyMask(bitUnion);
        if (std::popcount(mask) == 1) { // a single b & !b case
            uint64_t remUnionMask =
                ~(mask | (mask << 1)); // 0s `b`, meaning b can be either.
            uint64_t w = remUnionMask & x;
            uint64_t z = remUnionMask & y;
            if (w == z)
                return Intersection{w};
            // if we now have
            //  a     |  a & c
            // 010000 | 010001
            uint64_t wz = w & z;
            if (wz == w) {
                return std::make_pair(*this, Intersection{z});
            } else if (wz == z) {
                return std::make_pair(Intersection{w}, other);
            }
        }
        return {};
    }
};
/// PredicateSet
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
struct PredicateSet {
    [[no_unique_address]] llvm::SmallVector<Intersection, 2> intersectUnion;
    PredicateSet() = default;
    PredicateSet(Intersection pred) : intersectUnion({pred}) {}
    constexpr auto operator[](size_t index) -> Intersection {
        return intersectUnion[index];
    }
    constexpr auto operator[](size_t index) const -> Intersection {
        return intersectUnion[index];
    }
    constexpr auto operator()(size_t i, size_t j) const -> PredicateRelation {
        return intersectUnion[i][j];
    }
    [[nodiscard]] constexpr auto size() const -> size_t {
        return intersectUnion.size();
    }
    /// Cases we simplify:
    /// a | {} = a
    /// Impl: if either empty, set to other
    /// a | (a & b) == a & (a | b) == a
    /// Impl: if one is super set of other, set to subset
    /// (a & b) | (a & !b) == a
    /// Impl: if exactly one full intersection, zero that cond, check if
    /// remaining match, if so, set to remaining.
    /// (a & b) | !b == a | !b
    /// Impl: if one contains only one cond, drop that cond if it's reversed in
    /// other.
    /// TODO: handle more cases? Smarter algorithm that applies rewrite rules?
    /// Currently, it should be able to simplify:
    /// *this = (a & !b & c) | (a & !c)
    /// other = (a & b)
    /// to:
    /// (a & b) | (a & c) | (a & !c) = (a & b) | a = a
    void predUnion(Intersection other) {
        if (other.isEmpty())
            return;
        else if (intersectUnion.empty()) {
            intersectUnion.push_back(other);
            return;
        }
        // we first try to avoid pushing so that we don't have to realloc
        bool simplifyPreds = false;
        for (auto &&pred : intersectUnion) {
            auto u = pred.compactUnion(other);
            if (auto compact = std::get_if<Intersection>(&u)) {
                pred = *compact;
                return;
            } else if (auto simplify =
                           std::get_if<std::pair<Intersection, Intersection>>(
                               &u)) {
                pred = simplify->first;
                other = simplify->second;
                simplifyPreds = true;
            }
        }
        intersectUnion.push_back(other);
        while (simplifyPreds) {
            simplifyPreds = false;
            for (size_t i = 0; i < intersectUnion.size(); ++i) {
                for (size_t j = i + 1; j < intersectUnion.size();) {
                    auto u = intersectUnion[i].compactUnion(intersectUnion[j]);
                    if (auto compact = std::get_if<Intersection>(&u)) {
                        // delete `j`, update `i`
                        intersectUnion[i] = *compact;
                        intersectUnion.erase(intersectUnion.begin() + j);
                        simplifyPreds = true;
                    } else {
                        if (auto simplify = std::get_if<
                                std::pair<Intersection, Intersection>>(&u)) {
                            // assert forward progress
                            assert(
                                (std::popcount(simplify->first.predicates) +
                                 std::popcount(simplify->second.predicates)) <=
                                (std::popcount(intersectUnion[i].predicates) +
                                 std::popcount(intersectUnion[j].predicates)));
                            intersectUnion[i] = simplify->first;
                            intersectUnion[j] = simplify->second;
                            simplifyPreds = true;
                        }
                        ++j;
                    }
                }
            }
        }
    }
    auto operator&=(Intersection pred) -> PredicateSet & {
        for (size_t i = 0; i < intersectUnion.size();) {
            intersectUnion[i] &= pred;
            if (intersectUnion[i].isEmpty()) {
                intersectUnion.erase(intersectUnion.begin() + i);
            } else
                ++i;
        }
        return *this;
    }
    [[nodiscard]] constexpr auto begin() { return intersectUnion.begin(); }
    [[nodiscard]] constexpr auto end() { return intersectUnion.end(); }
    [[nodiscard]] constexpr auto begin() const {
        return intersectUnion.begin();
    }
    [[nodiscard]] constexpr auto end() const { return intersectUnion.end(); }
    [[nodiscard]] auto operator&=(PredicateSet &pred) -> PredicateSet & {
        for (auto p : pred)
            (*this) &= p;
        return *this;
    }
    [[nodiscard]] constexpr auto isEmpty() const -> bool {
        return intersectUnion.empty();
    }
    [[nodiscard]] auto emptyIntersection(const PredicateSet &other) const
        -> bool {
        for (auto pred : intersectUnion)
            for (auto otherPred : other)
                if ((pred & otherPred).isEmpty())
                    return true;
        return false;
    }

    // static auto getIndex(llvm::SmallVectorImpl<Instruction *> &instructions,
    //                      Instruction *instruction) -> size_t {
    //     size_t I = instructions.size();
    //     for (size_t i = 0; i < I; i++)
    //         if (instructions[i] == instruction)
    //             return i;
    //     instructions.push_back(instruction);
    //     return I;
    // }
    // PredicateSet() = default;
    // PredicateSet(llvm::BumpPtrAllocator &alloc, Instruction::Cache
    // &ic,
    //                    llvm::SmallVector<Instruction *> &predicates,
    //                    Predicates &pred) {
    //     for (Predicate &p : pred) {
    //         Instruction *i = ic.get(alloc, p.condition);
    //         size_t index = getIndex(predicates, i);
    //         PredicateRelation val =
    //             p.flip ? PredicateRelation::False :
    //             PredicateRelation::True;
    //         set(index, val);
    //     }
    // }
};
struct Predicates {
    [[no_unique_address]] PredicateSet predicates;
    // `SmallVector` to copy, as `llvm::ArrayRef` wouldn't be safe in
    // case of realloc
    [[no_unique_address]] llvm::SmallVector<Instruction *> instr;
};

struct PredicateMap {
    llvm::DenseMap<llvm::BasicBlock *, PredicateSet> map;
    llvm::SmallVector<Instruction *> predicates;
    auto get(llvm::BasicBlock *bb) -> PredicateSet & { return map[bb]; }
    auto find(llvm::BasicBlock *bb)
        -> llvm::DenseMap<llvm::BasicBlock *, PredicateSet>::iterator {
        return map.find(bb);
    }
    auto find(llvm::Instruction *inst)
        -> llvm::DenseMap<llvm::BasicBlock *, PredicateSet>::iterator {
        return map.find(inst->getParent());
    }
    auto begin() -> decltype(map.begin()) { return map.begin(); }
    auto end() -> decltype(map.end()) { return map.end(); }
    auto operator[](llvm::BasicBlock *bb) -> std::optional<Predicates> {
        auto it = map.find(bb);
        if (it == map.end())
            return std::nullopt;
        return Predicates{it->second, predicates};
    }
    auto operator[](llvm::Instruction *inst) -> std::optional<Predicates> {
        return (*this)[inst->getParent()];
    }
    void insert(std::pair<llvm::BasicBlock *, PredicateSet> &&pair) {
        map.insert(std::move(pair));
    }
};

struct PredicateOld {
    [[no_unique_address]] llvm::Value *condition;
    [[no_unique_address]] bool flip{false};
    auto operator!() -> PredicateOld { return {condition, !flip}; }
    PredicateOld(llvm::Value *condition, bool flip = false)
        : condition(condition), flip(flip) {}
    friend auto operator<<(llvm::raw_ostream &os, const PredicateOld &pred)
        -> llvm::raw_ostream & {
        if (pred.flip)
            os << "!";
        return os << *pred.condition;
    }
    auto operator==(const PredicateOld &p) const -> bool {
        return (condition == p.condition) && (flip == p.flip);
    }
    enum MatchResult { NoMatch, Match, MatchAndFlip };
    [[nodiscard]] auto match(const PredicateOld &p) const -> MatchResult {
        if (condition == p.condition)
            return flip == p.flip ? Match : MatchAndFlip;
        return NoMatch;
    }
};
struct PredicatesOld {
    [[no_unique_address]] llvm::SmallVector<PredicateOld, 3> pred;
    [[nodiscard]] auto size() const -> size_t { return pred.size(); }
    auto operator&(llvm::Value *cond) -> PredicatesOld {
        PredicatesOld newPreds;
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
    auto operator&=(llvm::Value *cond) -> PredicatesOld & {
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
    auto dropLastCondition() -> PredicatesOld & {
        pred.pop_back();
        return *this;
    }
    auto flipLastCondition() -> PredicatesOld & {
        pred.back() = !pred.back();
        return *this;
    }
    auto begin() { return pred.begin(); }
    auto end() { return pred.end(); }
    [[nodiscard]] auto begin() const { return pred.begin(); }
    [[nodiscard]] auto end() const { return pred.end(); }
    void clear() { pred.clear(); }
    [[nodiscard]] auto empty() const -> bool { return pred.empty(); }
    [[nodiscard]] auto emptyIntersection(const PredicatesOld &p) const -> bool {
        for (auto a : *this)
            for (auto b : p)
                if (a.match(b) == PredicateOld::MatchAndFlip)
                    return true;
        return false;
    }
    /// Returns a new predicate if the intersection is non-empty
    auto operator&(const PredicatesOld &p) const
        -> std::optional<PredicatesOld> {
        // auto x = llvm::Intrinsic::sqrt;
        PredicatesOld ret;
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
    /// an intersection of predicates from `a` or `b` in `a | b`
    /// expression
    auto operator|(PredicatesOld p) const -> std::optional<PredicatesOld> {
        switch (size()) {
        case 0:
            return *this;
        case 1:
            switch (p.size()) {
            case 0:
                return p;
            case 1:
                switch (pred[0].match(p.pred[0])) {
                case PredicateOld::Match:
                    return p;
                case PredicateOld::MatchAndFlip:
                    return PredicatesOld{};
                case PredicateOld::NoMatch:
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
    friend auto operator<<(llvm::raw_ostream &os, const PredicatesOld &pred)
        -> llvm::raw_ostream & {
        os << "[";
        for (size_t i = 0; i < pred.size(); ++i) {
            if (i)
                os << ", ";
            os << pred.pred[i];
        }
        os << "]";
        return os;
    }
    auto operator==(const PredicatesOld &p) const -> bool {
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
    [[no_unique_address]] PredicatesOld predicates;
    [[no_unique_address]] llvm::BasicBlock *basicBlock;
    // PredicatedBasicBlock(const PredicatedBasicBlock &) = default;
    PredicatedBasicBlock() = default;
    PredicatedBasicBlock(llvm::BasicBlock *basicBlock)
        : predicates(PredicatesOld{}), basicBlock(basicBlock) {}
    PredicatedBasicBlock(PredicatesOld predicates, llvm::BasicBlock *basicBlock)
        : predicates(std::move(predicates)), basicBlock(basicBlock) {}
    auto dropLastCondition() -> PredicatedBasicBlock & {
        predicates.dropLastCondition();
        return *this;
    }
    auto operator==(const PredicatedBasicBlock &pbb) const -> bool {
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
    auto conditionOnLastPred() -> PredicatedChain & {
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
    void emplace_back(PredicatesOld p, llvm::BasicBlock *BB) {
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
    static auto contains(llvm::ArrayRef<PredicatedBasicBlock> chain,
                         llvm::BasicBlock *BB) -> bool {
        for (auto &&pbb : chain)
            if (pbb.basicBlock == BB)
                return true;
        return false;
    }
    auto contains(llvm::BasicBlock *BB) -> bool { return contains(chain, BB); }
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
    [[nodiscard]] auto size() const { return chain.size(); }
    auto operator[](size_t i) -> auto & { return chain[i]; }
    auto operator[](size_t i) const -> auto & { return chain[i]; }
    auto back() -> auto & { return chain.back(); }
    [[nodiscard]] auto back() const -> auto & { return chain.back(); }
    auto front() -> auto & { return chain.front(); }
    [[nodiscard]] auto front() const -> auto & { return chain.front(); }

    [[nodiscard]] auto containsPredicates() const -> bool {
        for (auto &&pbb : chain)
            if (!pbb.predicates.empty())
                return true;
        return false;
    }
};
