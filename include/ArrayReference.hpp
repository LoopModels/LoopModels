#pragma once

#include "./Math.hpp"
#include "./Symbolics.hpp"

// Stride terms are sorted based on VarID
// NOTE: we require all Const sources be folded into the Affine, and their ids
// set identically. thus, `getCount(VarType::Constant)` must always return
// either `0` or `1`.
struct Stride {
    Polynomial::Multivariate<intptr_t, Polynomial::Monomial> stride;
    // sources must be ordered
    llvm::SmallVector<
        std::pair<Polynomial::Multivariate<intptr_t, Polynomial::Monomial>,
                  VarID>,
        1>
        indices;
    uint8_t counts[5];
    // size_t constCount;
    // size_t indCount;
    // size_t memCount;
    // size_t termCount;
    Stride() //= default;
        :    // : indices(llvm:n:SmallVector<
          // std::pair<Polynomial::Multivariate<intptr_t>, VarID>>()),
          counts{0, 0, 0, 0, 0} {};
    Stride(Polynomial::Multivariate<intptr_t, Polynomial::Monomial> const &x)
        : stride(x), counts{0, 0, 0, 0, 0} {};
    Stride(Polynomial::Multivariate<intptr_t, Polynomial::Monomial> &&x)
        : stride(std::move(x)), counts{0, 0, 0, 0, 0} {};
    Stride(Polynomial::Multivariate<intptr_t, Polynomial::Monomial> const &x,
           VarID ind)
        : indices({std::make_pair(x, ind)}), counts{0, 0, 0, 0, 0} {};
    Stride(Polynomial::Multivariate<intptr_t, Polynomial::Monomial> &x,
           uint32_t indId, VarType indTyp)
        : indices({std::make_pair(x, VarID(indId, indTyp))}), counts{0, 0, 0, 0,
                                                                     0} {};

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
        Stride y = largerCapacityCopy(x.indices.size());
        y -= x;
        return y;
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
    // 	auto [s, v] = gcd(a.terms);

    // }

    // bool operator>=(Polynomial x){

    // 	return false;
    // }
};
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

struct ArrayRef {
    size_t arrayID;
    llvm::SmallVector<
        std::pair<Polynomial::Multivariate<intptr_t, Polynomial::Monomial>,
                  VarID>,
        ArrayRefPreAllocSize>
        inds;
    llvm::SmallVector<Stride, ArrayRefPreAllocSize> axes;
    llvm::SmallVector<uint32_t, ArrayRefPreAllocSize> indToStrideMap;
};
