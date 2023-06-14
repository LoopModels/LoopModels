#pragma once
#include "Dicts/MapVector.hpp"
#include "IR/Cache.hpp"
#include "IR/Predicate.hpp"
namespace poly::IR::Predicate {
using dict::MapVector;
class Map {
  MapVector<llvm::BasicBlock *, Set> map;
  UList<Value *> *predicates;

public:
  constexpr Map(BumpAlloc<> &alloc) : map(alloc) {}
  Map(const Map &x) = default;
  Map(Map &&x) noexcept : map{std::move(x.map)} {}
  auto operator=(const Map &) -> Map & = default;
  auto operator=(Map &&) -> Map & = default;
  [[nodiscard]] auto size() const -> size_t { return map.size(); }
  [[nodiscard]] auto empty() const -> bool { return map.empty(); }
  [[nodiscard]] auto isDivergent() const -> bool {
    if (size() < 2) return false;
    for (auto I = map.begin(), E = map.end(); I != E; ++I) {
      if (I->second.empty()) continue;
      for (const auto *J = std::next(I); J != E; ++J) {
        // NOTE: we don't need to check`isEmpty()`
        // because `emptyIntersection()` returns `false`
        // when isEmpty() is true.
        if (I->second.intersectionIsEmpty(J->second)) return true;
      }
    }
    return false;
  }
  auto getPredicates() { return predicates; }
  [[nodiscard]] auto getEntry() const -> llvm::BasicBlock * {
    return map.back().first;
  }
  // [[nodiscard]] auto get(llvm::BasicBlock *bb) -> Set & { return map[bb]; }
  [[nodiscard]] auto find(llvm::BasicBlock *bb) { return map.find(bb); }
  [[nodiscard]] auto find(llvm::Instruction *inst) {
    return map.find(inst->getParent());
  }
  // we insert into map in reverse order, so our iterators reverse
  [[nodiscard]] auto begin() { return std::reverse_iterator(map.end()); }
  [[nodiscard]] auto end() { return std::reverse_iterator(map.begin()); }
  [[nodiscard]] auto rbegin() { return map.begin(); }
  [[nodiscard]] auto rend() { return map.end(); }
  [[nodiscard]] auto operator[](llvm::BasicBlock *bb) -> Set {
    auto *it = map.find(bb);
    if (it == map.end()) return {};
    return it->second;
  }
  [[nodiscard]] auto operator[](llvm::Instruction *inst) -> std::optional<Set> {
    return (*this)[inst->getParent()];
  }
  void insert(std::pair<llvm::BasicBlock *, Set> &&pair) {
    map.insert(std::move(pair));
  }
  [[nodiscard]] auto contains(llvm::BasicBlock *BB) const -> bool {
    return map.count(BB);
  }
  [[nodiscard]] auto isInPath(llvm::BasicBlock *BB) -> bool {
    auto *f = find(BB);
    if (f == rend()) return false;
    return !f->second.empty();
  }
  [[nodiscard]] auto isInPath(llvm::Instruction *I) -> bool {
    return isInPath(I->getParent());
  }
  void clear() { map.clear(); }
  // void visit(llvm::BasicBlock *BB) { map.insert(std::make_pair(BB,
  // Set())); } void visit(llvm::Instruction *inst) {
  // visit(inst->getParent()); }
  [[nodiscard]] auto addPredicate(IR::Cache &cache, llvm::Value *value)
    -> size_t {
    auto *I = cache.getInstruction(*this, value);
    assert(predicates->count <= 32 && "too many predicates");
    for (size_t i = 0; i < cache.predicates.size(); ++i)
      if (cache.predicates[i] == I) return i;
    size_t i = cache.predicates.size();
    assert(cache.predicates.size() != 32 && "too many predicates");
    cache.predicates.emplace_back(I);
    return i;
  }
  void reach(BumpAlloc<> &alloc, llvm::BasicBlock *BB, Intersection predicate) {
    // because we may have inserted into predMap, we need to look up
    // again rather than being able to reuse anything from the
    // `visit`.
    if (auto *f = find(BB); f != rend()) f->second |= predicate;
    else map.insert({BB, Set{alloc, predicate}});
  }
  void assume(Intersection predicate) {
    for (auto &&pair : map) pair.second &= predicate;
  };
  enum class Destination { Reached, Unreachable, Returned, Unknown };
  // TODO:
  // 1. see why L->contains(BBsrc) does not work; does it only contain BBs
  // in it directly, and not nested another loop deeper?
  // 2. We are ignoring cycles for now; we must ensure this is done
  // correctly
  [[nodiscard]] static auto // NOLINTNEXTLINE(misc-no-recursion)
  descendBlock(IR::Cache &cache, aset<llvm::BasicBlock *> &visited,
               Predicate::Map &predMap, llvm::BasicBlock *BBsrc,
               llvm::BasicBlock *BBdst, Predicate::Intersection predicate,
               llvm::BasicBlock *BBhead, llvm::Loop *L) -> Destination {
    if (BBsrc == BBdst) {
      assert(!predMap.contains(BBsrc));
      predMap.insert({BBsrc, Set{alloc, predicate}});
      return Destination::Reached;
    }
    if (L && (!(L->contains(BBsrc)))) {
      // oops, we seem to have skipped the preheader and escaped the
      // loop.
      return Destination::Returned;
    }
    if (visited.contains(BBsrc)) {
      // FIXME: This is terribly hacky.
      // if `BBsrc == BBhead`, then we assume we hit a path that
      // bypasses the following loop, e.g. there was a loop guard.
      //
      // Thus, we return `Returned`, indicating that it was a
      // non-fatal dead-end. Otherwise, we check if it seems to have
      // led to a live, non-empty path.
      // TODO: should we union the predicates in case of returned?
      if ((BBsrc != BBhead) && predMap.find(BBsrc) != predMap.rend())
        return Destination::Reached;
      return Destination::Returned;
    }
    // Inserts a tombstone to indicate that we have visited BBsrc, but
    // not actually reached a destination.
    visited.insert(BBsrc);
    const llvm::Instruction *I = BBsrc->getTerminator();
    if (!I) return Destination::Unknown;
    if (llvm::isa<llvm::ReturnInst>(I)) return Destination::Returned;
    if (llvm::isa<llvm::UnreachableInst>(I)) return Destination::Unreachable;
    const auto *BI = llvm::dyn_cast<llvm::BranchInst>(I);
    if (!BI) return Destination::Unknown;
    if (BI->isUnconditional()) {
      auto rc = descendBlock(alloc, cache, visited, predMap,
                             BI->getSuccessor(0), BBdst, predicate, BBhead, L);
      if (rc == Destination::Reached) predMap.reach(alloc, BBsrc, predicate);
      return rc;
    }
    // We have a conditional branch.
    llvm::Value *cond = BI->getCondition();
    // We need to check both sides of the branch and add a predicate.
    size_t predInd = predMap.addPredicate(alloc, cache, cond);
    auto rc0 = descendBlock(
      alloc, cache, visited, predMap, BI->getSuccessor(0), BBdst,
      predicate.intersect(predInd, Predicate::Relation::True), BBhead, L);
    if (rc0 == Destination::Unknown) // bail
      return Destination::Unknown;
    auto rc1 = descendBlock(
      alloc, cache, visited, predMap, BI->getSuccessor(1), BBdst,
      predicate.intersect(predInd, Predicate::Relation::False), BBhead, L);
    if ((rc0 == Destination::Returned) || (rc0 == Destination::Unreachable)) {
      if (rc1 == Destination::Reached) {
        //  we're now assuming that !cond
        predMap.assume(
          Predicate::Intersection(predInd, Predicate::Relation::False));
        predMap.reach(alloc, BBsrc, predicate);
      }
      return rc1;
    }
    if ((rc1 == Destination::Returned) || (rc1 == Destination::Unreachable)) {
      if (rc0 == Destination::Reached) {
        //  we're now assuming that cond
        predMap.assume(
          Predicate::Intersection(predInd, Predicate::Relation::True));
        predMap.reach(alloc, BBsrc, predicate);
      }
      return rc0;
    }
    if (rc0 != rc1) return Destination::Unknown;
    if (rc0 == Destination::Reached) predMap.reach(alloc, BBsrc, predicate);
    return rc0;
  }
  /// We bail if there are more than 32 conditions; control flow that
  /// branchy is probably not worth trying to vectorize.
  [[nodiscard]] static auto descend(IR::Cache &cache, llvm::BasicBlock *start,
                                    llvm::BasicBlock *stop, llvm::Loop *L)
    -> std::optional<Map> {
    auto p = alloc.checkpoint();
    Map pm{alloc};
    aset<llvm::BasicBlock *> visited{alloc};
    if (descendBlock(alloc, cache, visited, pm, start, stop, {}, start, L) ==
        Destination::Reached)
      return pm;
    alloc.rollback(p);
    return std::nullopt;
  }

}; // struct Map

} // namespace poly::IR::Predicate
