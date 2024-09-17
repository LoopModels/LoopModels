#include <gtest/gtest.h>
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/ScalarEvolutionExpressions.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/Allocator.h>
#include <llvm/Support/Casting.h>
#ifndef USE_MODULE
#include "Alloc/Arena.cxx"
#include "Dicts/Dict.cxx"
#include "IR/Address.cxx"
#include "IR/Cache.cxx"
#include "IR/Instruction.cxx"
#include "IR/Node.cxx"
#include "IR/Phi.cxx"
#include "LinearProgramming/LoopBlock.cxx"
#include "Math/AxisTypes.cxx"
#include "Math/Comparisons.cxx"
#include "Math/Indexing.cxx"
#include "Optimize/CostModeling.cxx"
#include "Polyhedra/Dependence.cxx"
#include "Polyhedra/Loops.cxx"
#include "Polyhedra/Schedule.cxx"
#include "TestUtilities.cxx"
#include "Utilities/MatrixStringParse.cxx"
#include <array>
#include <cstddef>
#include <cstdint>
#else
import ArrayParse;
import Comparisons;
import CostModeling;
import Invariant;
import STL;
import TestUtilities;
#endif

using math::DenseMatrix, math::DenseDims, math::PtrMatrix, math::MutPtrMatrix,
  math::Vector, math::IntMatrix, math::last, math::_, utils::operator""_mat;

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(TriangularExampleTest, BasicAssertions) {
  // badly written triangular solve:
  // for (m = 0; m < M; ++m){
  //   for (n = 0; n < N; ++n){
  //     A[n,m] = B[n,m];
  //   }
  //   for (n = 0; n < N; ++n){
  //     A[n,m] = A[n,m] / U[n,n];
  //     for (k = n+1; k < N; ++k){
  //       A[k,m] = A[k,m] - U[k,n]*A[n,m];
  //     }
  //   }
  // }
  TestLoopFunction tlf;
  poly::Loop *loopMN = tlf.addLoop("[-1 1 0 -1 0; "
                                   "0 0 0 1 0; "
                                   "-1 0 1 0 -1; "
                                   "0 0 0 0 1]"_mat,
                                   2);
  poly::Loop *loopMNK = tlf.addLoop("[-1 1 0 -1 0 0; "
                                    "0 0 0 1 0 0; "
                                    "-1 0 1 0 -1 0; "
                                    "0 0 0 0 1 0; "
                                    "-1 0 1 0 0 -1; "
                                    "-1 0 0 0 -1 1]"_mat,
                                    3);
  ASSERT_FALSE(loopMN->isEmpty());
  ASSERT_FALSE(loopMNK->isEmpty());
  ASSERT_EQ(loopMN->getSyms().size(), loopMNK->getSyms().size());
  for (ptrdiff_t i = 0; i < loopMN->getSyms().size(); ++i)
    ASSERT_EQ(loopMN->getSyms()[i], loopMNK->getSyms()[i]);

  auto &builder = tlf.getBuilder();

  // create arrays
  llvm::Type *f64 = builder.getDoubleTy();
  IR::Cache &ir{tlf.getIRC()};
  IR::Value *ptrB = tlf.createArray();
  IR::Value *ptrA = tlf.createArray();
  IR::Value *ptrU = tlf.createArray();

  IR::Value *M = loopMN->getSyms()[0];
  IR::Value *N = loopMN->getSyms()[1];

  IR::Cint *one = tlf.getConstInt(1);

  // Currently nest:
  // for (m = 0; m < M; ++m){
  //   for (n = 0; n < N; ++n){
  //     A[n,m] = B[n,m]; // [0, 0, 0...1]
  //   }
  // A[n,m] = -> B[n,m] <-
  IR::Addr *m00{tlf.createLoad(ptrB, f64, "[0 1; 1 0]"_mat,
                               std::array<IR::Value *, 2>{M, one},
                               "[0 0 0]"_mat, loopMN)};
  // -> A[n,m] <- = B[n,m]
  IR::Addr *m01{tlf.createStow(ptrA, m00, "[0 1; 1 0]"_mat,
                               std::array<IR::Value *, 2>{M, one},
                               "[0 0 1]"_mat, loopMN)};
  // Next store:
  //   for (n = 0; n < N; ++n){
  //     A[n,m] = A[n,m] / U[n,n];   // [0, 1, 0...2]
  // A[n,m] = -> A[n,m] <- / U[n,n]; // sch2
  IR::Addr *m10{tlf.createLoad(ptrA, f64, "[0 1; 1 0]"_mat,
                               std::array<IR::Value *, 2>{M, one},
                               "[0 1 0]"_mat, loopMN)};
  // A[n,m] = A[n,m] / -> U[n,n] <-;
  IR::Addr *m11{tlf.createLoad(ptrU, f64, "[0 1; 0 1]"_mat,
                               std::array<IR::Value *, 2>{N, one},
                               "[0 1 1]"_mat, loopMN)};

  // -> A[n,m] <- = A[n,m] / U[n,n]; // sch2
  IR::Addr *m12{tlf.createStow(ptrA, ir.createFDiv(m10, m11), "[0 1; 1 0]"_mat,
                               std::array<IR::Value *, 2>{M, one},
                               "[0 1 2]"_mat, loopMN)};

  // Now, we handle the reduction store:
  //     for (k = n+1; k < N; ++k){
  //       A[k,m] = A[k,m] - U[k,n]*A[n,m]; // [0, 1, 3, 0...3]
  //     }
  //   }
  // }
  // A[k,m] = A[k,m] - A[n,m]* -> U[k,n] <-;
  IR::Addr *m130{tlf.createLoad(ptrU, f64, "[0 0 1; 0 1 0]"_mat,
                                std::array<IR::Value *, 2>{N, one},
                                "[0 1 3 0]"_mat, loopMNK)};
  // A[k,m] = A[k,m] - -> A[n,m] <- *U[k,n];
  IR::Addr *m131{tlf.createLoad(ptrA, f64, "[0 1 0; 1 0 0]"_mat,
                                std::array<IR::Value *, 2>{M, one},
                                "[0 1 3 1]"_mat, loopMNK)};
  // A[k,m] = -> A[k,m] <- - A[n,m]*U[k,n];
  IR::Addr *m132{tlf.createLoad(ptrA, f64, "[0 0 1; 1 0 0]"_mat,
                                std::array<IR::Value *, 2>{M, one},
                                "[0 1 3 2]"_mat, loopMNK)};
  // -> A[k,m] <- = A[k,m] - A[n,m]*U[k,n];
  IR::Addr *m133{tlf.createStow(
    ptrA, ir.createFSub(m132, ir.createFMul(m130, m131)), "[0 0 1; 1 0 0]"_mat,
    std::array<IR::Value *, 2>{M, one}, "[0 1 3 3]"_mat, loopMNK)};

  poly::Dependencies deps{};
  // First, comparisons of store to `A[n,m] = B[n,m]` versus...
  // // load in `A[n,m] = A[n,m] / U[n,n]`
  {
    deps.check(tlf.getAlloc(), m01, m10);
    ASSERT_EQ(m01->getEdgeIn(), -1);
    ASSERT_EQ(m10->getEdgeOut(), -1);
    ASSERT_EQ(m01->getEdgeOut(), m10->getEdgeIn());
    int32_t depId = m10->getEdgeIn();
    ASSERT_TRUE(deps[depId].isForward());
  }
  //
  //
  // store in `A[n,m] = A[n,m] / U[n,n]`
  {
    deps.check(tlf.getAlloc(), m01, m12);
    int32_t depId = m01->getEdgeOut();
    ASSERT_EQ(depId, m12->getEdgeIn());
    ASSERT_TRUE(deps[depId].isForward());
  }
  //
  // sch3_               3        0         1     2
  // load `A[n,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    deps.check(tlf.getAlloc(), m01, m131);
    int32_t depId = m01->getEdgeOut();
    ASSERT_EQ(depId, m131->getEdgeIn());
    ASSERT_TRUE(deps[depId].isForward());
  }
  // load `A[k,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  //
  {
    deps.check(tlf.getAlloc(), m01, m132);
    int32_t depId = m01->getEdgeOut();
    ASSERT_EQ(depId, m132->getEdgeIn());
    ASSERT_TRUE(deps[depId].isForward());
  }
  // store `A[k,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    deps.check(tlf.getAlloc(), m01, m133);
    int32_t depId = m01->getEdgeOut();
    ASSERT_EQ(depId, m133->getEdgeIn());
    ASSERT_TRUE(deps[depId].isForward());
  }

  // Second, comparisons of load in `A[n,m] = A[n,m] / U[n,n]`
  // with...
  // store in `A[n,m] = A[n,m] / U[n,n]`
  {
    deps.check(tlf.getAlloc(), m10, m12);
    int32_t depId = m10->getEdgeOut();
    ASSERT_EQ(depId, m12->getEdgeIn());
    ASSERT_TRUE(deps[depId].isForward());
  }

  //
  // sch3_               3        0         1     2
  // load `A[n,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    deps.check(tlf.getAlloc(), m10, m131);
    int32_t depId = m10->getEdgeOut();
    ASSERT_EQ(depId, m131->getEdgeIn());
    ASSERT_TRUE(deps[depId].isForward());
  }
  // load `A[k,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    // load `A[n,m]` happens after all loads and stores
    // `A[k,m]`, as `for(k = n+1; k < N; ++k)`
    // When we access `A[n,m]`, we're never accessing it
    // through `A[k,m]` again (but did for each prior iter)
    deps.check(tlf.getAlloc(), m10, m132);
    // dependence is m10 <- m132
    int32_t depId = m10->getEdgeIn();
    ASSERT_EQ(depId, m132->getEdgeOut());
    ASSERT_FALSE(deps[depId].isForward());
    if (depId != m132->getEdgeOut()) __builtin_trap();
  }
  // store `A[k,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    deps.check(tlf.getAlloc(), m10, m133);
    // dependence is m10 <- m133
    int32_t depId = m10->getEdgeIn();
    ASSERT_EQ(depId, m133->getEdgeOut());
    ASSERT_FALSE(deps[depId].isForward());
  }

  // Third, comparisons of store in `A[n,m] = A[n,m] / U[n,n]`
  // with...
  // sch3_               3        0         1     2
  // load `A[n,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    deps.check(tlf.getAlloc(), m12, m131);
    // m12 -> m131
    int32_t depId = m12->getEdgeOut();
    ASSERT_EQ(depId, m131->getEdgeIn());
    ASSERT_TRUE(deps[depId].isForward());
  }
  // load `A[k,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    deps.check(tlf.getAlloc(), m12, m132);
    // m12 <- m132
    int32_t depId = m12->getEdgeIn();
    ASSERT_EQ(depId, m132->getEdgeOut());
    ASSERT_FALSE(deps[depId].isForward());
  }
  // store `A[k,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    deps.check(tlf.getAlloc(), m12, m133);
    // m12 <- m133
    int32_t depId = m12->getEdgeIn();
    ASSERT_EQ(depId, m133->getEdgeOut());
    ASSERT_FALSE(deps[depId].isForward());
  }

  // Fourth, comparisons of load `A[n,m]` in
  // sch3_               3        0         1     2
  // load `A[n,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  // with...
  // load `A[k,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    deps.check(tlf.getAlloc(), m131, m132);
    // m131 <- m132
    int32_t depId = m131->getEdgeIn();
    ASSERT_EQ(depId, m132->getEdgeOut());
    ASSERT_FALSE(deps[depId].isForward());
    ASSERT_EQ(deps[depId].depPoly()->getTimeDim(), 0);
  }
  // store `A[k,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    deps.check(tlf.getAlloc(), m131, m133);
    // m131 <- m133
    int32_t depId = m131->getEdgeIn();
    ASSERT_EQ(depId, m133->getEdgeOut());
    ASSERT_FALSE(deps[depId].isForward());
    ASSERT_EQ(deps[depId].depPoly()->getTimeDim(), 0);
  }

  // Fifth, comparisons of load `A[k,m]` in
  // sch3_               3        0         1     2
  // load `A[k,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  // with...
  // store `A[k,m]` in 'A[k,m] = A[k,m] - A[n,m]*U[k,n]'
  {
    deps.check(tlf.getAlloc(), m132, m133);
    int32_t forward = m132->getEdgeOut();
    int32_t reverse = m132->getEdgeIn();
    ASSERT_EQ(forward, m133->getEdgeIn());
    ASSERT_EQ(reverse, m133->getEdgeOut());
    ASSERT_TRUE(deps[forward].isForward());
    ASSERT_FALSE(deps[reverse].isForward());
    if (!deps[forward].isForward()) __builtin_trap();
    if (deps[reverse].isForward()) __builtin_trap();
    auto *fwdDepPoly = deps[forward].depPoly();
    auto *revDepPoly = deps[reverse].depPoly();
    ASSERT_TRUE(allZero(fwdDepPoly->getE()[_, 0]));
    ASSERT_FALSE(allZero(revDepPoly->getE()[_, 0]));

    ptrdiff_t nonZeroInd = -1;
    for (unsigned i = 0; i < revDepPoly->getE().numRow(); ++i) {
      bool notZero = !allZero(revDepPoly->getEqSymbols(i));
      // we should only find 1 non-zero
      ASSERT_FALSE((nonZeroInd != -1) & notZero);
      if (notZero) nonZeroInd = i;
    }
    // vt1 is `n` for the load
    // v_4 is `n` for the store
    // thus, we expect vt1 = v_4 + 1
    // that is, the load depends on the store from the previous iteration
    // (e.g., store when `v_4 = 0` is loaded when `vt1 = 1`.
    ASSERT_NE(nonZeroInd, -1);
    if (nonZeroInd == -1) __builtin_trap();
    auto nonZero = revDepPoly->getCompTimeEqOffset(nonZeroInd);
    const size_t numSymbols = revDepPoly->getNumSymbols();
    ASSERT_EQ(numSymbols, 3);
    ASSERT_TRUE(nonZero.has_value());
    if (*nonZero == 1) {
      // vt1 - v_4 == 1
      // 1 - vt1 + v_4 == 0
      ASSERT_EQ((revDepPoly->getE()[nonZeroInd, numSymbols + 1]), -1);
      ASSERT_EQ((revDepPoly->getE()[nonZeroInd, numSymbols + 4]), 1);

    } else {
      // -vt1 + v_4 == -1
      // -1 + vt1 - v_4 == 0
      ASSERT_EQ(*nonZero, -1);
      ASSERT_EQ((revDepPoly->getE()[nonZeroInd, numSymbols + 1]), 1);
      ASSERT_EQ((revDepPoly->getE()[nonZeroInd, numSymbols + 4]), -1);
    }
  }

  for (IR::Addr *A : tlf.getTreeResult().getAddr()) {
    A->setEdgeIn(-1);
    A->setEdgeOut(-1);
  }
  deps.clear();

  alloc::OwningArena salloc;
  lp::LoopBlock lblock{deps, salloc};
  lp::LoopBlock::OptimizationResult optRes =
    lblock.optimize(ir, tlf.getTreeResult());

  ASSERT_NE(optRes.nodes, nullptr);
  // orig order (outer <-> inner): m, n
  DenseMatrix<int64_t> optPhi2{DenseDims<>{math::row(2), math::col(2)}, 0};
  // phi2 loop order is
  optPhi2.antiDiag() << 1;
  // the scheduler swaps the order, making `n` outermost,
  // and `m` as innermost
  // orig order (outer <-> inner): m, n, k
  DenseMatrix<int64_t> optPhi3{"[0 0 1; 1 0 0; 0 1 0]"_mat};
  // phi3 loop order (outer <-> inner) is [k, m, n]
  // so the schedule moves  `m` inside. The reason for this is because
  // we are indexing row-major `A[n,m]`,
  // original indmat `[0 1; 1 0]`; swapping produces identity.
  for (auto *node : optRes.nodes->getVertices()) {
    poly::AffineSchedule s = node->getSchedule();
    if (s.getNumLoops() == 2) {
      ASSERT_EQ(s.getPhi(), optPhi2);
    } else {
      ASSERT_EQ(s.getNumLoops(), 3);
      ASSERT_EQ(s.getPhi(), optPhi3);
    }
    ASSERT_TRUE(allZero(s.getFusionOmega()));
    ASSERT_TRUE(allZero(s.getOffsetOmega()));
  }
  dict::set<llvm::BasicBlock *> loop_bbs{};
  dict::set<llvm::CallBase *> erase_candidates{};

  // auto [TL, unrolls, opti] = CostModeling::optimize(
  //   salloc, deps, ir, loopBBs, eraseCandidates, optRes, tlf.getTarget());
  auto [TL, opt, trfs] = CostModeling::optimize(
    salloc, deps, ir, loop_bbs, erase_candidates, optRes, tlf.getTarget());
  // FIXME: these should really be checked if they're doing the right thing.
  // It looks like they are NOT contiguous loads/stores?
  // br cacheOptBisect(CostModeling::LoopSummaries, double*,
  // CostModeling::Cache::CacheOptimizer::DepSummary*, long, long,
  // std::array<CostModeling::Cache::CacheOptimizer::Best, 7ul>,
  // CostModeling::LoopTransform*) if (unrolls_[0].reg_factor_ == 8)
  EXPECT_EQ(trfs[0].vector_width(), 1);
  EXPECT_EQ(trfs[1].vector_width(), 8);
  EXPECT_EQ(trfs[2].vector_width(), 1);
  EXPECT_EQ(trfs[0].reg_unroll(), 9);
  EXPECT_EQ(trfs[1].reg_unroll(), 3);
  EXPECT_EQ(trfs[2].reg_unroll(), 1);
  EXPECT_EQ(trfs[0].cache_unroll(), 29);
  EXPECT_EQ(trfs[1].cache_unroll(), 16);
  EXPECT_EQ(trfs[2].cache_unroll(), 128);
  EXPECT_EQ(trfs[0].cache_perm(), 15);
  EXPECT_EQ(trfs[1].cache_perm(), 1);
  EXPECT_EQ(trfs[2].cache_perm(), 2);
  // EXPECT_EQ(trfs[0].vector_width(), 1);
  // EXPECT_EQ(trfs[1].vector_width(), 8);
  // EXPECT_EQ(trfs[2].vector_width(), 1);
  // EXPECT_EQ(trfs[0].reg_unroll(), 9);
  // EXPECT_EQ(trfs[1].reg_unroll(), 3);
  // EXPECT_EQ(trfs[2].reg_unroll(), 1);
  IR::dumpGraph(TL);
  // Pattern: test level, test child, test next
  ASSERT_EQ(TL->getCurrentDepth(), 0);

  auto *L0 = TL->getSubLoop();
  ASSERT_NE(L0, nullptr);
  ASSERT_EQ(L0->getCurrentDepth(), 1);
  ASSERT_EQ(TL->getChild()->getChild(), m11);
  ASSERT_EQ(m11->getCurrentDepth(), 1);

  auto *L1 = L0->getSubLoop();
  ASSERT_NE(L1, nullptr);
  ASSERT_EQ(L1, m11->getSubLoop());
  ASSERT_EQ(L1->getCurrentDepth(), 2);
  ASSERT_EQ(L1->getChild(), m00);
  ASSERT_EQ(m00->getCurrentDepth(), 2);

  auto *L2 = L1->getSubLoop();
  ASSERT_NE(L2, nullptr);
  ASSERT_EQ(L2->getCurrentDepth(), 3);
  ASSERT_EQ(L2, m00->getSubLoop());
  auto *phi_acc = llvm::cast<IR::Phi>(L2->getChild());
  ASSERT_EQ(phi_acc->getOperand(0), m00);
  ASSERT_EQ(phi_acc->getNext(), m130);
  ASSERT_EQ(m130->getNext(), m131);
  ASSERT_EQ(m131->getNext()->getKind(), IR::Node::VK_Oprn);
  ASSERT_EQ(m130->getCurrentDepth(), 3);
  ASSERT_EQ(m131->getCurrentDepth(), 3);
  auto *C0 = llvm::cast<IR::Compute>(m131->getNext());
  ASSERT_EQ(C0->getOpId(), llvm::Instruction::FMul);
  if (C0->getOperand(0) == m130) {
    ASSERT_EQ(C0->getOperand(1), m131);
  } else {
    ASSERT_EQ(C0->getOperand(0), m131);
    ASSERT_EQ(C0->getOperand(1), m130);
  }
  ASSERT_EQ(C0->getCurrentDepth(), 3);
  auto *C1 = llvm::cast<IR::Compute>(C0->getNext());
  ASSERT_EQ(C1->getOpId(), llvm::Instruction::FSub);
  ASSERT_EQ(C1->getOperand(0), phi_acc);
  ASSERT_EQ(C1->getOperand(1), C0);
  ASSERT_EQ(C1->getCurrentDepth(), 3);
  ASSERT_EQ(phi_acc->getOperand(1), C1);
  ASSERT_FALSE(L2->getSubLoop());
  ASSERT_FALSE(m130->getSubLoop());
  ASSERT_FALSE(m131->getSubLoop());
  ASSERT_FALSE(C0->getSubLoop());
  ASSERT_FALSE(C1->getSubLoop());
  ASSERT_FALSE(C1->getNext());

  auto *phi_join = llvm::cast<IR::Phi>(L2->getNext());
  ASSERT_EQ(phi_join->getOperand(0), m00);
  ASSERT_EQ(phi_join->getOperand(1), C1);
  auto *C2 = llvm::cast<IR::Compute>(phi_join->getNext());
  ASSERT_EQ(C2->getOpId(), llvm::Instruction::FDiv);
  ASSERT_EQ(C2->getCurrentDepth(), 2);
  ASSERT_EQ(C2->getOperand(0), phi_join);
  ASSERT_EQ(C2->getOperand(1), m11);
  auto *stow = llvm::cast<IR::Addr>(C2->getNext());
  ASSERT_EQ(stow->getArrayPointer(), m133->getArrayPointer());
  ASSERT_EQ(stow->getArrayPointer(), ptrA);
  ASSERT_EQ(stow->indexMatrix(), "[1 0; 0 1]"_mat);
  ASSERT_EQ(stow->getCurrentDepth(), 2);
  ASSERT_EQ(stow->getStoredVal(), C2);
  ASSERT_FALSE(C2->getSubLoop());
  ASSERT_FALSE(stow->getSubLoop());
  ASSERT_FALSE(stow->getNext());

  ASSERT_FALSE(L0->getNext());
  ASSERT_FALSE(TL->getNext());

  ASSERT_EQ(L0->getLegality().peel_flag_, 4);
  ASSERT_EQ(L0->getLegality().ordered_reduction_count_, 0);
  ASSERT_EQ(L0->getLegality().unordered_reduction_count_, 0);
  ASSERT_TRUE(L0->getLegality().reorderable_);

  ASSERT_EQ(L1->getLegality().peel_flag_, 0);
  ASSERT_EQ(L1->getLegality().ordered_reduction_count_, 0);
  ASSERT_EQ(L1->getLegality().unordered_reduction_count_, 0);
  ASSERT_TRUE(L1->getLegality().reorderable_);

  ASSERT_EQ(L2->getLegality().peel_flag_, 0);
  ASSERT_EQ(L2->getLegality().ordered_reduction_count_, 0);
  ASSERT_EQ(L2->getLegality().unordered_reduction_count_, 1);
  ASSERT_TRUE(L2->getLegality().reorderable_);

  // Now, we disable fast math on the floating point ops, and make sure
  // we have a non-reorderable reduction.

  // L->printDotFile(salloc, std::cout);
}
