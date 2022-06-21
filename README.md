
### LoopModels

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

###### Getting Started

This project requires C++20.
On Ubuntu 22.04 LTS or later (if you're on an older Ubuntu, I suggest upgrading), you can install the dependencies via
```
# needed to build; g++ also works in place of clang
sudo apt install meson clang llvm-dev libgtest-dev libbenchmark-dev pkg-config
# quality of life
sudo apt install clangd clang-format ccache lld
```
I did not start from a clean ubuntu, so some dependencies may be missing.

Then to build and run the test suite, simply run
```
CXX_LD=lld CXXFLAGS="" meson setup builddir -Db_santize=address
cd builddir
time meson test
```
Recompiling and rerunning tests simply requires rerunning `meson test`.

