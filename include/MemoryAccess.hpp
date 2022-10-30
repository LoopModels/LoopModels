#pragma once
#include "./Schedule.hpp"

// struct MemorySchedule{

// }
// TODO:
// refactor to use GraphTraits.h
// https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/ADT/GraphTraits.h
struct MemoryAccess {
    ArrayReference ref;
    // unsigned ref; // index to ArrayReference
    llvm::User *user;
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
    MemoryAccess(ArrayReference ref, llvm::User *user, Schedule schedule,
                 bool isLoad)
        : ref(std::move(ref)), user(user), schedule(schedule),
          edgesIn(llvm::SmallVector<unsigned>()),
          edgesOut(llvm::SmallVector<unsigned>()), isLoad(isLoad){};
    // MemoryAccess(const MemoryAccess &MA) = default;
    
    void addEdgeIn(unsigned i) { edgesIn.push_back(i); }
    void addEdgeOut(unsigned i) { edgesOut.push_back(i); }
    // size_t getNumLoops() const { return ref->getNumLoops(); }
    // size_t getNumAxes() const { return ref->axes.size(); }
    // std::shared_ptr<AffineLoopNest> loop() { return ref->loop; }
    bool fusedThrough(MemoryAccess &x) {
        // originally separate loops could be fused
        // if (loop() != x.loop()){ return false; }
        return schedule.fusedThrough(x.schedule);
    }
    size_t getNumLoops() const { return schedule.getNumLoops(); }
    auto indexMatrix() { return ref.indexMatrix(); }
    auto indexMatrix() const { return ref.indexMatrix(); }
    // note returns true if unset
    PtrVector<int64_t> getSchedule(size_t loop) const {
        return schedule.getPhi()(loop, _);
    }
};
