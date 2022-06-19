#pragma once

#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Symbolics.hpp"
#include <cstdint>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallVector.h>

// Stride terms are sorted based on VarID
// NOTE: we require all Const sources be folded into the Affine, and their ids
// set identically. thus, `getCount(VarType::Constant)` must always return
// either `0` or `1`.
struct Stride {
    const std::pair<MPoly, MPoly> *strideAndOffset;
    const int64_t *inds;
    const size_t dim;

    inline size_t size() const { return dim; }
    inline auto begin() { return inds; }
    inline auto end() { return inds + dim; }
    inline auto begin() const { return inds; }
    inline auto end() const { return inds + dim; }
    inline auto cbegin() const { return inds + dim; }
    inline auto cend() const { return inds + dim; }
    size_t rank() const {
        size_t r = 0;
        for (size_t i = 0; i < dim; ++i)
            r += (inds[i] != 0);
        return r;
    }
    // int64_t &operator[](size_t i) { return inds[i]; }
    int64_t operator[](size_t i) const { return inds[i]; }
    // llvm::MutableArrayRef<int64_t> indices() {
    //     return llvm::MutableArrayRef{inds, dim};
    // }
    llvm::ArrayRef<int64_t> indices() const {
        return llvm::ArrayRef{inds, dim};
    }
    bool isLoopIndependent() const { return allZero(indices()); }
    // MPoly &stride() { return strideAndOffset->first; }
    // MPoly &offset() { return strideAndOffset->second; }
    const MPoly &stride() const { return strideAndOffset->first; }
    const MPoly &offset() const { return strideAndOffset->second; }
    bool operator==(Stride x) {
        return indices() == x.indices() && stride() == x.stride() &&
               x.offset() == x.offset();
    }
};
struct StrideIterator {
    Stride x;
    StrideIterator operator++() {
        x.strideAndOffset++;
        x.inds += x.dim;
        return *this;
    }
    StrideIterator operator--() {
        x.strideAndOffset--;
        x.inds -= x.dim;
        return *this;
    }
    bool operator==(StrideIterator y) {
        return x.strideAndOffset == y.x.strideAndOffset;
    }
    Stride operator*() { return x; }
};

std::ostream &operator<<(std::ostream &os, Stride const &axis) {
    bool strideIsOne = isOne(axis.stride());
    if (!strideIsOne) {
        os << axis.stride() << " * ( ";
    }
    bool printPlus = false;
    for (size_t i = 0; i < axis.dim; ++i) {
        int64_t c = axis[i];
        if (c) {
            if (printPlus) {
                if (c < 0) {
                    c *= -1;
                    os << " - ";
                } else {
                    os << " + ";
                }
            }
            if (c == 1) {
                os << "i_" << i << " ";
            } else {
                os << c << " * i_" << i << " ";
            }
	    printPlus = true;
        }
    }
    if (!isZero(axis.offset())) {
        if (printPlus) {
            os << " + ";
        }
        os << axis.offset();
    }
    if (!strideIsOne) {
        os << " )";
    }
    return os;
}

// static constexpr unsigned ArrayRefPreAllocSize = 2;

// M*N*i + M*j + i
// [M*N + 1]*i, [M]*j
// M*N
//
// x = i1 * (M*N) + j1 * M + i1 * 1 = i2 * (M*N) + j2 * M + i2 * 1
//
// MN * [ i1 ] = MN * [ i2 ]
// M  * [ j1 ] = M  * [ j2 ]
// 1  * [ i1 ] = M  * [ i2 ]
//
// divrem(x, MN) = (i1, j1 * M + i1) == (i2, j2 * M + i2)
// i1 == i2
// j1 * M + ...

// struct ArrayReferenceFlat {
//     size_t arrayID;
//     std::shared_ptr<AffineLoopNest> loop;
//     llvm::SmallVector<std::pair<MPoly, VarID>, ArrayRefPreAllocSize>
//     inds;
// };

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
struct ArrayReference {
    size_t arrayID;
    llvm::IntrusiveRefCntPtr<AffineLoopNest> loop;
    // std::shared_ptr<AffineLoopNest> loop;
    llvm::SmallVector<std::pair<MPoly, MPoly>> stridesOffsets;
    llvm::SmallVector<int64_t> indices;

    size_t dim() const { return stridesOffsets.size(); }
    size_t getNumLoops() const { return loop->getNumLoops(); }
    PtrMatrix<int64_t> indexMatrix() {
        const size_t numLoops = getNumLoops();
        return {
            .mem = indices.data(), .M = dim(), .N = numLoops, .X = numLoops};
    }
    PtrMatrix<const int64_t> indexMatrix() const {
        const size_t numLoops = getNumLoops();
        return {
            .mem = indices.data(), .M = dim(), .N = numLoops, .X = numLoops};
    }

    ArrayReference(size_t arrayID,
                   llvm::IntrusiveRefCntPtr<AffineLoopNest> loop)
        : arrayID(arrayID), loop(loop){};
    ArrayReference(size_t arrayID, AffineLoopNest &loop)
        : arrayID(arrayID),
          loop(llvm::IntrusiveRefCntPtr<AffineLoopNest>(&loop)){};

    void resize(size_t d) {
        stridesOffsets.resize(d);
        indices.resize(d * getNumLoops());
    }
    ArrayReference(size_t arrayID,
                   llvm::IntrusiveRefCntPtr<AffineLoopNest> loop, size_t dim)
        : arrayID(arrayID), loop(loop) {
        resize(dim);
    };
    ArrayReference(size_t arrayID, AffineLoopNest &loop, size_t dim)
        : arrayID(arrayID),
          loop(llvm::IntrusiveRefCntPtr<AffineLoopNest>(&loop)) {
        resize(dim);
    };
    // StrideIterator begin() {
    //     return StrideIterator{
    //         Stride{stridesOffsets.data(), indices.data(), getNumLoops()}};
    // }
    // StrideIterator end() {
    //     return StrideIterator{Stride{stridesOffsets.end(),
    //                                  indices.data() + indices.size(),
    //                                  getNumLoops()}};
    // }
    StrideIterator begin() const {
        return {stridesOffsets.data(), indices.data(), getNumLoops()};
    }
    StrideIterator end() const {
        return {stridesOffsets.end(), indices.data() + indices.size(),
                getNumLoops()};
    }
    Stride operator[](size_t i) const {
        size_t numLoops = getNumLoops();
        return {stridesOffsets.data() + i, indices.data() + numLoops * i,
                numLoops};
    }
    bool isLoopIndependent() const { return allZero(indices); }
    bool allConstantIndices() const {
        for (auto &so : stridesOffsets) {
            if (!so.first.isCompileTimeConstant())
                return false;
        }
        return true;
    }
    // Assumes stridesOffsets are sorted
    bool stridesMatch(const ArrayReference &x) const {
        if (dim() != x.dim()) {
            return false;
        }
        for (size_t i = 0; i < dim(); ++i) {
            if (stridesOffsets[i].first != x.stridesOffsets[i].first)
                return false;
        }
        return true;
    }
    friend std::ostream &operator<<(std::ostream &os,
                                    ArrayReference const &ar) {
        os << "ArrayReference " << ar.arrayID << " (dim = " << ar.dim()
           << "):" << std::endl;
        for (auto ax : ar) {
            std::cout << ax << std::endl;
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
