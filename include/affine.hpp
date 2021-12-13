#pragma once
#include "./math.hpp"
#include <tuple>
#include <cstddef>

// SourceType: RTW/WTR
// size_t 32 bits: src_arrayref_id 8 bits / dst_arrayref_id 8 bits / src_term 16
// bits size_t 64 bits: src_arrayref_id 16 bits / dst_arrayref_id 16 bits /
// src_term 32 bits
enum SourceType { MEMORY, TERM, CONSTANT, LOOPINDUCTVAR, WTR, RTW };

std::string toString(SourceType s) {
    switch (s) {
    case MEMORY:
        return "Memory";
    case TERM:
        return "Term";
    case CONSTANT:
        return "Constant";
    case LOOPINDUCTVAR:
        return "Induction Variable";
    case WTR:
        return "Write then read";
    case RTW: // dummy SourceType for indicating a relationship; not lowered
        return "Read then write";
    // default:
    //     assert("Unreachable reached; invalid SourceType.");
    //     return "";
    }
}

struct AffineSource{
    // SourceType, sourceId, Mlt, Off
    Vector<std::tuple<SourceType,size_t,Int,Int>,0> data;
    Int constOffset;

    std::tuple<SourceType,size_t,Int,Int> &operator()(size_t i){ return data(i); }
};
size_t length(AffineSource a){ return length(a.data); }

// struct AffineSourceVector{
//     SourceType *src;
//     Int *ints;
//     Int *constOffsets;
//     size_t *ncombs;
//     AffineSource operator()(size_t i){
	
//     }
// };




