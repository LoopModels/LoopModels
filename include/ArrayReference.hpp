#pragma once

#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Symbolics.hpp"
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallVector.h>

// `foo` and `bar` can share the same `AffineLoopNest` (of depth 3), but
// `baz` needs its own (of depth 2):
// for i = I, j = J
//   baz(i,j,...)
//   for k = K
//     foo(i,j,k,...)
//   end
// end
// for i = I, j = J, k = K
//   bar(i,j,k,...)
// end
// NOTE: strides are in row major order!
// this is because we want stride ranks to be in decreasing order
struct ArrayReference {
    size_t arrayID;
    llvm::IntrusiveRefCntPtr<AffineLoopNest> loop;
    // std::shared_ptr<AffineLoopNest> loop;
    llvm::SmallVector<MPoly, 3> strides;
    // llvm::Optional<IntMatrix>
    //     offsets; // symbolicOffsets * (loop->symbols)
    llvm::SmallVector<int64_t, 16> indices;
    unsigned rank;
    bool hasSymbolicOffsets; // normal case is not to

    size_t arrayDim() const { return strides.size(); }
    size_t getNumLoops() const { return loop->getNumLoops(); }
    size_t getNumSymbols() const {
        return hasSymbolicOffsets ? loop->getNumSymbols() : 1;
    }
    // static inline size_t requiredData(size_t dim, size_t numLoops){
    // 	return dim*numLoops +
    // }
    // indexMatrix()' * i == indices
    // indexMatrix() returns a getNumLoops() x arrayDim() matrix.
    // e.g. [ 1 1; 0 1] corresponds to A[i, i + j]
    // getNumLoops() x arrayDim()
    MutPtrMatrix<int64_t> indexMatrix() {
        const size_t d = arrayDim();
        return MutPtrMatrix<int64_t>{
            .mem = indices.data(), .M = getNumLoops(), .N = d, .X = d};
    }
    PtrMatrix<int64_t> indexMatrix() const {
        const size_t d = arrayDim();
        return PtrMatrix<int64_t>{
            .mem = indices.data(), .M = getNumLoops(), .N = d, .X = d};
    }
    MutPtrMatrix<int64_t> offsetMatrix() {
        const size_t d = arrayDim();
        const size_t numSymbols = getNumSymbols();
        return MutPtrMatrix<int64_t>{.mem = indices.data() + getNumLoops() * d,
                                     .M = d,
                                     .N = numSymbols,
                                     .X = numSymbols};
    }
    PtrMatrix<int64_t> offsetMatrix() const {
        const size_t d = arrayDim();
        const size_t numSymbols = getNumSymbols();
        return PtrMatrix<int64_t>{.mem = indices.data() + getNumLoops() * d,
                                  .M = d,
                                  .N = numSymbols,
                                  .X = numSymbols};
    }
    ArrayReference(const ArrayReference &a, PtrMatrix<int64_t> newInds)
        : arrayID(a.arrayID), loop(a.loop), strides(a.strides),
          indices(a.indices.size()), hasSymbolicOffsets(a.hasSymbolicOffsets) {
        indexMatrix() = newInds;
    }
    ArrayReference(const ArrayReference &a,
                   llvm::IntrusiveRefCntPtr<AffineLoopNest> loop,
                   PtrMatrix<int64_t> newInds)
        : arrayID(a.arrayID), loop(loop), strides(a.strides),
          indices(a.indices.size()), hasSymbolicOffsets(a.hasSymbolicOffsets) {
        indexMatrix() = newInds;
    }
    ArrayReference(size_t arrayID,
                   llvm::IntrusiveRefCntPtr<AffineLoopNest> loop)
        : arrayID(arrayID), loop(loop){};
    ArrayReference(size_t arrayID, AffineLoopNest &loop)
        : arrayID(arrayID),
          loop(llvm::IntrusiveRefCntPtr<AffineLoopNest>(&loop)){};

    void resize(size_t d) {
        strides.resize(d);
        indices.resize(d * (getNumLoops() + getNumSymbols()));
    }
    ArrayReference(size_t arrayID,
                   llvm::IntrusiveRefCntPtr<AffineLoopNest> loop, size_t dim,
                   bool hasSymbolicOffsets = false)
        : arrayID(arrayID), loop(loop), hasSymbolicOffsets(hasSymbolicOffsets) {
        resize(dim);
    };
    ArrayReference(size_t arrayID, AffineLoopNest &loop, size_t dim,
                   bool hasSymbolicOffsets = false)
        : arrayID(arrayID),
          loop(llvm::IntrusiveRefCntPtr<AffineLoopNest>(&loop)),
          hasSymbolicOffsets(hasSymbolicOffsets) {
        resize(dim);
    };
    bool isLoopIndependent() const { return allZero(indices); }
    bool allConstantIndices() const { return !hasSymbolicOffsets; }
    // Assumes strides and offsets are sorted
    bool stridesMatch(const ArrayReference &x) const {
        if (arrayDim() != x.arrayDim())
            return false;
        for (size_t i = 0; i < arrayDim(); ++i)
            if (strides[i] != x.strides[i])
                return false;
        return true;
    }

    friend std::ostream &operator<<(std::ostream &os,
                                    ArrayReference const &ar) {
        os << "ArrayReference " << ar.arrayID << " (dim = " << ar.arrayDim()
           << "):" << std::endl;
        auto A{ar.indexMatrix()};
        for (size_t i = 0; i < A.numCol(); ++i) {
            auto &stride = ar.strides[i];
            assert(!isZero(stride));
            bool strideIsOne = isOne(stride);
	    if (i)
		os << "+";
            if (!strideIsOne)
                os << stride << " * ( ";
            bool printPlus = false;
            for (size_t j = 0; j < A.numRow(); ++j) {
                if (int64_t Aji = A(j, i)) {
                    if (printPlus) {
                        if (Aji > 0) {
                            os << " + ";
                        } else {
                            Aji *= -1;
                            os << " - ";
                        }
                    }
                    if (Aji != 1)
                        os << Aji << '*';
                    os << "i_" << j << " ";
                    printPlus = true;
                }
            }
            PtrMatrix<int64_t> offs = ar.offsetMatrix();
            for (size_t j = 0; j < offs.numCol(); ++j) {
                if (int64_t offij = offs(i, j)) {
                    if (printPlus) {
                        if (offij > 0) {
                            os << " + ";
                        } else {
                            offij *= -1;
                            os << " - ";
                        }
                    }
                    if (j) {
                        if (offij != 1)
                            os << offij << '*';
                        os << ar.loop->symbols[j - 1];
                    } else {
                        os << offij;
                    }
                    printPlus = true;
                }
            }
            if (!strideIsOne)
                os << " )";
        }
        return os;
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

// std::ostream &operator<<(std::ostream &os, ArrayReferenceFlat const &ar)
// {
//     os << "ArrayReference " << ar.arrayID << ":" << std::endl;
//     for (size_t i = 0; i < length(ar.inds); ++i) {
//         auto [ind, src] = ar.inds[i];
//         os << "(" << ind << ") "
//            << "i_" << src.id << " (" << src.getType() << ")";
//         if (i + 1 < length(ar.inds)) {
//             os << " +";
//         }
//         os << std::endl;
//     }
//     return os;
// }
