#pragma once
#if defined(__clang__)
#define MULTIVERSION                                                           \
    __attribute__((target_clones("avx512dq", "avx2", "default")))
#else
#define MULTIVERSION                                                           \
    __attribute__((target_clones(                                              \
        "arch=skylake-avx512,arch=cascadelake,arch=icelake-client,arch="       \
        "tigerlake,"                                                           \
        "arch=icelake-server,arch=sapphirerapids,arch=cooperlake",             \
        "avx2", "default")))
#endif
