#pragma once
/*
#if defined(NDEBUG) && defined(__x86_64__)
#if defined(__clang__)
#define MULTIVERSION                                                           \
    __attribute__((target_clones("avx512dq", "avx2", "default")))
#define VECTORIZE \
     _Pragma("clang loop vectorize(enable)")	\
     _Pragma("clang loop unroll(disable)")	\
     _Pragma("clang loop vectorize_predicate(enable)")

#else
#define MULTIVERSION                                                           \
     __attribute__((target_clones("arch=x86-64-v4", "arch=x86-64-v3", "default")))
#define VECTORIZE _Pragma("GCC ivdep")
#endif
#else
*/
#define MULTIVERSION
#define VECTORIZE
// #endif

