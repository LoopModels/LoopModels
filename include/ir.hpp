#pragma once

#include "./math.hpp"

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

enum SourceType { MEMORY, TERM, CONSTANT, LOOPINDUCTVAR };

enum NumType {
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
};

struct Const {
    NumType type;
    uint64_t bits;
};

void show(Const c) {
    auto b = c.bits;
    switch (c.type) {
    case Float64:
        std::printf("Float64(%f)", double(b));
        break;
    case Float32:
        std::printf("Float32(%f)", float(b));
        break;
    case Float16:
        std::printf("Float16(%x)", uint16_t(b));
        break;
    case BFloat16:
        std::printf("BFloat16(%x)", uint16_t(b));
        break;
    case Int64:
        std::printf("Int64(%zu)", int64_t(b));
        break;
    case Int32:
        std::printf("Int32(%d)", int32_t(b));
        break;
    case Int16:
        std::printf("Int16(%d)", int16_t(b));
        break;
    case Int8:
        std::printf("Int8(%d)", int8_t(b));
        break;
    case UInt64:
        std::printf("UInt64(%lx)", uint64_t(b));
        break;
    case UInt32:
        std::printf("UInt32(%x)", uint32_t(b));
        break;
    case UInt16:
        std::printf("UInt16(%x)", uint16_t(b));
        break;
    case UInt8:
        std::printf("UInt8(%x)", uint8_t(b));
        break;
    default:
        assert("unreachable");
    }
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

1 [ 1 1 0 0   * [i, ik, j, jk]
M   0 0 1 1 ]
// i + ik
// M*(j + jk)
// M = stride(A,2)

Example:
[L, M, K]

perm: identity

1 [ 1 0 0   .* [m,
M   1 0 1       n,
N   0 1 0       k ]
K   0 0 0 ]
# matrix of coefs
# vector of indices into kron space

 matrix  3 x 3
+ lengths [0, 1, 3]

memory: [1, 0, 0, 1]
offsets [0, 0, 1, 4]

mem[offsets[i]:offsets[i+1]-1]
[ [], [1], [0,0,1] ]
[  1,  N,   M*M*N  ]

1     [ 1 -2  3    1   * [i, ik, j, jk]
N       1  8  3    0
M*M*N   1 82 -1204 0 ]

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
// loopnest_to_array_map has length equal to loopnest depth, matches original
//   order. Each value is a bitmask indicating which loops depend on it.

template <typename T> struct VectorOfVectors {
    Vector<T, 0> memory;
    Vector<size_t, 0> offsets;

    VectorOfVectors(Vector<T, 0> memory, Vector<size_t, 0> offsets)
        : memory(memory), offsets(offsets){};
};

template <typename T> size_t length(VectorOfVectors<T> x) {
    return length(x.offsets) - 1;
}
template <typename T> Vector<T, 0> getCol(VectorOfVectors<T> x, size_t i) {
    return subset(x.memory, x.offsets(i), x.offsets(i + 1));
}

// Rows of `R` correspond to strides / program variables, first row is `1`.
// For example, the `ArrayRef` for `B` in
// for (int n = 0; n < N; ++n){
//   for (int m = 0; m < M; ++m){
//     for (int k = 0; k < K; ++k){
//       C(m,n) += A(m,k) * B(k,n);
//     }
//   }
// }
// Would be
// [ 0  0  1     [ n
//   1  0  0    m
//       ]    k ]
struct ArrayRef {
    size_t arrayid;
    Vector<SourceType, 1> ind_typ;                                    // layer0;
    Vector<size_t, 1> ind_id;                                         // layer0;
    Vector<VectorOfVectors<size_t>, 0> program_variable_combinations; // layer1
    VectorOfVectors<Int> coef; // length(coef) == length(pvc), map(length, coef)
                               // == map(length \circ length, pvc)
    Int offset;
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
    Operation op;               // Operation id
    Vector<SourceType, 0> srct; // type of source
    Vector<Int, 0> srcs;        // source id
    Vector<Int, 0> dsts;        // destination id
    uint32_t loopdeps;          // minimal loopdeps based on source's
    Int lnid;                   // id of loopnest
};

/*
bool isadditive(Term t) {
    Operation op = t.op;
    return (op == ADD) | (op == SUB1) | (op == SUB2) | (op == IDENTITY);
}
*/

struct Schedule {
    Int *ptr;
    size_t nloops;
};

size_t getNLoops(Schedule x) { return x.nloops; };

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

struct Function {
    Vector<Term, 0> terms;
    Vector<TriangularLoopNest, 0> triln;
    Vector<RectangularLoopNest, 0> rectln;
    //Vector<Array, 0> arrays;
    Vector<ArrayRef, 0> arrayrefs;
    Vector<Const, 0> constants;
    Vector<bool, 0> visited;
    Vector<Schedule, 0> bestschedules;
    Matrix<Schedule, 0, 0> tempschedules;
    Matrix<double, 0, 0> tempcosts;
    FastCostSummaries fastcostsum;
    Vector<Vector<Int, 0>, 0> triloopcache;
    size_t ne;
    // char *data;

    Function(Vector<Term, 0> terms, Vector<TriangularLoopNest, 0> triln,
             Vector<RectangularLoopNest, 0> rectln, //Vector<Array, 0> arrays,
             Vector<ArrayRef, 0> arrayrefs, Vector<Const, 0> constants,
             Vector<bool, 0> visited, Vector<Schedule, 0> bestschedules,
             Matrix<Schedule, 0, 0> tempschedules,
             Matrix<double, 0, 0> tempcosts, FastCostSummaries fastcostsum,
             Vector<Vector<Int, 0>, 0> triloopcache) // FIXME: triloopcache type
        : terms(terms), triln(triln), rectln(rectln), //arrays(arrays),
          arrayrefs(arrayrefs), constants(constants), visited(visited),
          bestschedules(bestschedules), tempschedules(tempschedules),
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
    }
};

// Array getArray(Function fun, ArrayRef ar) { return fun.arrays(ar.arrayid); }

void clear(Function fun) {
    for (size_t j = 0; j < length(fun.visited); ++j) {
        fun.visited(j) = false;
    }
}
size_t nv(Function fun) { return length(fun.terms); }
size_t ne(Function fun) { return fun.ne; }
Vector<Int, 0> outneighbors(Term t) { return t.dsts; }
Vector<Int, 0> outneighbors(Function fun, size_t i) {
    return outneighbors(fun.terms(i));
}
Vector<Int, 0> inneighbors(Term t) { return t.srcs; }
Vector<Int, 0> inneighbors(Function fun, size_t i) {
    return inneighbors(fun.terms(i));
}

Term &getTerm(Function fun, size_t tidx) { return fun.terms(tidx); }

// Flatten affine term relationships
// struct AffineRelationship{
// };

/*
bool isContiguousTermIndex(Function fun, Term t, Int mlt, size_t level) {
    // SourceType srct0, srct1;
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
    case MEMORY:
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
