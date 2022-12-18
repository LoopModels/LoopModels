#pragma once

#include "./Loops.hpp"
#include "./Math.hpp"
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

    ArrayReference() = delete;

    [[nodiscard]] auto isLoad() const -> bool {
        return llvm::isa<llvm::LoadInst>(loadOrStore);
    }
    // TODO: `constexpr` once `llvm::SmallVector` supports it
    [[nodiscard]] auto getArrayDim() const -> size_t { return sizes.size(); }
    [[nodiscard]] auto getNumSymbols() const -> size_t {
        return 1 + symbolicOffsets.size();
    }
    [[nodiscard]] auto getNumLoops() const -> size_t {
        return loop->getNumLoops();
    }

    [[nodiscard]] auto getAlignment() const -> llvm::Align {
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
    auto indexMatrix() -> MutPtrMatrix<int64_t> {
        const size_t d = getArrayDim();
        return MutPtrMatrix<int64_t>{indices.data(), getNumLoops(), d, d};
    }
    [[nodiscard]] auto indexMatrix() const -> PtrMatrix<int64_t> {
        const size_t d = getArrayDim();
        return PtrMatrix<int64_t>{indices.data(), getNumLoops(), d, d};
    }
    auto offsetMatrix() -> MutPtrMatrix<int64_t> {
        const size_t d = getArrayDim();
        const size_t numSymbols = getNumSymbols();
        return MutPtrMatrix<int64_t>{indices.data() + getNumLoops() * d, d,
                                     numSymbols, numSymbols};
    }
    [[nodiscard]] auto offsetMatrix() const -> PtrMatrix<int64_t> {
        const size_t d = getArrayDim();
        const size_t numSymbols = getNumSymbols();
        return PtrMatrix<int64_t>{indices.data() + getNumLoops() * d, d,
                                  numSymbols, numSymbols};
    }
    ArrayReference(const ArrayReference &a, PtrMatrix<int64_t> newInds)
        : indices(a.indices.size()), basePointer(a.basePointer), loop(a.loop),
          loadOrStore(a.loadOrStore), sizes(a.sizes),
          symbolicOffsets(a.symbolicOffsets) {
        indexMatrix() = newInds;
    }
    ArrayReference(const ArrayReference &a, AffineLoopNest<true> *loop,
                   PtrMatrix<int64_t> newInds)
        : indices(a.indices.size()), basePointer(a.basePointer), loop(loop),
          loadOrStore(a.loadOrStore), sizes(a.sizes),
          symbolicOffsets(a.symbolicOffsets) {
        indexMatrix() = newInds;
    }
    /// initialize alignment from an elSize SCEV.
    static auto typeAlignment(const llvm::SCEV *S) -> llvm::Align {
        if (auto *C = llvm::dyn_cast<llvm::SCEVConstant>(S)) {
            return llvm::Align(C->getAPInt().getZExtValue());
        }
        return llvm::Align{1};
    }
    ArrayReference(
        const llvm::SCEVUnknown *basePointer, AffineLoopNest<true> *loop,
        llvm::Instruction *loadOrStore = nullptr,
        llvm::SmallVector<const llvm::SCEV *, 3> sizes = {},
        llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets = {})
        : basePointer(basePointer), loop(loop), loadOrStore(loadOrStore),
          sizes(std::move(sizes)),
          symbolicOffsets(std::move(symbolicOffsets)){};

    void resize(size_t d) {
        sizes.resize(d);
        indices.resize(d * (getNumLoops() + getNumSymbols()));
    }
    ArrayReference(
        const llvm::SCEVUnknown *basePointer, AffineLoopNest<true> *loop,
        size_t dim, llvm::Instruction *loadOrStore = nullptr,
        llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets = {})
        : basePointer(basePointer), loop(loop), loadOrStore(loadOrStore),
          symbolicOffsets(std::move(symbolicOffsets)) {
        resize(dim);
    };
    ArrayReference(
        const llvm::SCEVUnknown *basePointer, AffineLoopNest<true> &loop,
        size_t dim, llvm::Instruction *loadOrStore = nullptr,
        llvm::SmallVector<const llvm::SCEV *, 3> symbolicOffsets = {})
        : basePointer(basePointer), loop(&loop), loadOrStore(loadOrStore),
          symbolicOffsets(std::move(symbolicOffsets)) {
        resize(dim);
    };
    [[nodiscard]] auto isLoopIndependent() const -> bool {
        return LinearAlgebra::allZero(indices);
    }
    [[nodiscard]] auto allConstantIndices() const -> bool {
        return symbolicOffsets.size() == 0;
    }
    // Assumes strides and offsets are sorted
    [[nodiscard]] auto sizesMatch(const ArrayReference &x) const -> bool {
        if (getArrayDim() != x.getArrayDim())
            return false;
        for (size_t i = 0; i < getArrayDim(); ++i)
            if (sizes[i] != x.sizes[i])
                return false;
        return true;
    }

    friend auto operator<<(llvm::raw_ostream &os, const ArrayReference &ar)
        -> llvm::raw_ostream & {
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
        size_t numLoops = size_t(A.numRow());
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
    [[nodiscard]] auto gcdKnownIndependent(const ArrayReference &) const
        -> bool {
        // TODO: handle this!
        // consider `x[2i]` vs `x[2i + 1]`, the former
        // will have a stride of `2`, and the latter of `x[2i+1]`
        // Additionally, in the future, we do
        return false;
    }
};
