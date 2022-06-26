
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
sudo apt install meson clang llvm-dev libgtest-dev libbenchmark-dev ninja-build pkg-config cmake
# quality of life
sudo apt install clangd clang-format ccache lld
```
On Fedora 36:
```
sudo dnf install meson clang llvm-devel gtest-devel google-benchmark-devel ninja-build pkgconf cmake
sudo dnf install clang-tools-extra ccache lld libasan
```
I did not start from a clean ubuntu or fedora, so some dependencies may be missing.




Then to build and run the test suite, simply run
```
CC_LD=lld CXX_LD=lld CXXFLAGS="" meson setup builddir -Db_santize=address
cd builddir
time meson test
```
Recompiling and rerunning tests simply requires rerunning `meson test`.
The address sanitizer works for me on Fedora, but not Ubuntu (it has linking errors on Ubuntu, not unsanitary addresses ;) ), so you can remove it if it gives you trouble. Or find out how to actually get it working on Ubuntu and let me know.

If you chose a directory name other than `builddir`, you may want to update the symbolically linked file `compile_commands.json`, as `clangd` will in your editor will likely be looking for this (and use it for example to find your header files).

Benchmarks can be run via `meson test benchmarks`, which isn't that useful as it benchmarks the benchmark scripts. `meson`'s benchmark support seems ideal for macro benchmarks, which this project doesn't currently have.
This repository currently only has a few micro benchmarks making use of [google benchmark](https://github.com/google/benchmark), which I should probably change to no longer mark as benchmarks w/ respect to `meson`, but as separate targets.
These can be run via (or optionally `meson compile` to build all targets).
```
meson compile polynomial_benchmark constraint_pruning_benchmark
./polynomial_benchmark
./constraint_pruning_benchmark
```

###### No Root
If you don't have root, or are using an operating system with package managers less wieldy than manual package management...
Make sure you've defined the environmental variables on Linux:
```
export PATH=$HOME/.local/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/lib/x86_64-unknown-linux-gnu/:$HOME/.local/lib:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH
export C_INCLUDE_PATH=$HOME/.local/include:$C_INCLUDE_PATH
export CPLUS_INCLUDE_PATH=$HOME/.local/include:$CPLUS_INCLUDE_PATH
```
Or on MacOS:
```
export SDKROOT=$(xcrun --show-sdk-path)
export PATH=$HOME/.local/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/lib/x86_64-unknown-linux-gnu/:$HOME/.local/lib:$LD_LIBRARY_PATH
export PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig:$PKG_CONFIG_PATH
export C_INCLUDE_PATH=$HOME/.local/include:$C_INCLUDE_PATH
export CPLUS_INCLUDE_PATH=$HOME/.local/include/c++/v1:$HOME/.local/include:$CPLUS_INCLUDE_PATH
```

You should probably place these in a script you can easily source it whenever you're developing LoopModels. Alternatively, place this in your `~/.bashrc` or equivalent.
These paths will let the compiler and linker find the new LLVM tool chain.

```
curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py
python3 get-pip.py
python3 -m pip install meson --user
rm get-pip.py
mkdir -p $HOME/Documents/libraries
cd $HOME/Documents/libraries
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
git checkout release/14.x
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE="Release" -DLLVM_USE_SPLIT_DWARF=ON -DLLVM_BUILD_LLVM_DYLIB=ON -DLLVM_ENABLE_PROJECTS="mlir;clang;lld;clang-tools-extra" -DLLVM_TARGETS_TO_BUILD="host" -DBUILD_SHARED_LIBS=ON -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DLLVM_PARALLEL_LINK_JOBS=1 -DLLVM_OPTIMIZED_TABLEGEN=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind;compiler-rt" ../llvm
time ninja
ninja install
```
You've now build a new enough toolchain that the project can use, both for linking with (LoopModels depends on LLVM >= 14) and for compiling the project (LoopModels uses C++20). 
The project and all its dependencies will have to be built with and link to this toolchain, so it's important to set `CXXFLAGS="-stdlib=libc++"` below.

When building LLVM, if you have a lot of RAM, you can remove the option `-DLLVM_PARALLEL_LINK_JOBS=1` to allow parallel linking. If your RAM is limited, the OOM Killer is likely to hit your build.

If you're on MacOS, remove the `*_LD`s, as `lld` won't work. Or you could try replacing `lld` with `ld64.lld`. The default linker on Linux is slow, which is why I'm using the `lld` we build with llvm below.
```
cd $HOME/Documents/libraries
git clone https://github.com/google/benchmark.git
cd benchmark
cmake -E make_directory "build"
CC_LD=lld CXX_LD=lld CXXFLAGS="-stdlib=libc++" CC=clang CXX=clang++ cmake -E chdir "build" cmake -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DCMAKE_BUILD_TYPE=Release ../
CC_LD=lld CXX_LD=lld CXXFLAGS="-stdlib=libc++" CC=clang CXX=clang++ cmake --build "build" --config Release --target install

cd $HOME/Documents/libraries
git clone https://github.com/google/googletest.git
cd googletest
cmake -E make_directory "build"
CC_LD=lld CXX_LD=lld CXXFLAGS="-stdlib=libc++" CC=clang CXX=clang++ cmake -E chdir "build" cmake -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DCMAKE_BUILD_TYPE=Release ../
CC_LD=lld CXX_LD=lld CXXFLAGS="-stdlib=libc++" CC=clang CXX=clang++ cmake --build "build" --config Release --target install
```
Now that all our dependencies are built, we can finally build `LoopModels` itself. It of course also requires `libc++`.
```
cd $HOME/Documents/libraries
git clone https://github.com/JuliaSIMD/LoopModels.git
cd LoopModels
CC_LD=lld CXX_LD=lld CXXFLAGS="-stdlib=libc++" CC=clang CXX=clang++ meson setup builddir
cd builddir
meson test
```

Now that this is all set up, you just need to make sure the environmental variables are defined, and can just reinvoke `meson test` and `meson compile` to build the test suite/project as needed.

If you need to wipe the build dir, you'll have to set the temporary environment variables such as the linkers and CXX flags again.

