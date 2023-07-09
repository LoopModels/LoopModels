
### LoopModels

[![codecov](https://codecov.io/github/JuliaSIMD/LoopModels/branch/main/graph/badge.svg?token=nokmK2kmhT)](https://codecov.io/github/JuliaSIMD/LoopModels)
[![Global Docs](https://img.shields.io/badge/docs-LoopModels-blue.svg)](https://juliasimd.github.io/LoopModels/)

###### Description

LoopModels is intended to be the successor to [LoopVectorization.jl](https://github.com/JuliaSIMD/LoopVectorization.jl) and the [JuliaSIMD](https://github.com/JuliaSIMD/) ecosystem.

It is a work in progress, it will probably be many months before it achieves the level of completeness needed for a working prototype capable of compiling LLVM IR.

Compared to `LoopVectorization.jl`, the initial release of LoopModels will lack support for threading, as well as for non-affine indexing.
However, `LoopModels` will correctly handle dependencies, support arbitrary affine loop nests (e.g. triangular loops and loops with multiple loops at the same level), and (by virtue of working on the LLVM level) will support arbitrary higher level data types.
The goal for the initial release is for naively written operations on small arrays (fits in L2 cache) such as triangular solves and cholesky factorizations will be as close to optimal as can be reasonably achieved on the hardware, at least matching specialized libraries like MKL and OpenBLAS when single threaded over this range.

A longer term goal is also to ensure it works well with Enzyme, so that one can (for example) write simple/naive loops for machine learning or Bayesian models, and then get more or less optimal code for both the forward and reverse passes for gradient-based optimization and sampling algorithms.

Next in the road map will be support automatic cache tiling.
Eventually, threading support is intended.



A high level overview of intended operation:
1. Convert memory accesses from LLVM IR to an internal representation.
2. Use polyhedral methods to analyze dependencies.
3. Search for register tiling oportunties; check legality. Try to apply fixes, if illegal. If we found a legal schedule, jump to `6`.
4. If `3.` fails, run an ILP solver to find a legal schedule, and then.
5. Apply optimizations to all parallelizable, tileable, and permutable hyperplanes.
6. Emit LLVM.

Optimization algorithms (i.e., steps `3.` and `5.`) and code generation will take all the lessons learned from `LoopVectorization.jl`, which boasts impressive performance improvements on many loops (particularly on CPUs with AVX512) vs alternatives, but with the addition of actually performing dependence analysis to check for legality.

To assist with optimizations, `LoopModel`s will be allowed to move blocks ending in `unreachable` earlier. That is, if your code would throw an error, it will still do so, but perhaps at an earlier point. This will, for example, allow hoisting bounds checks out of a loop.
It is expected that in many cases, bounds checks will actually provide information enabling analysis (i.e., delinearization), such that performance will actually be better with bounds checking enabled than disabled (front ends will be able to use `@llvm.assume`s to convey the necessary information if they really want to disable bounds checking).

`LoopModels` will provide a function pass.

Some details and explanations will be provided at [spmd.org](https://spmd.org/).

#### Notes on Code

Eventually, I'd like to make didactic developer docs so that it's a useful resource for anyone wanting to learn about loop optimization and jump into the code to try implementing or improving optimizations.

For now, a few notes on conventions:

####### Loop Order in internal data structures

Loops are always in the `outer <-> inner` order.

For ILP optimization, we take the reverse-lexicographical minimum of the `[dependence distance; schedule]` vector where the schedule is linearly independent of all previously solved schedules. By ordering outer <-> inner, we favor preserving the original program order rather than arbitrarily permuting. 
This is subject to change; I'm likely to have it favor placing loops that index with higher strides outside.

#### Benchmarks

You may first want to install `libpmf`, for example on Fedora
```
sudo dnf install libpfm-devel libpfm-static
```
or on Debian(-based) systems:
```
sudo apt-get install libpfm4-dev
```
`libpmf` is only necessary if you want perf counters.
For example
```sh
CXX=clang++ CXXFLAGS="" cmake -G Ninja -S benchmark buildclang/benchmark -DCMAKE_BUILD_TYPE=Release
cmake --build buildclang/benchmark
buildclang/benchmark/LoopModelsBenchmarks --benchmark_perf_counters=CYCLES,INSTRUCTIONS,CACHE-MISSES

CXX=g++ CXXFLAGS="" cmake -G Ninja -S benchmark buildgcc/benchmark -DCMAKE_BUILD_TYPE=Release
cmake --build buildgcc/benchmark
buildgcc/benchmark/LoopModelsBenchmarks --benchmark_perf_counters=CYCLES,INSTRUCTIONS,CACHE-MISSES
```
Only up to 3 arguments may be passed to `--benchmark_perf_counter` at a time.
Additional options include `BRANCHES`, and architecture-specific event names like you'd use with `perf`.
Some options you can try include:
`cpu-cycles`,`task-clock`,`instructions`,`branch-instructions`,`branch-misses`, `L1-dcache-load-misses`, `L1-dcache-loads`, `cache-misses`, `cache-references`.

Google benchmark calls [pfm_get_os_event_encoding](https://man7.org/linux/man-pages/man3/pfm_get_os_event_encoding.3.html).

`compile_commands.json` generated with [compdb](https://github.com/Sarcasm/compdb):
```sh
compdb -p buildclang/nosan/ list > compile_commands.json
```
