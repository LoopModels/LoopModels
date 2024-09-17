#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#include <llvm/Support/Casting.h>

#ifndef USE_MODULE
#include "Containers/BitSets.cxx"
#include "Containers/Pair.cxx"
#include "Containers/Tuple.cxx"
#include "Dicts/Linear.cxx"
#include "IR/IR.cxx"
#include "Math/ManagedArray.cxx"
#include "Numbers/Int8.cxx"
#include "Utilities/Invariant.cxx"
#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <utility>
#else
export module CostModeling:RegisterLife;
import BitSet;
import Int8;
import Invariant;
import IR;
import LinearDict;
import ManagedArray;
import Pair;
import STL;
import Tuple;
#endif

#ifdef USE_MODULE
export namespace CostModeling::Register {
#else
namespace CostModeling::Register {
#endif
using containers::Pair, containers::Tuple;
using math::end, math::_;
using numbers::u8;
using utils::invariant;

/// Register Use Modeling
/// The primary goal is to estimate spill costs.
/// There are two components:
/// 1. Costs paid on block entry.
/// 2. Costs paid within a block.
/// `UsesAcrossBBs` is the primary data structure concerned with costs paid on
/// block entry. For data with lifetimes (definition to last-use) that span
/// more than 1 block, it tracks uses and spill-states. In a block where they
/// are used, any spilled variables must be reloaded. In a block where they
/// aren't used, they still add to spill cost, but with the option of cheaper,
/// hoisted spilling.
/// Within a block, we use `LiveRegisters`, which contains `intrablock` and
/// `interblock` sets. For the `intrablock`, we can consider unroll orders to
/// reduce the register cost. These are temporary; the register cost is the
/// product of all dependent loops unrolled interior to the first unrolled
/// independent loop, as we can reuse the same register for each new unrolled
/// value (dependent unrolls interior to an independent unroll are hoisted
/// out, thus consuming registers). On the other hand, `interblock` is for
/// uses that have lifetime rules forbidding this, e.g. because they must span
/// blocks. For these, the unroll cost is the full product of all dependent
/// loads.
/// We may have multiple snapshots of `LiveRegisters` within a basic block.
/// The `interblock`s are allowed to be consumed, e.g. the last use of a
/// variable defined at the same depth, or an `accumPhi`.
/// The LiveInfo objects with `UsedHere=1` indicate how much must be loaded as
/// we enter a block. The `interblock` is for cost calculations.
///
/// For `accumPhi`s,
///
///     v = foo();
///     for (int i = 0; i < I; ++i){
///       w = phi(v, y); // accum phi - counts as use of `v` but not `y`
///       x = bar(w);
///       y = qux(x);
///     }
///     z = phi(v, y); // join phi - counts as use of `y` but not `v`
///
/// we add the use of `v` in front of the loop, in the previous BB.
/// This is because `v` is consumed before the loop, replaced with `w`.
/// Only if it is also used elsewhere in the loop's BB would the BB need to
/// dedicate registers.
///
///
///
///
/// This is used for tracking spills/liveness across BBs.
/// The primary use of this is for estimating the cost of register spills.
///
/// Conceptually, the data structure represents a binary tree, rooted at the
/// last BB. For each node, we have used/not used. Future use patterns merge,
/// hence a binary tree rooted at the end.
/// For example, while these start different (0101 vs 1010):
/// 01011010101
/// 10101010101
/// they fuse after BB#4 and no longer need separate tracking.
///
/// The last BB only needs 'used'; unused would have no uses left and thus be
/// dropped. We don't hoist spilling of used, because they need to be loaded,
/// thus, used aren't really tracked, either. Thus, the second-to-last BB is
/// the "first" (if starting from the end) that we need data for.
///
/// To support the use of cost estimation, the data is organized by BB.
/// It could be viewed as a vector of BBs, where each BB has a vector of all
/// still relevant spill counts. The vector contains additions, as well as a
/// field for live.
///
/// As part of computation, we also want to hoist spills out as far as we can.
/// This means, we need to know if we have successive descents that also
/// aren't uses.
///
/// For building this object, we additionally need to store
/// the future use patterns.
/// Phi nodes should be consumable w/in their own block;
/// they represent memory (registers) set aside, but
/// %1 = phi(%0, %3)
/// %2 = A[0,i]*B[0,i] + %1 // last use within block
/// %3 = %2 - A[1,i]*B[1,i] // %1 phi is user
/// So, phi node should only count users within the block
/// for `remainingUses`?
/// Perhaps we should split remaining uses into a `std::array<int32_t,2>`
/// (or custom struct) indicating uses within BB and uses without?
struct UsesAcrossBBs {
  struct LiveInfo {
    // TODO: add hoistable arg, indicating where/how much we can hoist?
    // If `usedHere`, then all must become live.
    // If `!usedHere`, then we may spill.
    //
    // If used here, live count must be brought up to total + additional.
    // Otherwise, it is previous live(s) + additional.
    // Previous live(s) add to the `nextIdx`.
    // Note that using a lex ordering for uses gives us a fairly good
    // spill-preference.
    uint16_t used_here_ : 1;
    uint16_t dep_mask_ : 15;
    /// `additional` are added by instructions within the BB, and thus don't
    /// pay load costs.
    uint16_t additional_{};
    /// The total amount we need; load cost is
    /// total_count_ - additional_ - live_count_
    /// Note that additional_ is calculated in `updateUseAcrossBBs` as
    /// `total_count_ - prior_total_counts`; we also have
    /// 0 <= live_count_ <= prior_total_counts`, so given this and
    /// total_count_ - (total_count_ - prior_total_counts) - live_count_
    /// the load cost must be >= 0. (Just a double check.)
    uint16_t total_count_{};
    // uint16_t next_idx_{}; // used to point live_count_; 0 invalid
    std::array<u8, 2> prev_idxs_{};
  };
  static_assert(sizeof(LiveInfo) == 8);
  // gives all the liveness information for spills we need to track.
  // Length equals `liveCounts.sum()`
  math::Vector<LiveInfo> liveinfo_;
  // Vector with length=numBBs-1, yielding the number of counts.
  math::Vector<u8> live_counts_;
  constexpr void clear() {
    liveinfo_.clear();
    live_counts_.clear();
  }
};

class BBState {
  using LiveRegisters = dict::Linear<uint16_t, uint16_t>;
  math::Vector<LiveRegisters, 2> ephemeral_;
  math::Vector<math::Vector<LiveRegisters, 2>, 3> perennial_;
  int current_bb_{1};

  auto bb_reg(int idx) -> math::Vector<LiveRegisters, 2> & {
    return perennial_[idx];
  }
  auto live() -> LiveRegisters & { return perennial().back(); }

public:
  BBState(int numBlk)
    : ephemeral_{math::length(1)},
      perennial_{math::length(numBlk), ephemeral_} {}
  void checkpoint() {
    // FIXME: possible dangling references without `auto`?
    ephemeral_.emplace_back(auto{ephemeral_.back()});
    math::Vector<LiveRegisters, 2> &cur = perennial();
    cur.emplace_back(auto{cur.back()});
  }
  void free(IR::Instruction *lastuse) {
    if ((lastuse->getBlkIdx() != current_bb_) || IR::Phi::classof(lastuse))
      live().decRemoveIfNot(lastuse->loopMask());
    else ephemeral_.back().decRemoveIfNot(lastuse->loopMask());
  }
  // var becomes live from this point
  void defPerennialVar(uint16_t m) { ++live()[m]; }
  void defEphemeralVar(uint16_t m) { ++ephemeral_.back()[m]; }
  void usePerennial(uint16_t m, int uidx) { ++bb_reg(uidx).back()[m]; }
  /// adds to additional BBs, not added by `useInterBlock`
  void usePerennialConst(bool is_accum_phi) {
    math::Vector<LiveRegisters, 2> &regs = bb_reg(current_bb_ - is_accum_phi);
    for (LiveRegisters &lr : regs[_(0, end - 1)]) ++lr[0x00];
  }
  [[nodiscard]] constexpr auto getBlkIdx() const -> int { return current_bb_; }
  constexpr void incBB() {
    ++current_bb_;
    ephemeral_.resize(1);
    ephemeral_.back().clear();
  }
  auto perennial() -> math::Vector<LiveRegisters, 2> & {
    return perennial_[current_bb_];
  }
  auto ephemeral() -> math::Vector<LiveRegisters, 2> & { return ephemeral_; }
};
/// Used to assist in building `UsesAcrossBBs`.
struct FutureUses {
  // Search for a matching `uint16_t`, then...
  // We reverse the bits, so the last one is idx `0`.
  // This has two principle advantages:
  // 1. Earlier blocks have a higher lexicographical rank.
  // 2. We shrink the collection size as we move forward.
  //    In practice, this shouldn't matter often as we will rarely have
  //    more than `64` blocks to begin with.
  using BitSet = containers::BitSet<>;
  struct UseRecord {
    int16_t count_;
    // newly added invariants that may need loading
    // NOTE: We are assuming they need loading, although they may have been
    // produced in registers and not spilled, which we curently don't allow
    // for. This is most feasible for `Instruction*`s in BB0, i.e. the
    // outer-most loop preheader. We could support that by scanning it.
    int16_t new_invariants_;
    // `prev_idxs_` map from current to previoous
    // Value is `id` such that `uabb.liveinfo_[id + uses_offset]` yields
    // previous.
    std::array<int16_t, 2> prev_idxs_{{-1, -1}};
    BitSet uses_;
    constexpr auto operator<=>(const BitSet &s) const -> std::strong_ordering {
      return uses_ <=> s;
    }
    friend constexpr auto
    operator<=>(const BitSet &s, const UseRecord &x) -> std::strong_ordering {
      return s <=> x.uses_;
    }
    constexpr auto
    operator<=>(const UseRecord &s) const -> std::strong_ordering {
      return uses_ <=> s.uses_;
    }
    constexpr auto operator==(const BitSet &s) const -> bool {
      return uses_ == s;
    }
    friend constexpr auto operator==(const BitSet &s,
                                     const UseRecord &x) -> bool {
      return s == x.uses_;
    }
    constexpr auto operator==(const UseRecord &s) const -> bool {
      return uses_ == s.uses_;
    }
    void updateUseAcrossBBs(UsesAcrossBBs &uabb, bool used_here,
                            ptrdiff_t uses_offset, uint16_t mask) const {
      // This `uses` potentially corresponded to two `LiveInfo`s
      // These get set when fusing; we update `C->idx0` here to point to the
      // new `LifeInfo` we insert, and set `C->idx1=-1`.
      // Then, if we fuse the `UseRecord` afterwards, we may end up with
      // both `idx0` and `idx1` set for the next iter.
      // Alternatively, if it was just added, neither may be set.
      uint16_t idx{uint16_t(uabb.liveinfo_.size() - uses_offset)},
        ac{uint16_t(count_)}, tc{uint16_t(ac + new_invariants_)};
      UsesAcrossBBs::LiveInfo nli{used_here, mask, ac, tc};
      // we need to set `idx0` and `idx1`
      for (int i = 0; i < 2; ++i) {
        int id = prev_idxs_[i];
        if (id < 0) break;
        invariant(idx > 0);
        UsesAcrossBBs::LiveInfo &li{uabb.liveinfo_[id + uses_offset]};
        // li.next_idx_ = idx;
        nli.additional_ -= li.total_count_;
        // live_counts must be non-empty if id<0
        nli.prev_idxs_[i] = uabb.live_counts_.back() - u8(id);
      }
      // reserve is called in `incrementBlock`
      uabb.liveinfo_.push_back_within_capacity(nli);
    }
    void updateUses(UsesAcrossBBs &uabb, bool used_here, ptrdiff_t uses_offset,
                    uint16_t mask) {
      updateUseAcrossBBs(uabb, used_here, uses_offset, mask);
      count_ = int16_t(count_ + new_invariants_);
      new_invariants_ = 0;
    }
  };
  using UseRecords = math::Vector<UseRecord>;
  // 16 bits of space between `uint16_t` and `int32_t`
  math::Vector<Pair<uint16_t, UseRecords>> mask_use_sets_;
  int max_blk_idx_; // maxBlk+1 == numBlk
  // int lastLiveInfoIdx;
  auto findMask(uint16_t deps) -> Pair<uint16_t, UseRecords> * {
    return std::ranges::find_if(
      mask_use_sets_, [=](const auto &p) -> bool { return p.first == deps; });
  }
  constexpr auto found(const Pair<uint16_t, UseRecords> *f) const -> bool {
    return (f != mask_use_sets_.end());
  }
  // for this to work, we have to combine records as we make progress,
  // and clear the upper bits
  static auto findRecord(UseRecords &sets, const UseRecord &s) -> UseRecord * {
    // the records within a set are lexicographically sorted, so we can
    // use a binary search.
    return std::lower_bound(sets.begin(), sets.end(), s, std::greater{});
  }
  // A goal is to not treat leaves as special.
  // Inserting a dummy loop that doesn't do anything should not change
  // anything.
  /// returns `true` if any users are outside BB `blk`.
  /// if `true`, it inserts the use record.
  auto addUsers(const IR::Users &users, uint16_t deps, BBState &bb_state,
                int current_depth, int blk) -> Tuple<bool, uint16_t, int> {
    UseRecord rcrd{
      .count_ = (blk != 0), .new_invariants_ = (blk == 0), .uses_ = BitSet{}};
    BitSet &blks = rcrd.uses_;
    bool is_spillable = false;
    int num_users = users.size();
    uint16_t perennial_deps = 0;
    for (const IR::Instruction *user : users) {
      int uidx = user->getBlkIdx();
      invariant(blk <= uidx);
      invariant(uidx <= max_blk_idx_);
      if (const IR::Phi *PN = llvm::dyn_cast<IR::Phi>(user)) {
        // Four possibilities:
        //  - Either the first or second arg of a phi
        //  - Either an accumulate or join phi
        // v = foo(); // blk?
        // for (int i = 0; i < I; ++i){
        //   w = phi(v, y); // accum phi - uidx?
        //   x = bar(w);
        //   y = qux(x); // blk?
        // }
        // z = phi(v, y); // join phi - uidx?
        //
        //  - First arg of accum phi (e.g. w = phi(->v<-,y) )
        //    Treat as though use is in prior block, outside the loop, as it
        //    is consumed on the first iteration.
        //  - Second arg of accum phi (e.g. w = phi(v,->y<-) )
        //    Ignore: second arg means dep through next iteration.
        //  - First arg of join phi (e.g. z = phi(->v<-,y) )
        //    Ignore: first arg means loop didn't iter, same as update through
        //    loop.
        //  - Second arg of join phi (e.g. z = phi(v,->y<-) )
        //    Loop did iterate.
        // Ignore means we `continue`, and remove it from the users count.
        bool isacc = PN->isAccumPhi();
        invariant(!isacc || (current_depth <= PN->getCurrentDepth()));
        if ((isacc && (current_depth >= PN->getCurrentDepth())) ||
            (!isacc && (current_depth <= PN->getCurrentDepth()))) {
          --num_users;
          continue;
        }
        /// v's use by the accumPhi counts as though it is in front of the
        /// loop, not inside it.
        if (isacc) --uidx;
      }
      // NOTE: if `blk == uidx`, we do set as active.
      // This is to record its use here, where we add `additional` equal to
      // the total. Additional does not contribute to load cost:
      // Load cost is `total_count_ - additional_ - live_`
      //
      // If `blk == 0`, we add to `new_invariants_`, to effectively
      // treat it as a spilled value defined earlier, outside the loop.
      // We may eventually wish to not force spilling it in our cost modeling.
      blks.insert(max_blk_idx_ - uidx);
      is_spillable |= blk != uidx; // blk < uidx
      if (blk != uidx) bb_state.usePerennial(deps, uidx);
      if (blk != uidx) perennial_deps |= user->loopMask();
    }
    // If not used outside, then we return `deps` as the register-consumption
    // mask to use. Otherwise, we use `perennial_deps & deps`.
    if (!is_spillable) return {false, deps, num_users};
    // now we search for a match
    if (auto *match = findMask(deps); found(match))
      if (UseRecord *r = findRecord(match->second, rcrd);
          r != match->second.end() && r->uses_ == blks)
        blk ? ++(r->count_) : ++(r->new_invariants_);
      else match->second.insert(r, std::move(rcrd));
    else mask_use_sets_.emplace_back(deps, UseRecords{std::move(rcrd)});
    return {true, uint16_t(perennial_deps & deps), num_users};
  }
  // a struct ordered
  struct IdxPartion {
    ptrdiff_t idx_;
    bool fudge_;

    // because `fudge` is false, this `IdxPartion` will automatically be less
    // than any other of the same idx, but still greater than any of lesser idx.
    // Thus, a `std::lower_bound` will separate along `idx`.
    constexpr IdxPartion(ptrdiff_t idx) : idx_{idx}, fudge_{false} {}
    constexpr IdxPartion(const UseRecord &record)
      : idx_{record.uses_.maxValue()}, fudge_{true} {}

  private:
    friend constexpr auto operator==(IdxPartion, IdxPartion) -> bool = default;
    friend constexpr auto
    operator<=>(IdxPartion, IdxPartion) -> std::strong_ordering = default;
  };

  // We use rev order, so we may have, in order:
  // 111
  // 110
  // 101
  // 100
  // 011
  // 010
  // 001
  // 000
  // We want this order, as the cost function will start to spill registes
  // as it runs out. This places those used in the nearer future higher, making
  // it less likely we spill those.
  // In `incrementBlock`, we pop off the first use, and then search for matches.
  //
  //
  static void incrementBlock(UsesAcrossBBs &uses, int rm_idx,
                             ptrdiff_t uses_offset, ptrdiff_t old_end,
                             uint16_t mask, UseRecords &sets) {
    // One thing we do here is we `remove` the `currentBlk` bit.
    UseRecord *S = sets.begin(), *I = S, *E = sets.end(), *C = nullptr,
              *M = nullptr;
    if (I == E) return;
    if (ptrdiff_t needed_cap = uses.liveinfo_.size() + std::distance(I, E);
        uses.liveinfo_.getCapacity() < needed_cap)
      uses.liveinfo_.reserve(2 * needed_cap);
    // we may have two parallel streams to merge
    // We merge [I,M) and [M,E)
    if (I->uses_[rm_idx]) { // head active
      // we at least have active; do we have inactive?
      C = M = std::lower_bound(I + 1, E, IdxPartion{rm_idx}, std::greater{});
      if (C != E) {
        // we have two parallel streams to merge
        for (;;) {
          // may not be true, i.e. if I < C in the previous iter, then we
          // already removed it.
          I->uses_.remove(rm_idx);
          std::strong_ordering order = I->uses_ <=> C->uses_;
          bool less = order == std::strong_ordering::less;
          UseRecord *A = less ? C : I;
          A->prev_idxs_ = {short(uses.liveinfo_.size() - old_end)};
          A->updateUses(uses, !less, uses_offset, mask);
          if (less) {
            // C belongs first
            // need to rotate [I,...,M,...,C] -> [C, I,...,M,...]
            std::rotate(I, C, C + 1);
            ++M, ++C;
          } else if (order != std::strong_ordering::greater) {
            A->prev_idxs_[1] = short(uses.liveinfo_.size() - old_end);
            C->updateUses(uses, false, uses_offset, mask);
            I->count_ = int16_t(I->count_ + C->count_); // fuse
            // the number of `updateUses` calls corresponds to the number
            // of following incremeents, so we can use these distance
            // calculations to get the offsets.
            ++C;
          }
          if ((++I == M) || (C == E)) break;
        }
      }
    } else C = M = I;
    for (; I != M; ++I) {
      invariant(I->uses_.remove(rm_idx));
      I->updateUses(uses, true, uses_offset, mask);
    }
    for (; C != E; ++C, ++I) {
      C->updateUses(uses, false, uses_offset, mask);
      if (I != C) *I = std::move(*C);
    }
    sets.truncate(std::distance(S, I));
  }
  void incrementBlock(UsesAcrossBBs &uses, int current_blk) {
    // we are in `currentBlk`, and `uses` is up to date for all previous blks
    // We now aim to set `uses` up for this block, and then prepare our data
    // for the next block.
    //
    // After updating `uses` and before updating `this`, we have a
    // correspondence between them:
    //
    //     previousLive = uses.liveInfo[_(end - uses.liveCounts.back(), end)]
    //     previousLive.size() == total length across maskUseSets
    //
    // the `UseRecord` idxs give index of corresponding `previousLive` entry
    //
    // As we add users of the next block, we may add new userecords,
    // initialized with `idx{-1}` (idx < 0 indicates new.)
    //
    // To prepare `uses`,
    math::Vector<u8> &live_counts = uses.live_counts_;
    ptrdiff_t live_info_len = uses.liveinfo_.size(),
              uses_offset =
                live_info_len -
                (live_counts.empty() ? 0 : ptrdiff_t(live_counts.back()));
    for (auto &[m, bs] : mask_use_sets_)
      incrementBlock(uses, max_blk_idx_ - current_blk, uses_offset,
                     live_info_len, m, bs);
    live_counts.push_back(u8(uses.liveinfo_.size() - live_info_len));
  }
  /// When consuming an operand, we check whether that operand was
  /// created within this BB.
  /// Either way, we...
  ///  - Decrement the use among use-counts
  /// If it was created in this BB...
  ///  - We mark it as freeing an active register
  /// If it was created in a different BB...
  ///  - We mark it as a permanently active use.
  /// Returns `true` if this consumption freed a register.
  auto useOperand(dict::map<IR::Value *, ptrdiff_t> &remaining_uses,
                  BBState &bb_state, int consumer_depth, IR::Value *op,
                  bool is_accum_phi = false) -> IR::Instruction * {
    ptrdiff_t &uses = remaining_uses[op];
    if (uses == 0) { // means is a loop-invariant, defined outside loop
#ifndef NDEBUG
      // All uses of `V` should follow this, if we haven't yet added it
      // We check for a bug that we've hit `0` uses yet a use remains
      invariant(op->getCurrentDepth() == 0);
      if (auto *I = llvm::dyn_cast<IR::Instruction>(op))
        invariant(I->getBlkIdx() == 0);
      for (IR::Instruction *u : op->getUsers())
        invariant(u->getBlkIdx() >= bb_state.getBlkIdx());
#endif
      uses = op->getUsers().size();
      addUsers(op->getUsers(), 0x00, bb_state, 0, 0);
      bb_state.usePerennialConst(is_accum_phi);
    }
    // last use can be consumed so long as I's depth is <= op's
    if ((--uses) || (consumer_depth > op->getCurrentDepth())) return nullptr;
    return llvm::cast<IR::Instruction>(op);
    // TODO:
    // registers allocated in other BBs are normally marked permanent
    // throughout the block.
    // but for the BB where they are used last, they be consumed,
    // so it should be tracked when w/in that block they are free.
    // Therefore:
    // 1. we should replace our live sets with pairs indicating
    //    hoistable alloc and non-hoistable.
    // 2. track and checkpoint both
    // `interblock` filled from used registers
  }

  auto consumeOperands(dict::map<IR::Value *, ptrdiff_t> &remaining_uses,
                       BBState &bb_state, IR::Compute *C,
                       bool decreasing) -> bool {
    invariant(bb_state.getBlkIdx() == C->getBlkIdx());
    int consumer_depth = C->getCurrentDepth();
    IR::Instruction *I = nullptr;
    // if the amount of regs being used has been decreasing,
    // then we do not need to re-checkpoint.
    // In that case, `docheckpoint = false`, and `I == nullptr` with use
    // immediately `free`-ing any no-longer-used ops.
    // On ther other hand, if `docheckpoint = true`, `I` gets set
    // to the first `N` as we wait for another; if we find another,
    // we checkpoint.
    for (IR::Value *op : C->getOperands()) {
      IR::Instruction *N =
        useOperand(remaining_uses, bb_state, consumer_depth, op);
      if (!N) continue;
      if (I) {
        // at least two freed! checkpoint...
        if (!decreasing) {
          decreasing = true;
          bb_state.checkpoint();
        }
        bb_state.free(N);
      } else if (!decreasing) I = N;
      else bb_state.free(N);
    }
    if (I) bb_state.free(I);
    return decreasing;
  }
};

} // namespace CostModeling::Register
