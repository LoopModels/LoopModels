#include <gtest/gtest.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

#ifndef USE_MODULE

#include "Math/Array.cxx"
#include "Math/AxisTypes.cxx"
#include "Optimize/CacheOptimization.cxx"
#include "Optimize/LoopTransform.cxx"
#include "Target/Machine.cxx"
#include "TestUtilities.cxx"
#include <array>
#include <cstdint>
#include <cstdio>

#else

import Array;
import ArrayParse;
import CacheModel;
import Constraints;
import IR;
import LoopTransform;
import OStream;
import STL;
import TargetMachine;
import TestUtilities;
import Valid;

#endif

using math::DenseDims, math::MutArray;

TEST(CacheOptimization, BasicAssertions) {
  target::Machine<false> skx{{target::MachineCore::SkylakeServer}};

  TestLoopFunction tlf;
  auto &builder = tlf.getBuilder();
  llvm::Type *f64 = builder.getDoubleTy();
  // int bytes = int(f64->getPrimitiveSizeInBits() >> 3);
  std::array<double, 3> phi_costs{0, 0, 24 * 9 * skx.getLoadStowCycles(f64)};

  using DS = CostModeling::Cache::CacheOptimizer::DepSummary;
  // MatMul example
  // Note that cache optimization is done in terms of bits, to support
  // sub-byte objects and mixed-precision.
  // E.g., the reverse pass following a `MaxPool` will include a `BitArray`.
  DS *ds{DS::create(tlf.getAlloc(), 2, 2, 1,
                    [](MutArray<uint16_t, DenseDims<3>> dep,
                       MutArray<uint16_t, DenseDims<3>> indep) {
                      // A[m,k]
                      dep[0, 0] = 6;  // dep 110
                      dep[1, 0] = 64; // fit coef
                      dep[2, 0] = 64; // cost coef
                      // B[k,n]
                      dep[0, 1] = 5;  // dep 101
                      dep[1, 1] = 64; // fit coef
                      dep[2, 1] = 64; // cost coef
                      // C[m,n] +=
                      indep[0, 0] = 3;   // dep 011
                      indep[1, 0] = 64;  // fit coef
                      indep[2, 0] = 128; // cost coef
                    })};
  // Note, takes allocator by value; assumed construction->use has no life.
  // FIXME: make into a function, not an object?
  // Perhaps pass `Alloc` into `cacheOptEntry`, instead?
  CostModeling::Cache::CacheOptimizer co{.unrolls_ = {},
                                         .caches_ = skx.cacheSummary(),
                                         .cachelinebits_ = 512,
                                         .alloc_ = *tlf.getAlloc()};

  // TODO:
  // 1. need to create `DepSummary` per leaf
  // 2. add phi counts to each loop
  std::array<CostModeling::LoopSummary, 3> lsa{
    CostModeling::LoopSummary{.reorderable_ = true,
                              .known_trip_ = false,
                              .reorderable_sub_tree_size_ = 2,
                              .num_reduct_ = 0,
                              .num_sub_loops_ = 1,
                              .trip_count_ = 8192},
    CostModeling::LoopSummary{.reorderable_ = true,
                              .known_trip_ = false,
                              .reorderable_sub_tree_size_ = 1,
                              .num_reduct_ = 0,
                              .num_sub_loops_ = 1,
                              .trip_count_ = 8192},
    CostModeling::LoopSummary{.reorderable_ = true,
                              .known_trip_ = false,
                              .reorderable_sub_tree_size_ = 0,
                              .num_reduct_ = 1,
                              .num_sub_loops_ = 0,
                              .trip_count_ = 8192}};
  std::array<CostModeling::LoopTransform, 3> lta{
    CostModeling::LoopTransform{.l2vector_width_ = 0,
                                .register_unroll_factor_ = 8,
                                .cache_unroll_factor_ = 0,
                                .cache_permutation_ = 0xf},
    CostModeling::LoopTransform{.l2vector_width_ = 3,
                                .register_unroll_factor_ = 2,
                                .cache_unroll_factor_ = 0,
                                .cache_permutation_ = 0xf},
    CostModeling::LoopTransform{.l2vector_width_ = 0,
                                .register_unroll_factor_ = 0,
                                .cache_unroll_factor_ = 0,
                                .cache_permutation_ = 0xf}};

  CostModeling::LoopSummaries ls{
    .loop_summaries_ = {lsa.data(), math::length(3)},
    .trfs_ = {lta.data(), math::length(3)}};
  {
    auto [best, dsnull] = co.cacheOpt(ls, phi_costs.data(), ds);
    // auto [costb, lseb, dsnullb, stsb] = co.bruteForce(ls, phi_counts.data(),
    // ds); EXPECT_EQ(cost, costb);
    EXPECT_LE(best.cost_, 40739441400.289772);
    // EXPECT_LE(cost, 22355034789.266411);
    EXPECT_FALSE(dsnull);
    EXPECT_EQ(lta[0].cache_unroll(), 30);
    EXPECT_EQ(lta[1].cache_unroll(), 13);
    EXPECT_EQ(lta[2].cache_unroll(), 152);
  }
  lta[0].register_unroll_factor_ = 13;
  lta[1].register_unroll_factor_ = 1;
  {
    auto [best, dsnull] = co.cacheOpt(ls, phi_costs.data(), ds);
    // auto [costb, lseb, dsnullb, stsb] = co.bruteForce(ls, phi_counts.data(),
    // ds); EXPECT_EQ(cost, costb);
    EXPECT_LE(best.cost_, 39843469888.028526);
    // EXPECT_LE(cost, 22355034789.266411);
    EXPECT_FALSE(dsnull);
    EXPECT_EQ(lta[0].cache_unroll(), 19);
    EXPECT_EQ(lta[1].cache_unroll(), 22);
    EXPECT_EQ(lta[2].cache_unroll(), 136);
  }
  // multithreaded
  lta[0].register_unroll_factor_ = 8;
  lta[1].register_unroll_factor_ = 2;
  // NOTE: these are not fitting in l1 cache, despite being close...
  co.caches_[2].stride_ = 18z * 8 * skx.getL3DStride();
  {
    auto [best, dsnull] = co.cacheOpt(ls, phi_costs.data(), ds);
    EXPECT_LE(best.cost_, 19823621437.113483);
    EXPECT_EQ(lta[0].cache_unroll(), 456);
    EXPECT_EQ(lta[1].cache_unroll(), 15);
    EXPECT_EQ(lta[2].cache_unroll(), 318);
  }
  lta[0].register_unroll_factor_ = 13;
  lta[1].register_unroll_factor_ = 1;
  {
    auto [best, dsnull] = co.cacheOpt(ls, phi_costs.data(), ds);
    EXPECT_LE(best.cost_, 19952920134.5406);
    EXPECT_EQ(lta[0].cache_unroll(), 300);
    EXPECT_EQ(lta[1].cache_unroll(), 22);
    EXPECT_EQ(lta[2].cache_unroll(), 302);
  }
}
