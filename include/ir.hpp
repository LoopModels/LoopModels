#pragma once

#include "./bitsets.hpp"
#include "./graphs.hpp"
#include "./loops.hpp"
#include "./math.hpp"
#include "./symbolics.hpp"
#include "./tree.hpp"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include <bit>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <llvm/ADT/SmallVector.h>
#include <ostream>
#include <string>
#include <sys/types.h>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

typedef Int Operation;

/*
// Associative operations should always be binary.
struct OperationCharacteristics {
    // Vector<OpType,0> optype;
    double heuristic_cost;
};

const Operation ADD = 0;
const Operation MUL = 1;
const Operation SUB1 = 2;
const Operation SUB2 = 3;
const Operation DIV = 4;
const Operation INV = 5;
const Operation SQRT = 6;
const Operation IDENTITY = 7;

const Int OPERATION_LENGTH = IDENTITY + 1;
OperationCharacteristics opchars[OPERATION_LENGTH] = {
    [ADD] = OperationCharacteristics{.heuristic_cost = 0.5},
    [MUL] = OperationCharacteristics{.heuristic_cost = 0.5},
    [SUB1] = OperationCharacteristics{.heuristic_cost = 0.5},
    [SUB2] = OperationCharacteristics{.heuristic_cost = 0.5},
    [DIV] = OperationCharacteristics{.heuristic_cost = 10.0},
    [INV] = OperationCharacteristics{.heuristic_cost = 10.0},
    [SQRT] = OperationCharacteristics{.heuristic_cost = 10.0},
    [IDENTITY] = OperationCharacteristics{.heuristic_cost = 0.0},
};
*/

struct Const {
    enum {
        Float64,
        Float32,
        Float16,
        BFloat16,
        Int64,
        Int32,
        Int16,
        Int8,
        UInt64,
        UInt32,
        UInt16,
        UInt8
    } NumType;
    union {
        double d;
        float f;
        int64_t i64;
        int32_t i32;
        int16_t i16;
        int8_t i8;
        u_int64_t u64;
        u_int32_t u32;
        u_int16_t u16;
        u_int8_t u8;
    };
};

std::ostream &operator<<(std::ostream &os, Const &c) {
    switch (c.NumType) {
    case Const::Float64:
        os << c.d;
        break;
    case Const::Float32:
        os << c.f;
        break;
    case Const::Int64:
        os << c.i64;
        break;
    case Const::Int32:
        os << c.i32;
        break;
    case Const::Int16:
        os << c.i16;
        break;
    case Const::Int8:
        os << c.i8;
        break;
    case Const::UInt64:
        os << c.u64;
        break;
    case Const::UInt32:
        os << c.u32;
        break;
    case Const::UInt16:
        os << c.u16;
        break;
    case Const::UInt8:
        os << c.u8;
        break;
    case Const::Float16:
        os << c.u16;
        break;
    case Const::BFloat16:
        os << c.u16;
        break;
    }
    return os;
}

// struct Pointer { }

// Column major array
// Dense indicates that the given axis is known to be contiguous,
// when including previous axis. E.g., in Julia, a `A = Matrix{Float64}(...)`
// would have all dims dense, but a `@view(A[1:4,:])` would be `[true,false]`.
// Basically, this can be used to determs(i)ne whether or not we can collapse
// loops.

/*
M x N x whatever
i,j,k
[ L, M, N ] means:
L, M, N -> L*(i + M*(j + N*(k)))

while [1, 1, 1]
1*(i + 1*(j + 1*(k))) = i + j + k



for j in J, i in I // I = J = 1:100
    tmp = zero(eltype(out))
    for jk in JK, ik in IK // IK = JK = 1:3
        tmp += A[i + ik, j + jk] * kern[ik, jk]
    end
    out[i,j] = tmp
end

i vectorized,
ik = 0, ik = 1
[ v0
  v1   [ v1
  v2     v2
  v3 ]   v3
         v4 ]
i vectorized
j =  Q, j  = Q-1
jk = 0, jk = 1
j+jk=Q, j+jk=Q
j_0 , j_1 , j_2 , j_3
jk_0, jk_0, jk_0, jk_0
jk_1, jk_1, jk_1, jk_1
jk_2, jk_2, jk_2, jk_2
// corresponding sums:
0,    1,    2,    3
1,    2,    3,    4
2,    3,    4,    5


// permutation layer, comes in next
//

L*i + M*j + N*k

A = rand(2,3,4,5)
B = view(A,1,:,:,:)

1 [ 0 1 1 0 0   * [1, i, ik, j, jk]
M   0 0 0 1 1 ]
// i + ik
// M*(j + jk)
// M = stride(A,2)

Example:
[L, M, K]

perm: identity

1 [ 0 1 0 0   .* [1,
M   0 1 0 1       m,
N   0 0 1 0       n,
K   0 0 0 0 ]     k ]
# matrix of coefs
# vector of indices into kron space

 matrix  3 x 3
+ lengths [0, 1, 3]

memory: [1, 0, 0, 1]
offsets [0, 0, 1, 4]

mem[offsets[i]:offsets[i+1]-1]
[ [], [1], [0,0,1] ]
[  1,  N,   M*M*N  ]

1     [ 0  1 -2  3    1   * [1, i, ik, j, jk]
N       0  1  8  3    0
M*M*N   0  1 82 -1204 0 ]

1
M
N
K
M*M
M*N
M*K
M*M*M
M*N*M
M*K*M
M*M*N
M*N*N
M*K*N
M*M*K
M*N*K
M*K*K

1 m
M m + k
N n
K 0

results in:
L*([m+m*M] + M*(n*N + K*(k*M)))


offset + m + m*M + n*N + k*M

32 x 32
1 [ 1 32 ] * [m, n]

Layer 1, we store
offset
for each variable
  kron_inds, coefficient vector

Layer 0:
variables:
  - variable type, e.g. loop induct var, term, mem
  - variable ind into appropriate type container

// loop induct var index is w/ respect to associated loopnest
//

1 [ 1 0
J   0 1 ] * [i^2, j]


J = stride(x,2)

for i in I, j in J
    x[i^2, j] // term: i^2
    x[inds[i]] // mem: inds[i]
end

*/

// array id gives the array
// mlt_offs includes the multipliers (0,:) and offsets (1,:) of the indices.
// I.e., mlts `<2, -5>` and offs `<-5, 8>` corresponds to `A(2*i - 5, -5j + 8)`
// The third index on the first axis of `mlt_off_ids` yields index into the
// appropriate `ind_typ` container.
// ind_typ indicates the type of the index.
// loopnest_to_array_map has length equal to loopnest depth, shouldPrefusees
// original
//   order. Each value is a bitmask indicating which loops depend on it.

template <typename T> struct VoV {
    Vector<T, 0> memory;
    Vector<size_t, 0> offsets;

    VoV(Vector<T, 0> memory, Vector<size_t, 0> offsets)
        : memory(memory), offsets(offsets){};

    Vector<T, 0> operator()(size_t i) {
        return subset(memory, offsets(i), offsets(i + 1));
    }
};

template <typename T> size_t length(VoV<T> x) { return length(x.offsets) - 1; }

template <typename T,
          unsigned N =
              llvm::CalculateSmallVectorDefaultInlinedElements<T>::value>
struct VoVoV {
    llvm::SmallVector<T, N> memory;
    llvm::SmallVector<uint32_t, N> innerOffsets;
    llvm::SmallVector<uint32_t, N> outerOffsets;
    llvm::SmallVector<uint32_t, N> memOffsets;
    // Vector<size_t, 0> innerOffsets;
    // Vector<size_t, 0> outerOffsets;
    // Vector<size_t, 0> memOffsets;

    // memOffsets is preallocated but uninitialized memory
    // length(memOffsets) = length(outerOffsets)
    VoVoV(T *memory, Vector<size_t, 0> innerOffsets,
          Vector<size_t, 0> outerOffsets, Vector<size_t, 0> memBuffer)
        : memory(memory), innerOffsets(innerOffsets),
          outerOffsets(outerOffsets), memOffsets(memBuffer) {
        size_t i = 0;
        memOffsets(0) = 0;
        for (size_t j = 1; j < length(outerOffsets); ++j) {
            size_t last_idx = outerOffsets(j);
            if (last_idx > 0) {
                i += innerOffsets(last_idx - 1);
                memOffsets(j) = i;
            }
        }
    }

    VoV<T> operator()(size_t i) {
        size_t base = memOffsets(i);
        Vector<T, 0> newMem(memory + base, memOffsets(i + 1) - base);
        Vector<T, 0> offsets =
            subset(innerOffsets, outerOffsets(i), outerOffsets(i + 1));
        return VoV<T>(newMem, offsets);
    }
};

template <typename T> size_t length(VoVoV<T> x) {
    return length(x.outerOffsets) - 1;
}

// Rows of `R` correspond to strides / program variables, first row is `1`.
// For example, the `ArrayRef` for `B` in
// for (int n = 0; n < N; ++n){
//   for (int m = 0; m < M; ++m){
//     for (int k = 0; k < K; ++k){
//       C(m,n) += A(m,k) * B(k+n,n);
//     }
//   }
// }
// Would be
// [1] [ 0  1  0  1    [ 1
// [M]   0  1  0  0 ]    n
//                       m
//                       k ]
//
// Corresponding to k + n + n*M;
//
// Representation:
// [ ]       []       // ind 0, corresponds to `1`
// [[],[M]] [1, 1]   // ind 1, corresponds to `n`
// [ ]       []       // ind 2, corresponds to `m`
// [[]]     [1]      // ind 3, corresponds to `k`
//
// Memory representation of coef:
// memory:  [1, 1, 1]
// offsets: [0, 0, 2, 2, 3]
//
// Memory representation of programVariableCombinations:
// Program variables inside `Function fun`: [M]
// Constant 1:
// memory: []
// offset: []
// i_1, VarType: Induction Variable
// memory: [1]
// offset: [0,0,1]
// i_2, VarType: Induction Variable
// memory: []
// offset: []
// i_3, VarType: Induction Variable
// memory: []
// offset: [0,0]

// Gives the part of an ArrayRef that is a function of the induction variables.

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

template <typename T0, typename T1, typename T2>
std::pair<T1, T2> tail(std::tuple<T0, T1, T2> &x) {
    return std::make_pair(std::get<1>(x), std::get<2>(x));
}
template <typename T0, typename T1, typename T2>
std::pair<T1, T2> tail(std::tuple<T0, T1, T2> &&x) {
    return std::make_pair(std::get<1>(x), std::get<2>(x));
}

template <typename T> std::pair<size_t, size_t> findMaxLength(VoVoV<T> x) {
    size_t j = 0;
    size_t v = 0;
    for (size_t i = 0; i < length(x); ++i) {
        size_t l = length(x(i));
        if (l > v) {
            j = i;
            v = l;
        }
    }
    return std::make_pair(j, v);
}

std::ostream &operator<<(std::ostream &os, ArrayRef const &ar) {
    os << "ArrayRef " << ar.arrayID << ":" << std::endl;
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

struct CostSummary {
    double vCost;
    double sCost;
    // double vLoadCost;
    // double sLoadCost;
    // double vStoreCost;
    // double sStoreCost;

    CostSummary()
        : vCost(0.0), sCost(0.0){}; //, vLoadCost(0.0), sLoadCost(0.0),
                                    // vStoreCost(0.0), sStoreCost(0.0) {};
    void operator+=(CostSummary cs) {
        vCost += cs.vCost;
        sCost += cs.sCost;
    }
};
//
// An instriction is a compute operation like '+', '*', '/', '<<', '&', ...
// These will typically map to a single CPU instruction.
//   - What about something like exp or log?
//   - Current thought: we do want to be able to, but also support
//     various transforms into alternate instruction sequences.
//
//
// What this needs to do:
// - Show dependencies on other operations.
// - (For convenience) destination operations.
// - Indicate
struct Term {
    Operation op; // Operation id
    size_t id;
    CostSummary costSummary;
    Vector<std::pair<size_t, VarType>, 0> srcs; // type of source
    Vector<std::pair<size_t, VarType>, 0> dsts; // destination id
    // Vector<size_t, 0> srcs;       // source id
    size_t loopNestId; // id of loopnest
    uint32_t loopDeps; // minimal loopdeps based on source's
};

std::pair<size_t, size_t> getLoopId(Term t) {
    size_t loopNestId = t.loopNestId;
    return std::make_pair(zeroUpper(loopNestId), zeroLower(loopNestId));
}

/*
bool isadditive(Term t) {
    Operation op = t.op;
    return (op == ADD) | (op == SUB1) | (op == SUB2) | (op == IDENTITY);
}
*/
// Columns are levels in the loop nest, rows correspond to term groups.
// At each level, the matrix value indexes into which loop it corresponds to.
// [ 0 0 0 0
//   0 1 0 0
//   0 0 0 0
//   0 1 1 0
//   0 1 2 0 ]
// For example, level 0, all 5 term groups have an index of 0, meaning they
// are fused into the same group. At level 1, term groups 1 and 3 have index 0,
// while the remaining term groups have index 1. Thus, term groups 1 and 3
// are fused into the first loop, and the remaining terms are fused into the
// second loop.
// Trivally, all fused together in all four loops:
// [ 0 0 0 0
//   0 0 0 0
//   0 0 0 0 ]
// Trivially, all immediately split:
// [ 0 0 0 0
//   1 0 0 0
//   2 0 0 0 ]
/*
struct FusionTree {
    Matrix<Int, 0, 0> tree;
};
struct Schedule {
    Int *ptr;
    const size_t numTermGs; // number of rows in mathrix
    const size_t numLoops;  // number of columns in matrix
    double cost;
};

size_t getNLoops(Schedule x) { return x.numLoops; };

FusionTree fusionMatrix(Schedule s) {
    return FusionTree{Matrix<Int, 0, 0>(s.ptr, s.numTermGs, s.numLoops)};
}

Permutation getPermutation(Schedule s, size_t i) {
    Int offset = s.numTermGs * s.numLoops;
    Int twoNumLoops = 2 * s.numLoops;
    offset += i * (twoNumLoops + 1);
    Int *permPtr = s.ptr + offset;
    return Permutation(permPtr, *(permPtr + twoNumLoops));
};

Int schedule_size(Schedule s) { return s.numTermGs * (3 * s.numLoops + 1); };
*/

/* // commented out because this is broken
size_t countScheduled(Schedule s, Int segment, Int level) {
    size_t c = 0;
    Vector<Int, 0> v = getCol(fusionMatrix(s).tree, level);
    for (size_t i = 0; i < length(v); ++i)
        c += (v(i) == segment);
    return c;
}
*/
struct Schedule {
    IndexTree tree;
    llvm::SmallVector<Permutation> perms;
};

// Assumption: Does not support more than 32 loops.
struct FastCostSummary {
    double scalar; // Scalar cost
    double vector; // Vector cost
    uint32_t msk0; // First dim loop deps
    uint32_t msk1; // Second dim loop deps
    uint32_t msk2; // Loop deps
};

// dimensions term
typedef Vector<FastCostSummary, 0> FastCostSummaries;

constexpr Int UNSET_COST = -1;
// Bits: UnknownSign(000), NoSign(001), Positive(010), NonNegative(011),
// Negative(100), NonPositive(101) enum Sign { UnknownSign, NoSign, Positive,
// NonNegative, Negative, NonPositive };

// Bits: InvalidOrder(000), EqualTo(001), LessThan(010), LessOrEqual(011),
// GreaterThan(100), GreaterOrEqual(101), NotEqual(110), UnknownOrder(111) Bits:
// EqualTo(000), LessThan(001), Greater, LessOrEqual(00), LessOrEqual(011),
// GreaterThan(100), GreaterOrEqual(101) enum Order { UnknownOrder, EqualTo,
// LessThan, LessOrEqual, GreaterThan, GreaterOrEqual };

struct Function {
    llvm::SmallVector<Term> terms;
    llvm::SmallVector<TriangularLoopNest, 0> triln;
    llvm::SmallVector<RectangularLoopNest, 0> rectln;
    // Vector<Array, 0> arrays;
    // Vector<ArrayRefStrides, 0> arrayRefStrides;
    llvm::SmallVector<ArrayRef, 0> arrayRefs;
    llvm::SmallVector<Const> constants;
    llvm::SmallVector<bool> visited;
    IndexTree initialLoopTree;
    // Vector<Schedule, 0> bestschedules;
    // Matrix<Schedule, 0, 0> tempschedules;
    Matrix<double, 0, 0> tempcosts;
    FastCostSummaries fastcostsum;
    Vector<Vector<Int, 0>, 0> triloopcache;
    llvm::SmallVector<llvm::SmallVector<std::pair<size_t, size_t>>>
        arrayReadsToTermMap;
    llvm::SmallVector<llvm::SmallVector<std::pair<size_t, size_t>>>
        arrayWritesToTermMap;
    llvm::SmallVector<ValueRange>
        rangeMap; // vector of length num_program_variables
    llvm::SmallVector<llvm::SmallVector<ValueRange>>
        diffMap; // comparisons, e.g. x - y via orderMap[x][y]; maybe we don't
                 // know anything about `x` or `y` individually, but do know `x
                 // - y > 0`.
    size_t ne;
    // char *data;

    /*
      // Makes more sense to initialize empty and take builder approach.
    Function(Vector<Term, 0> terms, Vector<TriangularLoopNest, 0> triln,
             Vector<RectangularLoopNest, 0> rectln, // Vector<Array, 0> arrays,
             // Vector<ArrayRefStrides, 0> arrayRefStrides,
             Vector<ArrayRef, 0> arrayRefs, Vector<Const, 0> constants,
             Vector<bool, 0> visited, IndexTree initialLoopTree,
             // Vector<Schedule, 0> bestschedules,
             // Matrix<Schedule, 0, 0> tempschedules,
             Matrix<double, 0, 0> tempcosts, FastCostSummaries fastcostsum,
             Vector<Vector<Int, 0>, 0> triloopcache,
             size_t numArrays) // FIXME: triloopcache type
        : terms(terms), triln(triln),
          rectln(rectln), // arrays(arrays),
                          // arrayRefStrides(arrayRefStrides),
          arrayRefs(arrayRefs), constants(constants), visited(visited),
          initialLoopTree(initialLoopTree),
          // bestschedules(bestschedules),
          // tempschedules(tempschedules),
          tempcosts(tempcosts), fastcostsum(fastcostsum),
          triloopcache(triloopcache) {
        size_t edge_count = 0;
        for (size_t j = 0; j < length(terms); ++j)
            edge_count += length(terms(j).dsts);
        ne = edge_count;
        for (size_t j = 0; j < length(triloopcache); ++j) {
            Vector<Int, 0> trlc = triloopcache(j);
            for (size_t k = 0; k < length(trlc); ++k) {
                trlc(k) = UNSET_COST;
            }
        }
        arrayReadsToTermMap.resize(numArrays);
        arrayWritesToTermMap.resize(numArrays);
    }
    */
};

ValueRange valueRange(Function const &fun, size_t id) {
    return fun.rangeMap[id];
}
template <typename C>
ValueRange
valueRange(Function const &fun,
           Polynomial::MultivariateTerm<C, Polynomial::Monomial> const &x) {
    ValueRange p = ValueRange(x.coefficient);
    for (auto it = x.cbegin(); it != x.cend(); ++it) {
        p *= fun.rangeMap[*it];
    }
    return p;
}
template <typename C>
ValueRange
valueRange(Function const &fun,
           Polynomial::Multivariate<C, Polynomial::Monomial> const &x) {
    ValueRange a(0);
    for (auto it = x.cbegin(); it != x.cend(); ++it) {
        a += valueRange(fun, *it);
    }
    return a;
}

Order cmpZero(intptr_t x) {
    return x ? (x > 0 ? GreaterThan : LessThan) : EqualTo;
}
Order cmpZero(Function &fun, size_t id) {
    return valueRange(fun, id).compare(0);
}
template <typename C>
Order cmpZero(Function &fun,
              Polynomial::MultivariateTerm<C, Polynomial::Monomial> &x) {
    return valueRange(fun, x).compare(0);
};

// std::pair<ArrayRef, ArrayRefStrides> getArrayRef(Function fun, size_t id) {
//     ArrayRef ar = fun.arrayRefs[id];
//     ArrayRefStrides ars = fun.arrayRefStrides[ar.strideId];
//     return std::make_pair(ar, ars);
// };
inline ArrayRef &getArrayRef(Function &fun, size_t id) {
    return fun.arrayRefs[id];
};

UpperBounds &getUpperBounds(Function &fun, ArrayRef &ar) {
    size_t triID = upperHalf(ar.arrayID);
    if (triID) {
        return fun.triln[triID].getUpperbounds();
    } else {
        return fun.rectln[ar.arrayID].getUpperbounds();
    }
}
UpperBounds &fillUpperBounds(Function &fun, ArrayRef &ar) {
    size_t triID = upperHalf(ar.arrayID);
    if (triID) {
        TriangularLoopNest &tri = fun.triln[triID];
        tri.fillUpperBounds();
        return tri.getUpperbounds();
    } else {
        return fun.rectln[ar.arrayID].getUpperbounds();
    }
}

void partitionArrayStrides(Function &fun, ArrayRef &ar) {
    // NOTE: this will be the firstg call, so we fill ehre.
    UpperBounds &upperBounds = fillUpperBounds(fun, ar);
    size_t numInds = ar.inds.size();
    /*
    llvm::SmallVector<uint32_t> indDegrees(numInds, uint32_t(0));

    for (size_t i = 0; i < numInds; ++i){
        indDegrees[i] = std::get<0>(ar.inds[i]).degree();
    }
    */
    size_t maxDegreeInd = 0;
    size_t maxDegree = 0;
    for (size_t i = 0; i < numInds; ++i) {
        size_t d = std::get<0>(ar.inds[i]).degree();
        if (d > maxDegree) {
            maxDegree = d;
            maxDegreeInd = i;
        }
    }
}

// Array getArray(Function fun, ArrayRef ar) { return fun.arrays(ar.arrayid); }

void clearVisited(Function &fun) {
    for (size_t j = 0; j < length(fun.visited); ++j) {
        fun.visited[j] = false;
    }
}
bool visited(Function &fun, size_t i) { return fun.visited[i]; }
size_t nv(Function &fun) { return length(fun.terms); }
size_t ne(Function &fun) { return fun.ne; }
Vector<std::pair<size_t, VarType>, 0> outNeighbors(Term &t) {
    return t.dsts;
}
Vector<std::pair<size_t, VarType>, 0> outNeighbors(Function &fun, size_t i) {
    return outNeighbors(fun.terms[i]);
}
Vector<std::pair<size_t, VarType>, 0> inNeighbors(Term &t) { return t.srcs; }
Vector<std::pair<size_t, VarType>, 0> inNeighbors(Function &fun, size_t i) {
    return inNeighbors(fun.terms[i]);
}

Term &getTerm(Function &fun, size_t tidx) { return fun.terms[tidx]; }

struct TermBundle {
    // llvm::SmallVector<size_t> termIDs;
    BitSet termIds;
    BitSet srcTerms;
    BitSet dstTerms;
    BitSet srcTermsDirect;
    BitSet dstTermsDirect;
    BitSet loads;  // arrayRef ids
    BitSet stores; // arrayRef ids
    // llvm::SmallVector<CostSummary> costSummary; // vector of
    // length(numLoopDeps);
    BitSet srcTermBundles; // termBundles
    BitSet dstTermBundles; // termBundles

    llvm::SmallSet<size_t, 4> RTWs;
    llvm::SmallSet<size_t, 4> WTRs;
    CostSummary costSummary;
    TermBundle(size_t maxTerms, size_t maxArrayRefs, size_t maxTermBundles)
        : termIds(BitSet(maxTerms)), srcTerms(BitSet(maxTerms)),
          dstTerms(BitSet(maxTerms)), srcTermsDirect(BitSet(maxTerms)),
          dstTermsDirect(BitSet(maxTerms)), loads(BitSet(maxArrayRefs)),
          stores(BitSet(maxArrayRefs)), srcTermBundles(BitSet(maxTermBundles)),
          dstTermBundles(BitSet(maxTermBundles)) {}
};

// inline uint32_t lowerQuarter(uint32_t x) { return x & 0x000000ff; }
// inline uint64_t lowerQuarter(uint64_t x) { return x & 0x000000000000ffff; }
// inline uint32_t upperHalf(uint32_t x) { return x & 0xffff0000; }
// inline uint64_t upperHalf(uint64_t x) { return x & 0xffffffff00000000; }

void push(TermBundle &tb, llvm::SmallVector<size_t> &termToTermBundle,
          Function &fun, size_t idx, size_t tbId) {
    termToTermBundle[idx] = tbId;
    Term t = fun.terms[idx];
    push(tb.termIds, idx);
    // MEMORY, TERM, CONSTANT, LOOPINDUCTVAR, WTR, RTW
    // Here, we fill out srcTerms and dstTerms; only later once all Term <->
    // TermBundle mappings are known do we fill srcTermbundles/dstTermbundles
    for (size_t i = 0; i < length(t.srcs); ++i) {
        auto [srcId, srcTyp] = t.srcs[i];
        switch (srcTyp) {
        case VarType::Memory:
            push(tb.loads, srcId);
            break;
        case VarType::Term:
            push(tb.srcTerms, srcId);
            push(tb.srcTermsDirect, srcId);
            break;
        // case WTR: // this is the read, so write is src
        //     push(tb.srcTerms, lowerHalf(srcId));
        //     push(tb.loads, secondQuarter(srcId));
        //     tb.WTRs.insert(srcId);
        //     break;
        // case RTW: // this is the read, so write is dst
        //     push(tb.dstTerms, lowerHalf(srcId));
        //     push(tb.loads, firstQuarter(srcId));
        //     // tb.RTWs.insert(srcId);
        default:
            break;
        }
    }
    for (size_t i = 0; i < length(t.dsts); ++i) {
        auto [dstId, dstTyp] = t.dsts[i];
        switch (dstTyp) {
        case VarType::Memory:
            push(tb.stores, dstId);
            break;
        case VarType::Term:
            push(tb.dstTerms, dstId);
            push(tb.dstTermsDirect, dstId);
            break;
        // case WTR: // this is the write, so read is dst
        //     push(tb.dstTerms, lowerHalf(dstId));
        //     push(tb.stores, firstQuarter(dstId));
        //     break;
        // case RTW: // this is the write, so read is src
        //     push(tb.srcTerms, lowerHalf(dstId));
        //     push(tb.stores, secondQuarter(dstId));
        //     tb.RTWs.insert(dstId);
        //     break;
        default:
            break;
        }
    }
    tb.costSummary += t.costSummary;
}

void fillDependencies(TermBundle &tb, size_t tbId,
                      llvm::SmallVector<size_t> &termToTermBundle) {
    for (auto I = tb.srcTerms.begin(); I != tb.srcTerms.end(); ++I) {
        size_t srcId = termToTermBundle[*I];
        if (srcId != tbId)
            push(tb.srcTermBundles, srcId);
    }
    for (auto I = tb.dstTerms.begin(); I != tb.dstTerms.end(); ++I) {
        size_t dstId = termToTermBundle[*I];
        if (dstId != tbId)
            push(tb.dstTermBundles, dstId);
    }
}

BitSet &outNeighbors(TermBundle &tb) { return tb.dstTermBundles; }
BitSet &inNeighbors(TermBundle &tb) { return tb.srcTermBundles; }

// for `shouldPrefuse` to work well, calls should try to roughly follow
// topological order as best as they can. This is because we rely on inclusion
// of `Term t` in either the `srcTermsDirect` or `dstTermsDirect. Depending on
// the order we iterate over the graph, it may be that even though we ultimately
// append `t` into `srcTermsDirect` or `dstTermsDirect`, it is not yet included
// at the time we call `shouldPrefuse`.
//
// Note that `shouldPrefuse` is used in `prefuse` as an optimization meant to
// speed up this library by reducing the search space; correctness does not
// depend on it.
bool shouldPrefuse(Function &fun, TermBundle &tb, size_t tid) {
    Term &t = fun.terms[tid];
    size_t members = length(tb.termIds);
    if (members) {
        Term &firstMember = fun.terms[tb.termIds[0]];
        if (firstMember.loopNestId != t.loopNestId)
            return false;
        return contains(tb.srcTermsDirect, tid) |
               contains(tb.dstTermsDirect, tid);
    }
    return true;
}

struct TermBundleGraph {
    llvm::SmallVector<TermBundle, 0> tbs;
    llvm::SmallVector<size_t>
        termToTermBundle; // mapping of `Term` to `TermBundle`.
    // llvm::SmallVector<llvm::SmallVector<bool>> visited;

    TermBundleGraph(Function &fun, llvm::SmallVector<Int> &wcc)
        : termToTermBundle(llvm::SmallVector<size_t>(length(fun.terms))) {
        // iterate over the weakly connected component wcc
        // it should be roughly topologically sorted,
        // so just iterate over terms, greedily checking whether each
        // shouldPrefusees the most recent `TermBundle`. If so, add them to the
        // previous. If not, allocate a new one and add them to it. Upon
        // finishing, construct the TermBundleGraph from this collection of
        // `TermBundle`s, using a `termToTermBundle` map.
        size_t maxTerms = length(fun.terms);
        size_t maxArrayRefs = length(fun.arrayRefs);
        size_t maxTermBundles = length(wcc);
        for (size_t i = 0; i < wcc.size(); ++i) {
            bool doPrefuse = i > 0;
            size_t idx = wcc[i];
            if (doPrefuse)
                doPrefuse = shouldPrefuse(fun, last(tbs), idx);
            if (!doPrefuse)
                tbs.emplace_back(
                    TermBundle(maxTerms, maxArrayRefs, maxTermBundles));
            push(last(tbs), termToTermBundle, fun, idx, tbs.size() - 1);
        }
        // Now, all members of the wcc have been added.
        // Thus, `termToTerBmundle` should be full, and we can now fill out the
        // `srcTermBundles` and `dstTermBundles`.
        for (size_t i = 0; i < tbs.size(); ++i) {
            fillDependencies(tbs[i], i, termToTermBundle);
        }
    }
};

struct WeaklyConnectedComponentOptimizer {
    TermBundleGraph tbg;
    // Schedule bestSchedule;
    // Schedule tempSchedule;
    llvm::SmallVector<llvm::SmallVector<Int>>
        stronglyConnectedComponents; // strongly connected components within the
                                     // weakly connected component
};

BitSet &outNeighbors(TermBundleGraph &tbg, size_t tbId) {
    TermBundle &tb = tbg.tbs[tbId];
    return outNeighbors(tb);
}
BitSet &inNeighbors(TermBundleGraph &tbg, size_t tbId) {
    TermBundle &tb = tbg.tbs[tbId];
    return inNeighbors(tb);
}

// returns true if `abs(x) < y`
template <typename C>
bool absLess(Function const &fun,
             Polynomial::Multivariate<C, Polynomial::Monomial> const &x,
             Polynomial::Multivariate<C, Polynomial::Monomial> const &y) {
    ValueRange delta = valueRange(fun, y - x); // if true, delta.lowerBound >= 0
    if (delta.lowerBound < 0.0) {
        return false;
    }
    ValueRange sum = valueRange(fun, y + x); // if true, sum.lowerBound >= 0
    // e.g. `x = [M]; y = [M]`
    // `delta = 0`, `sum = 2M`
    return sum.lowerBound >= 0.0;
}

// should now be able to WCC |> prefuse |> SCC.

/*
void clearVisited(TermBundleGraph &tbg, size_t level) {
    llvm::SmallVector<bool> &visited = tbg.visited[level];
    for (size_t i = 0; i < visited.size(); ++i) {
        visited[i] = false;
    }
    return;
}
void clearVisited(TermBundleGraph &tbg) { clearVisited(tbg, 0); }
bool visited(TermBundleGraph &tbg, size_t i, size_t level) {
    llvm::SmallVector<bool> &visited = tbg.visited[level];
    return visited[i];
}
bool visited(TermBundleGraph &tbg, size_t i) { return visited(tbg, i, 0); }

void markVisited(TermBundleGraph &tbg, size_t tb, size_t level) {
    llvm::SmallVector<bool> &visited = tbg.visited[level];
    visited[tb] = true;
    return;
}

bool allSourcesVisited(TermBundleGraph &tbg, size_t tbId, size_t level) {
    llvm::SmallVector<bool> &visited = tbg.visited[level];
    // TermBundle &tb = tbg.tbs[tbId];
    llvm::SmallVector<size_t> &srcs = inNeighbors(tbg, tbId);
    bool allVisited = true;
    for (size_t i = 0; i < srcs.size(); ++i) {
        allVisited &= visited[srcs[i]];
    }
    return allVisited;
}

// returns set of all outNeighbors that are covered
llvm::SmallVector<size_t> getIndexSet(TermBundleGraph &tbg, size_t node,
                                size_t level) {
    llvm::SmallVector<size_t> &dsts = outNeighbors(tbg, node);
    llvm::SmallVector<size_t> indexSet; // = tbg.indexSets[level];
    // indexSet.clear();
    for (size_t i = 0; i < dsts.size(); ++i) {
        size_t dstId = dsts[i];
        if (allSourcesVisited(tbg, dstId, level))
            indexSet.push_back(dstId);
    }
    return indexSet;
}

VarType sourceType(TermBundleGraph &tbg, size_t srcId, size_t dstId) {
    TermBundle &dst = tbg.tbs[dstId];
    BitSet& srcV = inNeighbors(dst);
    for (size_t i = 0; i < length(srcV); ++i) {
        if (srcV[i] == srcId) {
            return dst.srcTyp[i];
        }
    }
    assert("source not found");
    return TERM;
}
*/

// Will probably handle this differently, i.e. check source type, and then
// only call given .
/*
uint32_t compatibleLoops(Function &fun, TermBundleGraph &tbg, size_t srcId,
size_t dstId, size_t level){ VarType srcTyp = sourceType(tbg, srcId, dstId);
    switch (srcTyp) {
    case TERM:
        // return same loop as srcId
        return ;
    case RBW/WBR:
        // rotation is possible, so return vector of possiblities
        return compatibleLoops();
    default:
        assert("invalid src type");
    }
}
*/

uint32_t getLoopDeps(Function &fun, TermBundle &tb) {
    Term t = getTerm(fun, tb.termIds[0]);
    return t.loopDeps;
}

// for i in 1:I, j in 1:J;
//   s = 0.0
//   for ik in 1:3, jk in 1:3
//      s += A[i + ik, j + jk] * kern[ik,jk];
//   end
//   out[i,j] = s
// end
//
// for i in 1:I, j in 1:J
//    out[i,j] = x[i,j] + x[i,j-1] + x[i,j+1];
// end
//
// i + M*j     = i + M*j
// i + M*(j-1) = i + M*j - M
// i + M*(j+1) = i + M*j + M
//
// across the three above, we have x = -1, 0, 1
// 1 [ 0  1  0  [ 1
// M   x  0  1    i
//                j ]
//
//
// we have multiple terms with memory references to the same array (`x`)
// with the smae arrayid, indTyp, indID, programVariableCombinations
// We are checking for
// 1. different offsets, and different
// 2. first rows of coef.
//

/*
ArrayRef getArrayRef(Function fun, TermBundle tb) {
    Term t = getTerm(fun, tb.termIDs[0]);
    return fun.arrayrefs[];
}
*/

// Flatten affine term relationships
// struct AffineRelationship{
// };

/*
bool isContiguousTermIndex(Function fun, Term t, Int mlt, size_t level) {
    // VarType srct0, srct1;
    while (true) {
        switch (t.op) {
        case ADD:
            for (size_t i = 0; i < 2; i++) {
                switch (t.srct(i)) {
                case MEMORY:
                    ArrayRef ar = fun.arrayrefs(t.src(i));
                    if (ar.loopnest_to_array_map(level) != 0)
                        return false;
                    break;
                case TERM:

                    break;
                case CONSTANT:

                    break;
                case LOOPINDUCTVAR:

                    break;
                default:
                    assert("unreachable");
                }
            }
            srct1 = t.srct[1];
            break;
        case SUB1:
            t = getsrc(t, 0);
            mlt *= -1;
            break;
        case SUB2:
            break;
        case IDENTITY:
            t = getsrc(t, 0);
            break;
        default:
            return false;
        }
    }
}

bool isContiguousReference(Function fun, ArrayRef ar, Array a, size_t level) {
    switch (ar.ind_typ[0]) {
    case LOOPINDUCTVAR:
        // contiguous requires:
        // stride mlt of 1, first dim dense
        return (ar.mlt_off_ids(0, 0) == 1) & (a.dense_knownStride(0, 0));
    Case MEMORY:
        return false;
    case TERM:
        // Here, we need to parse terms
        Term t = getterm(fun, ar, 0);
        Int mlt = ar.mlt_off_ids(0, 0);
        return isContiguousTermIndex(fun, t, mlt, level);
    case CONSTANT:
        return false;
    default:
        assert("unreachable");
    }
}
bool isContiguousReference(Function fun, ArrayRef ar, size_t i) {
    // loop index `i` must map only to first index
    if (ar.loopnest_to_array_map[i] != 0x00000001)
        &&return false;
    return isContiguousReference(fun, ar, getArray(fun, ar));
}

//
size_t memoryCost(Function fun, ArrayRef ar, size_t v, size_t u1, size_t u2) {
    Array a = getArray(fun, ar);
    // __builtin_ctz(n)
    if (isContiguousReference(ar, a, v)) {

    } else {
    }
}
*/
