#pragma once
#include "Dicts/MapVector.hpp"
#include "IR/Node.hpp"
#include "IR/Predicate.hpp"
namespace poly::IR {
class Cache;
struct TreeResult;
namespace Predicate {
using dict::OrderedMap;
class Map {
  // chain is in reverse order, which is actually what we want
  // as we parse backwards.
  OrderedMap<llvm::BasicBlock *, Set> map;
  UList<Value *> *predicates;

public:
  Map(Arena<> *alloc) : map(alloc) {}
  Map(const Map &x) = default;
  Map(Map &&x) noexcept : map{std::move(x.map)} {}
  // auto operator=(const Map &) -> Map & = default;
  auto operator=(Map &&) -> Map & = default;
  [[nodiscard]] auto size() const -> size_t { return map.size(); }
  [[nodiscard]] auto empty() const -> bool { return map.empty(); }
  [[nodiscard]] auto isDivergent() const -> bool {
    if (size() < 2) return false;
    for (auto I = map.begin(), E = map.end(); I != E; ++I) {
      if (I->second.empty()) continue;
      for (const auto *J = std::next(I); J != E; ++J)
        if (I->second.intersectionIsEmpty(J->second)) return true;
      // NOTE: we don't need to check`isEmpty()`
      // because `emptyIntersection()` returns `false`
      // when isEmpty() is true.
    }
    return false;
  }
  auto getPredicates() { return predicates; }
  // [[nodiscard]] auto getEntry() const -> llvm::BasicBlock * {
  //   return map.back().first;
  // }
  // [[nodiscard]] auto get(llvm::BasicBlock *bb) -> Set & { return map[bb]; }
  [[nodiscard]] auto find(llvm::BasicBlock *bb) { return map.find(bb); }
  [[nodiscard]] auto find(llvm::Instruction *inst) {
    return map.find(inst->getParent());
  }
  // we insert into map in reverse order, so our iterators reverse
  [[nodiscard]] auto begin() { return map.begin(); }
  [[nodiscard]] auto end() { return map.end(); }
  [[nodiscard]] auto rbegin() { return std::reverse_iterator(map.end()); }
  [[nodiscard]] auto rend() { return std::reverse_iterator(map.begin()); }
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
    return map.contains(BB);
  }
  [[nodiscard]] auto contains(llvm::Instruction *I) const -> bool {
    return map.contains(I->getParent());
  }
  [[nodiscard]] auto isInPath(llvm::BasicBlock *BB) -> bool {
    auto *f = find(BB);
    if (f == end()) return false;
    return !f->second.empty();
  }
  [[nodiscard]] auto isInPath(llvm::Instruction *I) -> bool {
    return isInPath(I->getParent());
  }
  void clear() { map.clear(); }
  // void visit(llvm::BasicBlock *BB) { map.insert(std::make_pair(BB,
  // Set())); } void visit(llvm::Instruction *inst) {
  // visit(inst->getParent()); }
  [[nodiscard]] inline auto addPredicate(Arena<> *alloc, IR::Cache &cache,
                                         llvm::Value *value, TreeResult &tr)
    -> size_t;
  void reach(Arena<> *alloc, llvm::BasicBlock *BB, Intersection predicate) {
    // because we may have inserted into predMap, we need to look up
    // again rather than being able to reuse anything from the
    // `visit`.
    if (auto *f = find(BB); f != end()) f->second.Union(alloc, predicate);
    else map.insert({BB, Set{predicate}});
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
  /// We bail if there are more than 32 conditions; control flow that
  /// branchy is probably not worth trying to vectorize.
  [[nodiscard]] inline static auto descend(Arena<> *, IR::Cache &,
                                           llvm::BasicBlock *,
                                           llvm::BasicBlock *, llvm::Loop *,
                                           TreeResult &) -> std::optional<Map>;
}; // struct Map

} // namespace Predicate
} // namespace poly::IR
