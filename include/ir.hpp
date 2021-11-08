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

const Int OPERATION_LENGTH = SQRT + 1;
OperationCharacteristics opchars[OPERATION_LENGTH] = {
    [ADD] = OperationCharacteristics{.heuristic_cost = 0.5},
    [MUL] = OperationCharacteristics{.heuristic_cost = 0.5},
    [SUB1] = OperationCharacteristics{.heuristic_cost = 0.5},
    [SUB2] = OperationCharacteristics{.heuristic_cost = 0.5},
    [DIV] = OperationCharacteristics{.heuristic_cost = 10.0},
    [INV] = OperationCharacteristics{.heuristic_cost = 10.0},
    [SQRT] = OperationCharacteristics{.heuristic_cost = 10.0},
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
// Basically, this can be used to determine whether or not we can collapse
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
struct ArrayRef {
    Int arrayid;
    Matrix<Int, 3, 0> mlt_off_ids;
    Vector<SourceType, 1> ind_typ;
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
    Vector<bool, 0> loopdeps;   // minimal loopdeps based on source's
    Int lnid;                   // id of loopnest
};

struct Program {
    Vector<Term, 0> terms;
    Vector<TriangularLoopNest, 0> triln;
    Vector<RectangularLoopNest, 0> rectln;
    Vector<Array, 0> arrays;
    Vector<ArrayRef, 0> arrayrefs;
    Vector<Const, 0> constants;
};
