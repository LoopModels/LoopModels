#pragma once
#include "./Schedule.hpp"
#include "Macro.hpp"
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Support/raw_ostream.h>

// struct MemorySchedule{

// }
// TODO:
// refactor to use GraphTraits.h
// https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/ADT/GraphTraits.h
struct MemoryAccess {
    ArrayReference ref;
    // unsigned ref; // index to ArrayReference
    llvm::Instruction *user;
    Schedule schedule;
    // unsigned (instead of ptr) as we build up edges
    // and I don't want to relocate pointers when resizing vector
    llvm::SmallVector<unsigned> edgesIn;
    llvm::SmallVector<unsigned> edgesOut;
    llvm::SmallVector<unsigned> groups;
    [[no_unique_address]] unsigned index{std::numeric_limits<unsigned>::max()};
    [[no_unique_address]] unsigned nodeIndex{
        std::numeric_limits<unsigned>::max()};
    // schedule indicated by `1` top bit, remainder indicates loop
    [[no_unique_address]] bool isLoad;
    MemoryAccess(ArrayReference ref, llvm::Instruction *user, Schedule schedule,
                 bool isLoad)
        : ref(std::move(ref)), user(user), schedule(std::move(schedule)),
          edgesIn(llvm::SmallVector<unsigned>()),
          edgesOut(llvm::SmallVector<unsigned>()), isLoad(isLoad){};
    MemoryAccess(ArrayReference ref, llvm::Instruction *user, bool isLoad)
        : ref(std::move(ref)), user(user),
          edgesIn(llvm::SmallVector<unsigned>()),
          edgesOut(llvm::SmallVector<unsigned>()), isLoad(isLoad){};
    MemoryAccess(ArrayReference ref, llvm::Instruction *user,
                 llvm::ArrayRef<unsigned> omega, bool isLoad)
        : ref(std::move(ref)), user(user),
          schedule(llvm::ArrayRef<unsigned>{omega.data() + omega.size() -
                                                (ref.getNumLoops() + 1),
                                            ref.getNumLoops() + 1}),
          edgesIn(llvm::SmallVector<unsigned>()),
          edgesOut(llvm::SmallVector<unsigned>()), isLoad(isLoad){};
    // MemoryAccess(const MemoryAccess &MA) = default;

    inline void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
    inline void addEdgeOut(unsigned i) { edgesOut.push_back(i); }
    // size_t getNumLoops() const { return ref->getNumLoops(); }
    // size_t getNumAxes() const { return ref->axes.size(); }
    // std::shared_ptr<AffineLoopNest> loop() { return ref->loop; }
    inline bool fusedThrough(MemoryAccess &x) {
        // originally separate loops could be fused
        // if (loop() != x.loop()){ return false; }
        return schedule.fusedThrough(x.schedule);
    }
    inline size_t getNumLoops() const {
        // FIXME: assert is failing...
        SHOW(schedule.getNumLoops());
        CSHOWLN(ref.getNumLoops());
        assert(schedule.getNumLoops() == ref.getNumLoops());
        return schedule.getNumLoops();
    }
    inline MutPtrMatrix<int64_t> indexMatrix() { return ref.indexMatrix(); }
    inline PtrMatrix<int64_t> indexMatrix() const { return ref.indexMatrix(); }
    // note returns true if unset
    inline PtrMatrix<int64_t> getPhi() const { return schedule.getPhi(); }
    inline PtrVector<int64_t> getOmega() const { return schedule.getOmega(); }
    inline PtrVector<int64_t> getSchedule(size_t loop) const {
        return schedule.getPhi()(loop, _);
    }
    inline MemoryAccess *truncateSchedule() {
        llvm::errs() << "about to truncate schedule\n";
        schedule.truncate(ref.getNumLoops());
        SHOWLN(getNumLoops());
        return this;
    }
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &os, const MemoryAccess &m) {
    if (m.isLoad)
        os << "Load: ";
    else
        os << "Store: ";
    if (m.user)
        os << *m.user;
    os << "\n"
       << m.ref << "\nSchedule Omega: " << m.schedule.getOmega()
       << "\nAffineLoopNest: " << *m.ref.loop;
    return os;
}
