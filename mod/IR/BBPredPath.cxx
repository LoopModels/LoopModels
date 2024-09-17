#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/IR/BasicBlock.h>

#ifndef USE_MODULE
#include "IR/Predicate.cxx"
#include "IR/Node.cxx"
#include "Containers/UnrolledList.cxx"
#include "Containers/Pair.cxx"
#include "Dicts/MapVector.cxx"
#else
export module IR:BBPredPath;

import MapVector;
import Pair;
import UnrolledList;
import :Node;
import :Predicate;
#endif

#ifdef USE_MODULE
export namespace IR::Predicate {
#else
namespace IR::Predicate {
#endif
using dict::OrderedMap;
class Map {
  // chain is in reverse order, which is actually what we want
  // as we parse backwards.
  OrderedMap<llvm::BasicBlock *, Set> map;
  containers::UList<Value *> *predicates;

public:
  Map(Arena<> *alloc) : map(alloc) {}
  Map(const Map &x) = default;
  Map(Map &&x) noexcept : map{x.map} {}
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
  [[nodiscard]] auto
  find(llvm::BasicBlock *bb) -> containers::Pair<llvm::BasicBlock *, Set> * {
    return map.find(bb);
  }
  [[nodiscard]] auto
  find(llvm::Instruction *inst) -> containers::Pair<llvm::BasicBlock *, Set> * {
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
  void insert(containers::Pair<llvm::BasicBlock *, Set> &&pair) {
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

}; // struct Map

} // namespace IR::Predicate