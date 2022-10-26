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
     __attribute__((target_clones("arch=x86-64-v4", "arch=x86-64-v3",
"default"))) #define VECTORIZE _Pragma("GCC ivdep") #endif #else
*/
#define MULTIVERSION
#define VECTORIZE
// #define NOVECTORIZE
// #endif

#if defined(__clang__)
#define NOVECTORIZE                                                            \
    _Pragma("clang loop vectorize(disable)")                                   \
        _Pragma("clang loop unroll(disable)")
#else
#define NOVECTORIZE
#endif

#define SHOW(ex) llvm::errs() << #ex << " = " << ex;
#define CSHOW(ex) llvm::errs() << "; " << #ex << " = " << ex;
#define SHOWLN(ex) llvm::errs() << #ex << " = " << ex << "\n";
#define CSHOWLN(ex) llvm::errs() << "; " << #ex << " = " << ex << "\n";
