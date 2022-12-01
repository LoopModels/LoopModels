#pragma once

#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Predicate.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/Support/Alignment.h>
#include <llvm/Support/raw_ostream.h>

/// `foo` and `bar` can share the same `AffineLoopNest` (of depth 3), but
/// `baz` needs its own (of depth 2):
/// for i = I, j = J
///   baz(i,j,...)
///   for k = K
///     foo(i,j,k,...)
///   end
/// end
/// for i = I, j = J, k = K
///   bar(i,j,k,...)
/// end
/// NOTE: strides are in row major order!
/// this is because we want stride ranks to be in decreasing order
struct ArrayReference {
    [[no_unique_address]] llvm::SmallVector<int64_t, 16> indices;
    [[no_unique_address]] const llvm::SCEVUnknown *basePointer;
    [[no_unique_address]] AffineLoopNest<true> *loop;
    [[no_unique_address]] llvm::Instruction *loadOrStore;
    [[no_unique_address]] llvm::SmallVector<const llvm::SCEV *, 3> sizes;
    [[no_unique_address]] llvm::SmallVector<const llvm::SCEV *, 3>
        symbolicOffsets;
    [[no_unique_address]] Predicates predicates;

    ArrayReference() = delete;

    bool isLoad() const { return llvm::isa<llvm::LoadInst>(loadOrStore); }
    size_t getArrayDim() const { return sizes.size(); }
    size_t getNumLoops() const { return loop->getNumLoops(); }
    size_t getNumSymbols() const { return 1 + symbolicOffsets.size(); }

    llvm::Align getAlignment() const {
        if (auto l = llvm::dyn_cast<llvm::LoadInst>(loadOrStore))
            return l->getAlign();
        else if (auto s = llvm::dyn_cast<llvm::StoreInst>(loadOrStore))
            return s->getAlign();
            // not a load or store
#if __cplusplus >= 202202L
        std::unreachable();
#endif
        return llvm::Align(1);
    }
    // static inline size_t requiredData(size_t dim, size_t numLoops){
    // 	return dim*numLoops +
    // }
    // indexMatrix()' * i == indices
    // indexMatrix() returns a getNumLoops() x arrayDim() matrix.
    // e.g. [ 1 1; 0 1] corresponds to A[i, i + j]
    // getNumLoops() x arrayDim()
    MutPtrMatrix<int64_t> indexMatrix() {
        const size_t d = getArrayDim();
        return MutPtrMatrix<int64_t>{indices.data(), getNumLoops(), d, d};
    }
    PtrMatrix<int64_t> indexMatrix() const {
        const size_t d = getArrayDim();
        return PtrMatrix<int64_t>{indices.data(), getNumLoops(), d, d};
    }
    MutPtrMatrix<int64_t> offsetMatrix() {
        const size_t d = getArrayDim();
        const size_t numSymbols = getNumSymbols();
        return MutPtrMatrix<int64_t>{indices.data() + getNumLoops() * d, d,
                                     numSymbols, numSymbols};
    }
    PtrMatrix<int64_t> offsetMatrix() const {
        const size_t d = getArrayDim();
        const size_t numSymbols = getNumSymbols();
        return PtrMatrix<int64_t>{indices.data() + getNumLoops() * d, d,
                                  numSymbols, numSymbols};
    }
    ArrayReference(const ArrayReference &a, PtrMatrix<int64_t> newInds,
                   Predicates p = {})
        : indices(a.indices.size()), basePointer(a.basePointer), loop(a.loop),
          loadOrStore(a.loadOrStore), sizes(a.sizes),
          symbolicOffsets(a.symbolicOffsets), predicates(std::move(p)) {
        indexMatrix() = newInds;
    }
    ArrayReference(const ArrayReference &a, AffineLoopNest<true> *loop,
                   PtrMatrix<int64_t> newInds, Predicates p = {})
        : indices(a.indices.size()), basePointer(a.basePointer), loop(loop),
          loadOrStore(a.loadOrStore), sizes(a.sizes),
          symbolicOffsets(a.symbolicOffsets), predicates(std::move(p)) {
        indexMatrix() = newInds;
    }
    /// initialize alignment from an elSize SCEV.
    static llvm::Align typeAlignment(const llvm::SCEV *S) {
        if (auto *C = llvm::dyn_cast<llvm::SCEVConstant>(S)) {
            return llvm::Align(C->getAPInt().getZExtValue());
        }
        return llvm::Align{1};
    }
    ArrayReference(
        const llvm::SCEVUnknown *basePointer, AffineLoopNest<true> *loop,
        llvm::Instruction *loadOrStore = nullptr,
        llvm::SmallVector<const llvm::SCEV *, 3> sizes = {},
        llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets = {},
        Predicates p = {})
        : basePointer(basePointer), loop(loop), loadOrStore(loadOrStore),
          sizes(std::move(sizes)), symbolicOffsets(std::move(symbolicOffsets)),
          predicates(std::move(p)){};

    void resize(size_t d) {
        sizes.resize(d);
        indices.resize(d * (getNumLoops() + getNumSymbols()));
    }
    ArrayReference(
        const llvm::SCEVUnknown *basePointer, AffineLoopNest<true> *loop,
        size_t dim, llvm::Instruction *loadOrStore = nullptr,
        llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets = {},
        Predicates p = {})
        : basePointer(basePointer), loop(loop), loadOrStore(loadOrStore),
          symbolicOffsets(std::move(symbolicOffsets)),
          predicates(std::move(p)) {
        resize(dim);
    };
    ArrayReference(
        const llvm::SCEVUnknown *basePointer, AffineLoopNest<true> &loop,
        size_t dim, llvm::Instruction *loadOrStore = nullptr,
        llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets = {},
        Predicates p = {})
        : basePointer(basePointer), loop(&loop), loadOrStore(loadOrStore),
          symbolicOffsets(std::move(symbolicOffsets)),
          predicates(std::move(p)) {
        resize(dim);
    };
    bool isLoopIndependent() const { return allZero(indices); }
    bool allConstantIndices() const { return symbolicOffsets.size() == 0; }
    // Assumes strides and offsets are sorted
    bool sizesMatch(const ArrayReference &x) const {
        if (getArrayDim() != x.getArrayDim())
            return false;
        for (size_t i = 0; i < getArrayDim(); ++i)
            if (sizes[i] != x.sizes[i])
                return false;
        return true;
    }

    friend llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                         const ArrayReference &ar) {
        SHOWLN(ar.indexMatrix());
        os << "ArrayReference " << *ar.basePointer
           << " (dim = " << ar.getArrayDim()
           << ", num loops: " << ar.getNumLoops();
        if (ar.sizes.size())
            os << ", element size: " << *ar.sizes.back();
        os << "):\n";
        PtrMatrix<int64_t> A{ar.indexMatrix()};
        SHOW(A.numRow());
        CSHOWLN(A.numCol());
        os << "Sizes: [";
        if (ar.sizes.size()) {
            os << " unknown";
            for (ptrdiff_t i = 0; i < ptrdiff_t(A.numCol()) - 1; ++i)
                os << ", " << *ar.sizes[i];
        }
        os << " ]\nSubscripts: [ ";
        size_t numLoops = A.numRow();
        for (size_t i = 0; i < A.numCol(); ++i) {
            if (i)
                os << ", ";
            bool printPlus = false;
            for (size_t j = numLoops; j-- > 0;) {
                if (int64_t Aji = A(j, i)) {
                    if (printPlus) {
                        if (Aji <= 0) {
                            Aji *= -1;
                            os << " - ";
                        } else
                            os << " + ";
                    }
                    if (Aji != 1)
                        os << Aji << '*';
                    os << "i_" << numLoops - j - 1 << " ";
                    printPlus = true;
                }
            }
            PtrMatrix<int64_t> offs = ar.offsetMatrix();
            for (size_t j = 0; j < offs.numCol(); ++j) {
                if (int64_t offij = offs(i, j)) {
                    if (printPlus) {
                        if (offij <= 0) {
                            offij *= -1;
                            os << " - ";
                        } else
                            os << " + ";
                    }
                    if (j) {
                        if (offij != 1)
                            os << offij << '*';
                        os << *ar.loop->S[j - 1];
                    } else
                        os << offij;
                    printPlus = true;
                }
            }
        }
        return os << "]";
    }
    // use gcd to check if they're known to be independent
    bool gcdKnownIndependent(const ArrayReference &) const {
        // TODO: handle this!
        // consider `x[2i]` vs `x[2i + 1]`, the former
        // will have a stride of `2`, and the latter of `x[2i+1]`
        // Additionally, in the future, we do
        return false;
    }
};
