#pragma once

#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Symbolics.hpp"
#include <cstdint>
#include <memory>

// Stride terms are sorted based on VarID
// NOTE: we require all Const sources be folded into the Affine, and their ids
// set identically. thus, `getCount(VarType::Constant)` must always return
// either `0` or `1`.
struct Stride {
    MPoly stride;
    // sources must be ordered
    llvm::SmallVector<std::pair<MPoly, VarID>, 1> indices;
    uint8_t counts[5];
    // size_t constCount;
    // size_t indCount;
    // size_t memCount;
    // size_t termCount;
    Stride() //= default;
        :    // : indices(llvm:n:SmallVector<
          // std::pair<Polynomial::Multivariate<intptr_t>, VarID>>()),
          counts{0, 0, 0, 0, 0} {};
    Stride(MPoly const &x) : stride(x), counts{0, 0, 0, 0, 0} {};
    Stride(MPoly &&x) : stride(std::move(x)), counts{0, 0, 0, 0, 0} {};
    Stride(MPoly const &x, VarID ind)
        : indices({std::make_pair(x, ind)}), counts{0, 0, 0, 0, 0} {};
    Stride(MPoly &x, uint32_t indId, VarType indTyp)
        : indices({std::make_pair(x, VarID(indId, indTyp))}), counts{0, 0, 0, 0,
                                                                     0} {};
    Stride(MPoly stride, llvm::SmallVector<std::pair<MPoly, VarID>, 1> indices)
        : stride(std::move(stride)),
          indices(std::move(indices)), counts{0, 0, 0, 0, 0} {};
    Stride(Polynomial::Monomial stride,
           llvm::SmallVector<std::pair<MPoly, VarID>, 1> indices)
        : stride(MPoly{Polynomial::Term{intptr_t(1), std::move(stride)}}),
          indices(std::move(indices)), counts{0, 0, 0, 0, 0} {};
    size_t size() const { return indices.size(); }
    size_t getCount(VarType i) {
        return counts[size_t(i) + 1] - counts[size_t(i)];
    }
    size_t getCount(VarID i) { return getCount(i.getType()); }
    inline auto begin() { return indices.begin(); }
    inline auto end() { return indices.end(); }
    inline auto begin() const { return indices.begin(); }
    inline auto end() const { return indices.end(); }
    inline auto cbegin() const { return indices.begin(); }
    inline auto cend() const { return indices.end(); }
    inline auto begin(VarType i) { return indices.begin() + counts[size_t(i)]; }
    inline auto end(VarType i) {
        return indices.begin() + counts[size_t(i) + 1];
    }
    inline auto begin(VarID i) { return begin(i.getType()); }
    inline auto end(VarID i) { return end(i.getType()); }
    inline auto begin(VarType i) const {
        return indices.begin() + counts[size_t(i)];
    }
    inline auto end(VarType i) const {
        return indices.begin() + counts[size_t(i) + 1];
    }
    inline auto begin(VarID i) const { return begin(i.getType()); }
    inline auto end(VarID i) const { return end(i.getType()); }
    inline size_t numIndices() const { return indices.size(); }
    auto &operator[](size_t i) { return indices[i]; }
    auto &operator[](size_t i) const { return indices[i]; }
    // MPoly indToPoly() const {
    //     if (indices.size()) {
    //         auto I = indices.begin();
    //         MPoly p = (I->first) * (Polynomial::Monomial(I->second));
    // 	    ++I;
    // 	    for (; I != indices.end(); ++I){
    // 		p +=
    // 	    }
    //         return p;
    //     } else {
    //         return {};
    //     }
    // }
    bool allConstantIndices() const {
        for (auto &&ind : indices) {
            if (!ind.first.isCompileTimeConstant()) {
                return false;
            }
        }
        return true;
    }
    void addTyp(VarType t) {
        // Clang goes extremely overboard vectorizing loops with dynamic length
        // even if it should be statically inferrable that num iterations <= 4
        // thus, static length + masking is preferable.
        // GCC also seems to prefer this.
        // https://godbolt.org/z/Pcxd6jre7
        for (size_t i = 0; i < 4; ++i) {
            counts[i + 1] += (size_t(t) <= i);
        }
    }
    void remTyp(VarType t) {
        for (size_t i = 0; i < 4; ++i) {
            counts[i + 1] -= (size_t(t) <= i);
        }
    }
    void addTyp(VarID t) { addTyp(t.getType()); }
    void remTyp(VarID t) { remTyp(t.getType()); }

    template <typename A, typename I> void addTerm(A &&x, I &&ind) {
        auto ite = end();
        for (auto it = begin(ind); it != ite; ++it) {
            if (ind == (*it).second) {
                (it->first) += x;
                if (isZero((it->first))) {
                    remTyp(ind);
                    indices.erase(it);
                }
                return;
            } else if (ind < (it->second)) {
                addTyp(ind);
                indices.insert(it, std::make_pair(std::forward<A>(x),
                                                  std::forward<I>(ind)));
                return;
            }
        }
        addTyp(ind);
        indices.push_back(
            std::make_pair(std::forward<A>(x), std::forward<I>(ind)));
        return;
    }
    template <typename A, typename I> void subTerm(A &&x, I &&ind) {
        auto ite = end();
        for (auto it = begin(ind); it != ite; ++it) {
            if (ind == (it->second)) {
                (it->first) -= x;
                if (isZero((it->first))) {
                    remTyp(ind);
                    indices.erase(it);
                }
                return;
            } else if (ind < (it->second)) {
                addTyp(ind);
                indices.insert(it, std::make_pair(cnegate(std::forward<A>(x)),
                                                  std::forward<I>(ind)));
                return;
            }
        }
        addTyp(ind);
        indices.push_back(
            std::make_pair(cnegate(std::forward<A>(x)), std::forward<I>(ind)));
        return;
    }

    Stride &operator+=(Stride const &x) {
        for (auto it = x.cbegin(); it != x.cend(); ++it) {
            addTerm(std::get<0>(*it), std::get<1>(*it));
        }
        return *this;
    }
    Stride &operator-=(Stride const &x) {
        for (auto it = x.cbegin(); it != x.cend(); ++it) {
            subTerm(std::get<0>(*it), std::get<1>(*it));
        }
        return *this;
    }
    Stride largerCapacityCopy(size_t i) const {
        Stride s(stride);
        s.indices.reserve(i + indices.size()); // reserve full size
        for (auto &ind : indices) {
            s.indices.push_back(ind); // copy initial batch
        }
        // for (size_t i = 1; i < 5; ++i) {
        //     s.counts[i] = counts[i];
        // }
        return s;
    }

    Stride operator+(Stride const &x) const {
        Stride y = largerCapacityCopy(x.indices.size());
        y += x;
        return y;
    }
    Stride operator+(Stride &&x) const { return x += *this; }
    Stride operator-(Stride const &x) const {
        // don't increase capcity, in hopes of terms cancelling out.
        // Stride y = largerCapacityCopy(x.indices.size());
        Stride y = *this;
        y -= x;
        return y;
    }
    Stride &negBang() {
        for (auto ind : indices) {
            ind.first.negate();
        }
        return *this;
    }
    Stride neg() const {
        Stride y = *this;
        return y.negBang();
    }
    bool operator==(Stride const &x) const {
        return (stride == x.stride) && (indices == x.indices);
    }
    bool operator!=(Stride const &x) const {
        return (stride != x.stride) || (indices != x.indices);
    }

    bool isConstant() const {
        size_t n0 = indices.size();
        if (n0) {
            return indices[n0 - 1].second.getType() == VarType::Constant;
        } else {
            return true;
        }
    }
    // // takes advantage of sorting
    // bool isAffine() const { return counts[2] == counts[4]; }

    // std::pair<Polynomial,DivRemainder> tryDiv(Polynomial &a){
    //	auto [s, v] = gcd(a.terms);

    // }

    // bool operator>=(Polynomial x){

    //	return false;
    // }
};

std::ostream &operator<<(std::ostream &os, Stride const &axis) {
    bool strideIsOne = isOne(axis.stride);
    if (!strideIsOne) {
        os << axis.stride << " * (";
    }
    bool printPlus = false;
    for (auto &indvar : axis) {
        auto &[mlt, var] = indvar;
        if (auto optc = mlt.getCompileTimeConstant()) {
            intptr_t c = optc.getValue();
            if (printPlus) {
                if (c < 0) {
                    c *= -1;
                    os << " - ";
                } else {
                    os << " + ";
                }
            }
            if (c == 1) {
                os << "{ " << var << " }";
            } else {
                os << c << "* { " << var << " }";
            }
        } else {
            if (printPlus) {
                os << " + ";
            }
            os << mlt << "* { " << var << " }";
        }
        printPlus = true;
    }
    if (!strideIsOne) {
        os << ")\n";
    }
    return os;
}

/*
SourceCount sourceCount(Stride s) {
    SourceCount x;
    for (auto it = s.stride.begin(); it != s.stride.end(); ++it) {
        x += (*it).second;
    }
    return x;
}
*/

static constexpr unsigned ArrayRefPreAllocSize = 2;

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

struct ArrayReferenceFlat {
    size_t arrayID;
    AffineLoopNest *loop;
    llvm::SmallVector<std::pair<MPoly, VarID>, ArrayRefPreAllocSize> inds;
};
struct ArrayReference {
    size_t arrayID;
    std::shared_ptr<AffineLoopNest> loop;
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> axes;
    llvm::SmallVector<uint32_t, ArrayRefPreAllocSize> indToStrideMap;
    size_t dim() const { return axes.size(); }
    ArrayReference(size_t arrayID, std::shared_ptr<AffineLoopNest> loop)
        : arrayID(arrayID), loop(loop){};
    ArrayReference(size_t arrayID, std::shared_ptr<AffineLoopNest> loop,
                   llvm::SmallVector<Stride, ArrayRefPreAllocSize> axes)
        : arrayID(arrayID), loop(loop), axes(axes) {
        // TODO: fill indToStrideMap;
    }
    void pushAffineAxis(const MPoly &stride, const StridedVector<intptr_t> &s) {
        // uint32_t m = 0;
        if (s.size() >= indToStrideMap.size()) {
            indToStrideMap.resize(s.size());
        }
        uint32_t m = uint32_t(1) << axes.size();
        axes.emplace_back(stride);
        llvm::SmallVector<std::pair<MPoly, VarID>, 1> &inds =
            axes.back().indices;
        for (IDType i = 0; i < s.size(); ++i) {
            if (intptr_t c = s[i]) {
                inds.emplace_back(c, VarID(i, VarType::LoopInductionVariable));
                assert(inds.back().first.getCompileTimeConstant().getValue() ==
                       c);
                indToStrideMap[i] |= m;
            }
        }
    }
    auto begin() { return axes.begin(); }
    auto end() { return axes.end(); }
    auto begin() const { return axes.begin(); }
    auto end() const { return axes.end(); }
    bool allConstantStrides() const {
        for (auto &axis : axes) {
            if (!axis.allConstantIndices()) {
                return false;
            }
        }
        return true;
    }
    bool stridesMatch(const ArrayReference &x) const {
        if (dim() != x.dim()) {
            return false;
        }
        for (size_t i = 0; i < dim(); ++i) {
            const Stride &ys = axes[i];
            const Stride &xs = x.axes[i];
            if (ys.stride != xs.stride) {
                return false;
            }
        }
        return true;
    }
    bool stridesMatchAllConstant(const ArrayReference &x) const {
        if (dim() != x.dim()) {
            return false;
        }
        for (size_t i = 0; i < dim(); ++i) {
            const Stride &ys = axes[i];
            const Stride &xs = x.axes[i];
            if (!((ys.stride == xs.stride) && xs.allConstantIndices() &&
                  ys.allConstantIndices())) {
                return false;
            }
        }
        return true;
    }
    friend std::ostream &operator<<(std::ostream &os,
                                    ArrayReference const &ar) {
        os << "ArrayReference " << ar.arrayID << " (dim = " << ar.axes.size()
           << "):" << std::endl;
        for (auto &ax : ar) {
            std::cout << ax << std::endl;
        }
        return os;
    }
};

std::ostream &operator<<(std::ostream &os, ArrayReferenceFlat const &ar) {
    os << "ArrayReference " << ar.arrayID << ":" << std::endl;
    for (size_t i = 0; i < length(ar.inds); ++i) {
        auto [ind, src] = ar.inds[i];
        os << "(" << ind << ") "
           << "i_" << src.id << " (" << src.getType() << ")";
        if (i + 1 < length(ar.inds)) {
            os << " +";
        }
        os << std::endl;
    }
    return os;
}
