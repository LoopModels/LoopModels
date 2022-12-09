#pragma once
#include "./Schedule.hpp"
#include "Macro.hpp"
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>

// TODO:
// refactor to use GraphTraits.h
// https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/ADT/GraphTraits.h
struct MemoryAccess {
    [[no_unique_address]] ArrayReference ref;
    // omegas order is [outer <-> inner]
    [[no_unique_address]] llvm::SmallVector<unsigned, 8> omegas;
    [[no_unique_address]] llvm::SmallVector<unsigned> edgesIn;
    [[no_unique_address]] llvm::SmallVector<unsigned> edgesOut;
    [[no_unique_address]] BitSet<> nodeIndex;
    // unsigned (instead of ptr) as we build up edges
    // and I don't want to relocate pointers when resizing vector
    // schedule indicated by `1` top bit, remainder indicates loop
    [[nodiscard]] auto isLoad() const -> bool { return ref.isLoad(); }
    auto getInstruction() -> llvm::Instruction * { return ref.loadOrStore; }
    [[nodiscard]] auto getInstruction() const -> llvm::Instruction * {
        return ref.loadOrStore;
    }
    auto getLoad() -> llvm::LoadInst * {
        return llvm::dyn_cast<llvm::LoadInst>(ref.loadOrStore);
    }
    auto getStore() -> llvm::StoreInst * {
        return llvm::dyn_cast<llvm::StoreInst>(ref.loadOrStore);
    }

    inline void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
    inline void addEdgeOut(unsigned i) { edgesOut.push_back(i); }
    inline void addNodeIndex(unsigned i) { nodeIndex.insert(i); }
    MemoryAccess(ArrayReference r, llvm::Instruction *user,
                 llvm::SmallVector<unsigned, 8> omegas)
        : ref(std::move(r)), omegas(std::move(omegas)) {
        ref.loadOrStore = user;
    };
    MemoryAccess(ArrayReference r, llvm::Instruction *user)
        : ref(std::move(r)) {
        ref.loadOrStore = user;
    };
    MemoryAccess(ArrayReference r, llvm::Instruction *user,
                 llvm::ArrayRef<unsigned> o)
        : ref(std::move(r)), omegas(o.begin(), o.end()) {
        ref.loadOrStore = user;
    };
    MemoryAccess(ArrayReference r, llvm::SmallVector<unsigned, 8> omegas)
        : ref(std::move(r)), omegas(std::move(omegas)){};
    MemoryAccess(ArrayReference r) : ref(std::move(r)){};
    MemoryAccess(ArrayReference r, llvm::ArrayRef<unsigned> o)
        : ref(std::move(r)), omegas(o.begin(), o.end()){};
    // MemoryAccess(const MemoryAccess &MA) = default;

    // inline void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
    // inline void addEdgeOut(unsigned i) { edgesOut.push_back(i); }

    // size_t getNumLoops() const { return ref->getNumLoops(); }
    // size_t getNumAxes() const { return ref->axes.size(); }
    // std::shared_ptr<AffineLoopNest> loop() { return ref->loop; }
    inline auto fusedThrough(MemoryAccess &x) -> bool {
        bool allEqual = true;
        size_t numLoopsCommon = std::min(getNumLoops(), x.getNumLoops());
        for (size_t n = 0; n < numLoopsCommon; ++n)
            allEqual &= (omegas[n] == x.omegas[n]);
        return allEqual;
    }
    [[nodiscard]] inline auto getNumLoops() const -> size_t {
        size_t numLoops = ref.getNumLoops();
        assert(numLoops + 1 == omegas.size());
        return numLoops;
    }
    inline auto indexMatrix() -> MutPtrMatrix<int64_t> {
        return ref.indexMatrix();
    }
    [[nodiscard]] inline auto indexMatrix() const -> PtrMatrix<int64_t> {
        return ref.indexMatrix();
    }
    // note returns true if unset
    // inline PtrMatrix<int64_t> getPhi() const { return schedule.getPhi(); }
    [[nodiscard]] inline auto getFusionOmega() const -> PtrVector<unsigned> {
        return PtrVector<unsigned>{omegas.data(), omegas.size()};
    }
    // inline PtrVector<int64_t> getSchedule(size_t loop) const {
    //     return schedule.getPhi()(loop, _);
    // }
    inline auto truncateSchedule() -> MemoryAccess * {
        // we're truncating down to `ref.getNumLoops()`, discarding outer most
        size_t dropCount = omegas.size() - (ref.getNumLoops() + 1);
        if (dropCount)
            omegas.erase(omegas.begin(), omegas.begin() + dropCount);
        return this;
    }
};

auto operator<<(llvm::raw_ostream &os, const MemoryAccess &m)
    -> llvm::raw_ostream & {
    if (m.isLoad())
        os << "Load: ";
    else
        os << "Store: ";
    if (auto instr = m.getInstruction())
        os << *instr;
    os << "\n"
       << m.ref << "\nSchedule Omega: " << m.getFusionOmega()
       << "\nAffineLoopNest: " << *m.ref.loop;
    return os;
}
