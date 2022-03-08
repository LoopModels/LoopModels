#pragma once

#include "./ArrayReference.hpp"
#include "./BitSets.hpp"
#include "./Graphs.hpp"
#include "./Loops.hpp"
#include "./Math.hpp"
#include "./Symbolics.hpp"
#include <bit>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Support/InstructionCost.h>
#include <ostream>
#include <string>
#include <sys/types.h>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

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
    std::variant<llvm::BasicBlock *, llvm::Loop *, llvm::Intrinsic::ID,
                 llvm::Instruction *, llvm::Function *>
        op;
    size_t id;
    llvm::InstructionCost latency;
    llvm::InstructionCost recipThroughput;
    llvm::SmallVector<std::pair<size_t, VarType>, 3> srcs; // type of source
    llvm::SmallVector<std::pair<size_t, VarType>, 3> dsts; // destination id
    // Vector<size_t, 0> srcs;       // source id
    size_t loopNestId; // id of loopnest
    uint32_t loopDeps; // minimal loopdeps based on source's

    Term(llvm::Loop *LP, size_t loopNestId)
        : op(LP), loopNestId(loopNestId),
          loopDeps(std::numeric_limits<uint32_t>::max()) {
        // TODO: walk to add all srcs and dsts to the sources and destinations
    }
};

std::pair<size_t, size_t> getLoopId(Term t) {
    size_t loopNestId = t.loopNestId;
    return std::make_pair(zeroUpper(loopNestId), zeroLower(loopNestId));
}

// Assumes columns 0...j-1 are linearly independent
// while column `j` has just been appended.
// We check if it is dependent on the previous columns. 
size_t addLinearlyIndependentCol(SquareMatrix<intptr_t> &A, size_t j){
    size_t M = A.size(0);
    if (j == 0){
	// we confirm that at least one row is != 0
	for (size_t m = 0; m < M; ++m){
	    if (A(m,0)){
		return 1;
	    }
	}
	// if all zero, rank == 0 < 1
	return 0;
    }
    // first `jxj` block is diagonal
    // Step 1: zero out A(0:j-1,j)
    for (size_t i = 0; i < j; ++i){
	if (intptr_t Aij = A(i,j)){
	    // we must zero it
	    intptr_t Aii = A(i,i);
	    // use gcd to control growth
	    intptr_t g = std::gcd(Aii, Aij);
	    Aii /= g;
	    Aij /= g;
	    // A(:,j) = A(:,j)*Aii - A(:,i)*Aij
	    A(i,j) = 0;
	    for (size_t m = j; m < M; ++m) {
		A(m,j) = A(m,j)*Aii - A(m,i)*Aij;
	    }
	}
    }
    // Step 2: search for a row pivot
    size_t pivot = j;
    while (A(pivot,j) == 0){
	++pivot;
        if (pivot == M) {
	    // linearly dependent
            return j;
        }
    }
    swapRows(A, pivot, j);
    intptr_t Ajj = A(j,j);
    // zero out A(j,0:j-1)
    for (size_t i = 0; i < j; ++i) {
	if (intptr_t Aji = A(j,i)){
	    // we must zero it
	    // use gcd to control growth
	    intptr_t g = std::gcd(Ajj, Aji);
	    intptr_t Ajjg = Ajj / g;
	    intptr_t Ajig = Aji / g;
	    // A(:,i) = A(:,i)*Ajj - A(:,j)*Aji
	    A(j,i) = 0;
	    for (size_t m = j+1; m < M; ++m){
		A(m,i) = A(m,i)*Ajjg - A(m,j)*Ajig;
	    }
	}
    }
    return j+1;
}
// `B` is a transposed mirror in reduced form
// it is used to check whether a new row is linearly independent.
size_t addUniqueIndRow(SquareMatrix<intptr_t> &A, SquareMatrix<intptr_t> &B, const Stride &axis, size_t j){
    for (auto &a : axis){
        const MPoly &m = a.first;
        VarID v = a.second;
	if (v.getType() == VarType::LoopInductionVariable){
            llvm::Optional<intptr_t> c = m.getCompileTimeConstant();
            if (c.hasValue()){
		
	    }
	}
    }
    return j;
}

llvm::Optional<AffineLoopNestPerm>
orthogonalize(AffineLoopNestPerm &aln, llvm::SmallVectorImpl<ArrayReference *> &ai) {
    // need to construct matrix `A` of relationship
    // B*L = I
    // where L are the loop induct variables, and I are the array indices
    // e.g., if we have `C[i + j, j]`, then
    // B = [1 1; 0 1]
    // additionally, the loop is defined by the bounds
    // A*L = A*(B\^-1 * I) <= r
    // assuming that `B` is an invertible integer matrix,
    // which we can check via `lufact(B)`, and confirming that
    // the determinant == 1 or determinant == -1.
    // If so, we can then use the lufactorizationm for computing
    // A/B, to get loop bounds in terms of the indexes.
    //
    size_t numLoops = aln.getNumLoops();
    SquareMatrix<intptr_t> A(numLoops);
    SquareMatrix<intptr_t> B(numLoops);
    for (size_t i = 0; i < numLoops*numLoops; ++i){
	A[i] = 0;
    }
    size_t j = 0;
    for (auto a : ai){
	for (auto &axis : (*a)){
            j = addUniqueIndRow(A, axis, j);
	}
    }
    return llvm::Optional<AffineLoopNestPerm>();
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
/*
struct Schedule {
    IndexTree tree;
    llvm::SmallVector<Permutation> perms;
};
*/
// Assumption: Does not support more than 32 loops.
/*
struct FastCostSummary {
    double scalar; // Scalar cost
    double vector; // Vector cost
    uint32_t msk0; // First dim loop deps
    uint32_t msk1; // Second dim loop deps
    uint32_t msk2; // Loop deps
};

// dimensions term
typedef Vector<FastCostSummary, 0> FastCostSummaries;
*/
constexpr Int UNSET_COST = -1;
// Bits: UnknownSign(000), NoSign(001), Positive(010), NonNegative(011),
// Negative(100), NonPositive(101) enum Sign { UnknownSign, NoSign, Positive,
// NonNegative, Negative, NonPositive };

// Bits: InvalidOrder(000), EqualTo(001), LessThan(010), LessOrEqual(011),
// GreaterThan(100), GreaterOrEqual(101), NotEqual(110), UnknownOrder(111) Bits:
// EqualTo(000), LessThan(001), Greater, LessOrEqual(00), LessOrEqual(011),
// GreaterThan(100), GreaterOrEqual(101) enum Order { UnknownOrder, EqualTo,
// LessThan, LessOrEqual, GreaterThan, GreaterOrEqual };
