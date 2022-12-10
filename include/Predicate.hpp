#pragma once
#include "./BitSets.hpp"
#include "./Macro.hpp"
#include "./Math.hpp"
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/Allocator.h>

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
struct PredicateIntersection {
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
    constexpr auto operator&(PredicateIntersection other) const
        -> PredicateIntersection {
        return {predicates | other.predicates};
    }
    auto operator&=(PredicateIntersection other) -> PredicateIntersection & {
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
    constexpr auto isEmpty() const -> bool { return isEmpty(predicates); }
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
    [[no_unique_address]] llvm::SmallVector<PredicateIntersection, 1> relations;
    auto operator[](size_t index) const -> PredicateRelation {
        return static_cast<PredicateRelation>(
            (relations[index / 32] >> (2 * (index % 32))) & 3);
    }
    void set(size_t index, PredicateRelation value) {
        auto d = index / 32;
        if (d >= relations.size())
            relations.resize(d + 1);
        auto r2 = 2 * (index % 32);
        uint64_t maskedOff = relations[d] & ~(3ULL << (r2));
        relations[d] = maskedOff | static_cast<uint64_t>(value) << (r2);
    }
    struct Reference {
        [[no_unique_address]] uint64_t *rp;
        [[no_unique_address]] size_t index;
        operator PredicateRelation() const {
            return static_cast<PredicateRelation>((*rp) >> index);
        }
        auto operator=(PredicateRelation relation) -> Reference & {
            *this->rp = (*this->rp & ~(3 << index)) |
                        (static_cast<uint64_t>(relation) << index);
            return *this;
        }
    };

    auto operator[](size_t index) -> Reference {
        auto i = index / 32;
        if (i >= relations.size())
            relations.resize(i + 1);
        return {&relations[i], 2 * (index % 32)};
    }
    [[nodiscard]] auto size() const -> size_t { return relations.size() * 32; }
    [[nodiscard]] auto relationSize() const -> size_t {
        return relations.size();
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
    void predUnion(llvm::BumpPtrAllocator &alloc,
                   const PredicateRelations &other) {
        if (other.isEmpty())
            return;
        else if (isEmpty()) {
            relations = other.relations;
            return;
        }
        size_t N = std::max(size(), other.size());
        if (relations.size() < N)
            relations.resize(N);
        // `&` because `0` is `Any`
        // and `Any` is the preferred default initialization
        for (size_t i = 0; i < N; i++) {
            uint64_t a = relations[i];
            uint64_t b = other.relations[i];
            if (a == b)
                continue;
            // we want to iterate to all pred relations where they are not equal
            uint64_t c = a ^ b;
            while (true) {
                uint64_t tz = std::countr_zero(c);
                uint64_t shift = tz & (~(uint64_t(1)));
                // shift to non-zero relation
                a >>= shift;
                b >>= shift;
                c >>= shift;
            }
        }
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
    [[nodiscard]] auto predIntersect(const PredicateRelations &other) const
        -> PredicateRelations {
        PredicateRelations result;
        if (relationSize() < other.relationSize()) {
            intersectImpl(result, other, *this);
        } else {
            intersectImpl(result, *this, other);
        }
        return result;
    }

    [[nodiscard]] auto isEmpty() const -> bool {
        for (uint64_t x : relations)
            if (isEmpty(x))
                return true;
        return false;
    }
    [[nodiscard]] auto emptyIntersection(const PredicateRelations &other) const
        -> bool {
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
    static auto getIndex(llvm::SmallVectorImpl<Instruction *> &instructions,
                         Instruction *instruction) -> size_t {
        size_t I = instructions.size();
        for (size_t i = 0; i < I; i++)
            if (instructions[i] == instruction)
                return i;
        instructions.push_back(instruction);
        return I;
    }
    // PredicateRelations() = default;
    // PredicateRelations(llvm::BumpPtrAllocator &alloc, Instruction::Cache &ic,
    //                    llvm::SmallVector<Instruction *> &predicates,
    //                    Predicates &pred) {
    //     for (Predicate &p : pred) {
    //         Instruction *i = ic.get(alloc, p.condition);
    //         size_t index = getIndex(predicates, i);
    //         PredicateRelation val =
    //             p.flip ? PredicateRelation::False : PredicateRelation::True;
    //         set(index, val);
    //     }
    // }
};
struct Predicates {
    [[no_unique_address]] PredicateRelations predicates;
    // `SmallVector` to copy, as `llvm::ArrayRef` wouldn't be safe in case of
    // realloc
    [[no_unique_address]] llvm::SmallVector<Instruction *> instr;
};

struct PredicateMap {
    llvm::DenseMap<llvm::BasicBlock *, PredicateRelations> map;
    llvm::SmallVector<Instruction *> predicates;
    auto get(llvm::BasicBlock *bb) -> PredicateRelations & { return map[bb]; }
    auto find(llvm::BasicBlock *bb)
        -> llvm::DenseMap<llvm::BasicBlock *, PredicateRelations>::iterator {
        return map.find(bb);
    }
    auto find(llvm::Instruction *inst)
        -> llvm::DenseMap<llvm::BasicBlock *, PredicateRelations>::iterator {
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
    void insert(std::pair<llvm::BasicBlock *, PredicateRelations> &&pair) {
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
    /// an intersection of predicates from `a` or `b` in `a | b` expression
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
