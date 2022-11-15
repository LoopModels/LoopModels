#pragma once
#include "./Schedule.hpp"
#include "Macro.hpp"
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Support/raw_ostream.h>


// TODO:
// refactor to use GraphTraits.h
// https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/ADT/GraphTraits.h
struct MemoryAccess {
    [[no_unique_address]] ArrayReference ref;
    // unsigned ref; // index to ArrayReference
    [[no_unique_address]] llvm::Instruction *user;
    // omegas order is [outer <-> inner]
    [[no_unique_address]] llvm::SmallVector<unsigned, 8> omegas;
    [[no_unique_address]] llvm::SmallVector<unsigned> edgesIn;
    [[no_unique_address]] llvm::SmallVector<unsigned> edgesOut;
    [[no_unique_address]] BitSet nodeIndex;
    // unsigned (instead of ptr) as we build up edges
    // and I don't want to relocate pointers when resizing vector
    // schedule indicated by `1` top bit, remainder indicates loop
    [[no_unique_address]] bool isLoad;
    inline void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
    inline void addEdgeOut(unsigned i) { edgesOut.push_back(i); }
    inline void addNodeIndex(unsigned i) { nodeIndex.insert(i); }
    MemoryAccess(ArrayReference ref, llvm::Instruction *user,
                 llvm::SmallVector<unsigned, 8> omegas, bool isLoad)
        : ref(std::move(ref)), user(user), omegas(std::move(omegas)),
          isLoad(isLoad){};
    MemoryAccess(ArrayReference ref, llvm::Instruction *user, bool isLoad)
        : ref(std::move(ref)), user(user), isLoad(isLoad){};
    MemoryAccess(ArrayReference ref, llvm::Instruction *user,
                 llvm::ArrayRef<unsigned> o, bool isLoad)
        : ref(std::move(ref)), user(user), omegas(o.begin(), o.end()),
          isLoad(isLoad){};
    // MemoryAccess(const MemoryAccess &MA) = default;

    // inline void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
    // inline void addEdgeOut(unsigned i) { edgesOut.push_back(i); }

    // size_t getNumLoops() const { return ref->getNumLoops(); }
    // size_t getNumAxes() const { return ref->axes.size(); }
    // std::shared_ptr<AffineLoopNest> loop() { return ref->loop; }
    inline bool fusedThrough(MemoryAccess &x) {
        bool allEqual = true;
        size_t numLoopsCommon = std::min(getNumLoops(), x.getNumLoops());
        for (size_t n = 0; n < numLoopsCommon; ++n)
            allEqual &= (omegas[n] == x.omegas[n]);
        return allEqual;
    }
    inline size_t getNumLoops() const {
        size_t numLoops = ref.getNumLoops();
        assert(numLoops + 1 == omegas.size());
        return numLoops;
    }
    inline MutPtrMatrix<int64_t> indexMatrix() { return ref.indexMatrix(); }
    inline PtrMatrix<int64_t> indexMatrix() const { return ref.indexMatrix(); }
    // note returns true if unset
    // inline PtrMatrix<int64_t> getPhi() const { return schedule.getPhi(); }
    inline PtrVector<unsigned> getFusionOmega() const {
        return PtrVector<unsigned>{omegas.data(), omegas.size()};
    }
    // inline PtrVector<int64_t> getSchedule(size_t loop) const {
    //     return schedule.getPhi()(loop, _);
    // }
    inline MemoryAccess *truncateSchedule() {
        // we're truncating down to `ref.getNumLoops()`, discarding outer most
        size_t dropCount = omegas.size() - (ref.getNumLoops() + 1);
        if (dropCount)
            omegas.erase(omegas.begin(), omegas.begin() + dropCount);
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
       << m.ref << "\nSchedule Omega: " << m.getFusionOmega()
       << "\nAffineLoopNest: " << *m.ref.loop;
    return os;
}
