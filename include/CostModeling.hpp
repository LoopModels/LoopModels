#pragma once

#include "./LoopBlock.hpp"
#include <llvm/ADT/ArrayRef.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/Support/InstructionCost.h>

// We're likely to change how to represent inputs later.
// It's very unlikely we'll stick with this API.
// It's reasonably likely that we'll use a tree to represent loop fusion
// structure. As fused loops are likely to have the same vectorization/unrolling
// behavior, it would make sense for the tree to hold these, and avoid
// duplication of information vs the fields in the `schedule` struct, as is
// currently the case.
//
// But for now, loopFusion is a vector indicating of integers indicating which
// block to model. For example, [0, 0, 2, 1] means that we're looking at a loop
// nest 4 deep. and that we want to index into the imaginary tree at 0 for the
// outer most loop, 0 for the next to outer most, then 2, and finally 1. For
// example for (i = 0; i < I; ++i){ // outer index 0
//   for (j = 0; j < J; ++j){ // 2nd from outer index 0
//     for (m = 0; m < J; ++m){
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//     }
//     for (m = 0; m < J; ++m){
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//     }
//     for (m = 0; m < J; ++m){ // 3rd from outer index 2
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//       for (n = 0; n < J; ++n){ // 4th from outer index 1
//          // a bunch of math // this is the loop you're looking at
//       }
//     }
//   }
//   for (j = 0; j < J; ++j){
//     for (m = 0; m < J; ++m){
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//     }
//     for (m = 0; m < J; ++m){
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//     }
//   }
// }
// for (i = 0; i < I; ++i){
//   for (j = 0; j < J; ++j){
//     for (m = 0; m < J; ++m){
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//     }
//     for (m = 0; m < J; ++m){
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//     }
//   }
//   for (j = 0; j < J; ++j){
//     for (m = 0; m < J; ++m){
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//     }
//     for (m = 0; m < J; ++m){
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//       for (n = 0; n < J; ++n){
//          // a bunch of math
//       }
//     }
//   }
// }
//
// As a simple example of unrolling an outer loop, say you have
// for (i = 0; i < I; ++i){
//   for (j = 0; j < J; ++j){
//      a = foo(x[i], y[j]);
//      b = bar(z[j]) * a;
//      c = buz(x[i], b, a);
//      w[i] = bloop(c, a - b);
//   }
// }
//
// if we 4x unroll i, we may have something like...
// i = 0
// for (; i < I-3; i+=4){
//   for (j = 0; j < J; ++j){
//      y_j = y[j];
//      a_0 = foo(x[i], y_j);
//      a_1 = foo(x[i+1], y_j);
//      a_2 = foo(x[i+2], y_j);
//      a_3 = foo(x[i+3], y_j);
//      barz = bar(z[j]);
//      b_0 = barz * a_0;
//      b_1 = barz * a_1;
//      b_2 = barz * a_2;
//      b_3 = barz * a_3;
//      c_0 = buz(x[i], b_0, a_0);
//      c_1 = buz(x[i+1], b_1, a_1);
//      c_2 = buz(x[i+2], b_2, a_2);
//      c_3 = buz(x[i+3], b_3, a_3);
//      w[i] = bloop(c_0, a_0 - b_0);
//      w[i+1] = bloop(c_1, a_1 - b_1);
//      w[i+2] = bloop(c_2, a_2 - b_2);
//      w[i+3] = bloop(c_3, a_3 - b_3);
//   }
// }
// for (; i < I; ++i){ //clean up remainder
//   for (j = 0; j < J; ++j){
//      a = foo(x[i], y[j]);
//      b = bar(z[i]) * a;
//      c = buz(x[i], b, a);
//      w[i] = bloop(c, a - b);
//   }
// }
//
// note that we get savings on things like loading from `y[j]` and `z[j]` less
// often, as well as reusing `bar(...)`. The original loop's long dependency
// chain might also limit instruction level parallelism (although in theory
// different loop iterations will often be executed in parallel). Pipelined
// parallelism however will be easy for most CPUs to achieve (even in order
// CPUs!) on the unrolled loop. Note, `foo`, `bar`, `buz`, etc are just example
// functions. Imagine they're trivial things like `+` or `-`. If they're a small
// series of such operations, they should be inlined, so that we can interleave
// the different unrolled iterations.
//
// You might have seen this referred to as "loop stripmining".
llvm::InstructionCost blockThroughput(const LoopBlock &loopBlock,
                                      llvm::TargetLibraryInfo &TTI,
                                      llvm::ArrayRef<unsigned> loopFusion) {

    return llvm::InstructionCost{};
}

// Same inputs (currently) as the above. Determine the optimal unrolling
// factors. LoopVectorization.jl has a relatively simple algorithm:
// https://github.com/JuliaSIMD/LoopVectorization.jl/blob/master/src/modeling/determinestrategy.jl
// Looking at that could serve as a decent starting point if you want to start
// simple, as it does well in many cases. E.g., you can see here for a function
// evaluating the cost for unrolling two loops and picking the optimal unrolling
// factors:
// https://github.com/JuliaSIMD/LoopVectorization.jl/blob/a7f9e1bf91e14fba2c7f65a7c8fbd7e5eeb6f619/src/modeling/determinestrategy.jl#L1141
// This returns optimal unrolling factors as well as an estimatec cost (the
// combination of both the function above and below); you're free to do that as
// well, but I figured it's best to start it's easy enough to combine them if
// you want.
//
// I think we can do substantially better than LoopVectorization's approach. For
// one thing, it's based very heavily on brute force: it iterates over its
// search space, and evaluates the cost. It is also very wasteful: it recomputes
// a great deal every time. So we can do way better than translating.
//
// Computations can also be made more accurate, especially for larger loop
// blocks, e.g. a graph coloring based approach to register allocation to find
// out how many registers are needed. Basically, in the graph of LLVM
// instructions, if two operations depend on each other, they cannot use the
// same register/share the same color. Ideally, you could develop a
// parameterized function that describes register use as a function of unrolling
// factors. Note that re-use requires interleaving. E.g., in register tiling,
// consider matrix multiply
//
// for (m=0; m<M; ++m){
//   for (n=0; n<N; ++n){
//     Cmn = 0;
//     for (k=0; k<K; ++k){
//       Cmn += A(m,k) * B(k,n);
//     }
//     C(m,n) = Cmn;
//   }
// }
//
// we unroll `m` and `n`...
// for (m=0; m<M-1; m += 2){ // unroll `m` by 2
//   for (n=0; n<N-2; n += 3){ // unroll `n` by 3
//     // we have 2*3 C's who consume registers throughout the inner loop block
//     Cmn_0_0 = 0;
//     Cmn_0_1 = 0;
//     Cmn_1_0 = 0;
//     Cmn_1_1 = 0;
//     Cmn_2_0 = 0;
//     Cmn_2_1 = 0;
//     for (k=0; k<K; ++k){
//       // `m` is the "outer unroll" as desbribed in Schedule.hpp
//       // `A_0` and `A_1`'s lifetime/register use extends throughout unrolled
//       // block
//       A_0 = A(m,k);
//       A_1 = A(m+1,k);
//       // B is the inner unroll
//       // B's unrolling is nested inside A's unrolling
//       // B's load's lifetime is limited to one iteration of the inner
//       // unrolling
//       B_0 = B(k,n);
//       Cmn_0_0 += A_0 * B_0;
//       Cmn_0_1 += A_1 * B_0;
//       // B_0's register can be "reused" by B_1's here
//       B_1 = B(k,n+1);
//       Cmn_1_0 += A_0 * B_1;
//       Cmn_1_1 += A_1 * B_1;
//       B_1's register can be "reused" by B_2's here
//       B_2 = B(k,n+1);
//       Cmn_2_0 += A_0 * B_1;
//       Cmn_2_1 += A_1 * B_1;
//       // the maximum number of vector registers used at one time in the inner
//       // loop is
//       // 2*3 for C, 2 for A, and 1 for B
//     }
//     C(m,  n  ) = Cmn_0_0;
//     C(m+1,n  ) = Cmn_0_1;
//     C(m,  n+1) = Cmn_1_0;
//     C(m+1,n+1) = Cmn_1_1;
//     C(m,  n+2) = Cmn_2_0;
//     C(m+1,n+2) = Cmn_2_1;
//   }
// }
//
// You can view the task for modeling register use as defining a function on
// register use when not unrolled, and then considering how register use must
// change as a function of unrolling based on reuse patterns across the unrolls.
// For example, if you have 1 colored graph, what will it have to look like as
// you stich multiple graphs together? Turn that into a function we can
// optimize. We can brute force it if we have to, if that function is fast. Or
// we can improve on brute force when doing 2 loops by iterating on one, and
// doing a bisection to more quickly find the largest legal value of the other
// unrolling factor. Or we can, e.g., use integer relaxation and solve via
// lagrange multipliers, and then test all integers neighboring the floating
// point solutions.
std::pair<int, int> optimalUnrolls(const LoopBlock &loopBlock,
                                   llvm::TargetLibraryInfo &TTI,
                                   llvm::ArrayRef<unsigned> loopFusion) {

    return std::make_pair(-1, -1);
}
