#pragma once

#include "./math.hpp"

// Associative operations should always be binary.
struct OperationCharacteristics {
    // Vector<OpType,0> optype;
    double heuristic_cost;
};

typedef Int Operation;
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

// Column major array
// Dense indicates that the given axis is known to be contiguous,
// when including previous axis. E.g., in Julia, a `A = Matrix{Float64}(...)`
// would have all dims dense, but a `@view(A[1:4,:])` would be `[true,false]`.
// Basically, this can be used to determs(i)ne whether or not we can collapse
// loops.
struct Array {
    Matrix<bool, 2, 0> dense_knownStride;
    Vector<Int, 0> stride;
};

// array id gives the array
// mlt_offs includes the multipliers (0,:) and offsets (1,:) of the indices.
// I.e., mlts `<2, -5>` and offs `<-5, 8>` corresponds to `A(2*i - 5, -5j + 8)`
// The third index on the first axis of `mlt_off_ids` yields index into the
// appropriate `ind_typ` container.
// ind_typ indicates the type of the index.
// loopnest_to_array_map has length equal to loopnest depth, matches original
//   order. Each value is a bitmask indicating which loops depend on it.
struct ArrayRef {
    Int arrayid;
    Matrix<Int, 3, 0> mlt_off_ids;
    Vector<SourceType, 1> ind_typ;
    Vector<uint32_t, 0> loopnest_to_array_map;
    // Vector<Int, 0> iperm;
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

bool isadditive(Term t) {
    Operation op = t.op;
    return (op == ADD) | (op == SUB1) | (op == SUB2) | (op == IDENTITY);
}

struct TermBundle {
    Vector<Term, 0> terms; // Can we get the topological sort so these are
                           // always contiguous?
};

struct Function {
    Vector<Term, 0> terms;
    Vector<TriangularLoopNest, 0> triln;
    Vector<RectangularLoopNest, 0> rectln;
    Vector<Array, 0> arrays;
    Vector<ArrayRef, 0> arrayrefs;
    Vector<Const, 0> constants;
    Vector<bool, 0> visited;
    size_t ne;
    // char *data;

    Function(Vector<Term, 0> terms, Vector<TriangularLoopNest, 0> triln,
             Vector<RectangularLoopNest, 0> rectln, Vector<Array, 0> arrays,
             Vector<ArrayRef, 0> arrayrefs, Vector<Const, 0> constants,
             Vector<bool, 0> visited)
        : terms(terms), triln(triln), rectln(rectln), arrays(arrays),
          arrayrefs(arrayrefs), constants(constants), visited(visited) {
        size_t edge_count = 0;
        for (size_t j = 0; j < length(terms); ++j)
            edge_count += length(terms(j).dsts);
        ne = edge_count;
    }
};

Array getArray(Function fun, ArrayRef ar) { return fun.arrays(ar.arrayid); }

void clear(Function fun) {
    for (size_t j = 0; j < length(fun.visited); ++j) {
        fun.visited(j) = false;
    }
}
size_t nv(Function fun) { return length(fun.terms); }
size_t ne(Function fun) { return fun.ne; }
Vector<Int, 0> &outneighbors(Term t) { return t.dsts; }
Vector<Int, 0> &outneighbors(Function fun, size_t i) {
    return outneighbors(fun.terms(i));
}
Vector<Int, 0> &inneighbors(Term t) { return t.srcs; }
Vector<Int, 0> &inneighbors(Function fun, size_t i) {
    return inneighbors(fun.terms(i));
}

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
