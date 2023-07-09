
#include "ArrayReference.hpp"
#include "Math/Array.hpp"
#include "Math/Comparisons.hpp"
#include "Math/Math.hpp"
#include "Math/Orthogonalize.hpp"
#include "Polyhedra/Loops.hpp"
#include "TestUtilities.hpp"
#include <Utilities/MatrixStringParse.hpp>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/Triple.h>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <random>

namespace poly {

using math::DenseMatrix, math::DenseDims, math::PtrMatrix, math::MutPtrMatrix,
  math::Col, math::end, math::_, utils::operator""_mat;

namespace {
auto orthogonalize(utils::Arena<> *alloc,
                   llvm::SmallVectorImpl<ArrayReference *> const &ai)
  -> std::optional<
    std::pair<poly::Loop *, llvm::SmallVector<ArrayReference, 0>>> {

  // need to construct matrix `A` of relationship
  // B*L = I
  // where L are the loop induct variables, and I are the array indices
  // e.g., if we have `C[i + j, j]`, then
  // B = [1 1; 0 1]
  // additionally, the loop is defined by the bounds
  // A*L = A*(B\^-1 * I) <= r
  // assuming that `B` is an invertible integer matrix (i.e. is unimodular),
  // OwningArena<> alloc;
  const poly::Loop &alnp = *(ai[0]->loop);
  const ptrdiff_t numLoops = alnp.getNumLoops();
  const ptrdiff_t numSymbols = alnp.getNumSymbols();
  ptrdiff_t numRow = 0;
  for (auto *a : ai) numRow += a->getArrayDim();
  DenseMatrix<int64_t> S(DenseDims{numLoops, numRow}, int64_t(0));
  Col i = 0;
  for (auto *a : ai) {
    PtrMatrix<int64_t> A = a->indexMatrix();
    for (ptrdiff_t j = 0; j < numLoops; ++j)
      for (ptrdiff_t k = 0; k < A.numCol(); ++k) S(j, i + k) = A(j, k);
    i += A.numCol();
  }
  auto [K, included] = math::NormalForm::orthogonalize(S);
  if (included.empty()) return {};
  // We let
  // L = K'*J
  // Originally, the loop bounds were
  // A*L <= b
  // now, we have (A = alnp.aln->A, r = alnp.aln->r)
  // (A*K')*J <= r
  DenseMatrix<int64_t> AK{alnp.getA()};
  AK(_, _(numSymbols, end))
    << alnp.getA()(_, _(numSymbols, end)) * K.transpose();

  auto *alnNew =
    poly::Loop::construct(alloc, nullptr, std::move(AK), alnp.getSyms(), true);
  alnNew->pruneBounds();
  math::IntMatrix KS{K * S};
  std::pair<poly::Loop *, llvm::SmallVector<ArrayReference, 0>> ret{
    std::make_pair(alnNew, llvm::SmallVector<ArrayReference, 0>())};
  llvm::SmallVector<ArrayReference, 0> &newArrayRefs = ret.second;
  newArrayRefs.reserve(numRow);
  i = 0;
  for (auto *a : ai) {
    Col j = i + a->getArrayDim();
    newArrayRefs.emplace_back(*a, ret.first, KS(_, _(i, j)));
    EXPECT_EQ(newArrayRefs.back().indexMatrix(), KS(_, _(i, j)));
    i = j;
  }
  return ret;
}
} // namespace

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(OrthogonalizeTest, BasicAssertions) {
  // for m = 0:M-1, n = 0:N-1, i = 0:I-1, j = 0:J-1
  //   W[m + i, n + j] += C[i,j] * B[m,n]
  //
  // Loops: m, n, i, j
  math::IntMatrix A{"[-1 1 0 0 0 -1 0 0 0; "
                    "0 0 0 0 0 1 0 0 0; "
                    "-1 0 1 0 0 0 -1 0 0; "
                    "0 0 0 0 0 0 1 0 0; "
                    "-1 0 0 1 0 0 0 -1 0; "
                    "0 0 0 0 0 0 0 1 0; "
                    "-1 0 0 0 1 0 0 0 -1; "
                    "0 0 0 0 0 0 0 0 1]"_mat};

  TestLoopFunction tlf;
  tlf.addLoop(std::move(A), 4);
  poly::Loop *aln = tlf.getLoopNest(0);
  EXPECT_FALSE(aln->isEmpty());
  llvm::ScalarEvolution &SE{tlf.getSE()};
  auto *i64 = tlf.getInt64Ty();
  const llvm::SCEV *N = aln->getSyms()[2];
  const llvm::SCEV *J = aln->getSyms()[3];
  const llvm::SCEVUnknown *scevW = tlf.getSCEVUnknown(tlf.createArray());
  const llvm::SCEVUnknown *scevC = tlf.getSCEVUnknown(tlf.createArray());
  const llvm::SCEVUnknown *scevB = tlf.getSCEVUnknown(tlf.createArray());
  // we have three array refs
  // W[i+m, j+n]
  // llvm::SmallVector<std::pair<MPoly,MPoly>>
  ArrayReference War{scevW, aln, 2};
  {
    MutPtrMatrix<int64_t> indMat = War.indexMatrix();
    indMat << 0;
    indMat(0, 0) = 1; // m
    indMat(2, 0) = 1; // i
    indMat(1, 1) = 1; // n
    indMat(3, 1) = 1; // j
                      // I + M -1
    War.sizes[0] = SE.getAddExpr(N, SE.getAddExpr(J, SE.getMinusOne(i64)));
    War.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // llvm::errs() << "War = " << War << "\n";

  // B[i, j]
  ArrayReference Bar{scevB, aln, 2};
  {
    MutPtrMatrix<int64_t> indMat = Bar.indexMatrix();
    indMat << 0;
    indMat(2, 0) = 1; // i
    indMat(3, 1) = 1; // j
    Bar.sizes[0] = J;
    Bar.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // llvm::errs() << "Bar = " << Bar << "\n";

  // C[m, n]
  ArrayReference Car{scevC, aln, 2};
  {
    MutPtrMatrix<int64_t> indMat = Car.indexMatrix();
    indMat << 0;
    indMat(0, 0) = 1; // m
    indMat(1, 1) = 1; // n
    Car.sizes[0] = N;
    Car.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // llvm::errs() << "Car = " << Car << "\n";

  llvm::SmallVector<ArrayReference, 0> allArrayRefs{War, Bar, Car};
  llvm::SmallVector<ArrayReference *> ai{
    allArrayRefs.data(), allArrayRefs.data() + 1, allArrayRefs.data() + 2};

  std::optional<std::pair<poly::Loop *, llvm::SmallVector<ArrayReference, 0>>>
    orth(orthogonalize(tlf.getAlloc(), ai));

  EXPECT_TRUE(orth.has_value());
  assert(orth.has_value());
  poly::Loop *newAln = orth->first;
  llvm::SmallVector<ArrayReference, 0> &newArrayRefs = orth->second;
  for (auto &&ar : newArrayRefs) ar.loop = newAln;
  // for (size_t i = 0; i < newArrayRefs.size(); ++i)
  //   llvm::errs() << "newArrayRefs[" << i
  //                << "].indexMatrix() = " << newArrayRefs[i].indexMatrix()
  //                << "\n";
  EXPECT_EQ(countNonZero(newArrayRefs[0].indexMatrix()(_, 0)), 1);
  EXPECT_EQ(countNonZero(newArrayRefs[0].indexMatrix()(_, 1)), 1);
  EXPECT_EQ(countNonZero(newArrayRefs[1].indexMatrix()(_, 0)), 1);
  EXPECT_EQ(countNonZero(newArrayRefs[1].indexMatrix()(_, 1)), 1);
  EXPECT_EQ(countNonZero(newArrayRefs[2].indexMatrix()(_, 0)), 2);
  EXPECT_EQ(countNonZero(newArrayRefs[2].indexMatrix()(_, 1)), 2);
  llvm::errs() << "A=" << newAln->getA() << "\n";
  // llvm::errs() << "b=" << PtrVector<MPoly>(newAln.aln->b);
  llvm::errs() << "Skewed loop nest:\n" << newAln << "\n";
  auto loop3Count = countSigns(newAln->getA(), 3 + newAln->getNumSymbols());
  EXPECT_EQ(loop3Count[0], 2);
  EXPECT_EQ(loop3Count[1], 1);
  newAln = newAln->removeLoop(tlf.getAlloc(), 3);
  auto loop2Count = countSigns(newAln->getA(), 2 + newAln->getNumSymbols());
  EXPECT_EQ(loop2Count[0], 2);
  EXPECT_EQ(loop2Count[1], 1);
  newAln = newAln->removeLoop(tlf.getAlloc(), 2);
  auto loop1Count = countSigns(newAln->getA(), 1 + newAln->getNumSymbols());
  EXPECT_EQ(loop1Count[0], 1);
  EXPECT_EQ(loop1Count[1], 0);
  newAln = newAln->removeLoop(tlf.getAlloc(), 1);
  auto loop0Count = countSigns(newAln->getA(), 0 + newAln->getNumSymbols());
  EXPECT_EQ(loop0Count[0], 1);
  EXPECT_EQ(loop0Count[1], 0);
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(BadMul, BasicAssertions) {
  math::IntMatrix A{"[-3 1 1 1 -1 0 0; "
                    "0 0 0 0 1 0 0; "
                    "-2 1 0 1 0 -1 0; "
                    "0 0 0 0 0 1 0; "
                    "0 0 0 0 1 -1 0; "
                    "-1 0 1 0 -1 1 0; "
                    "-1 1 0 0 0 0 -1; "
                    "0 0 0 0 0 0 1; "
                    "0 0 0 0 0 1 -1; "
                    "-1 0 0 1 0 -1 1]"_mat};

  TestLoopFunction tlf;
  tlf.addLoop(std::move(A), 3);
  poly::Loop *aln = tlf.getLoopNest(0);
  EXPECT_FALSE(aln->isEmpty());
  llvm::ScalarEvolution &SE{tlf.getSE()};
  llvm::IntegerType *i64 = tlf.getInt64Ty();
  const llvm::SCEV *N = aln->getSyms()[1];
  const llvm::SCEV *K = aln->getSyms()[2];

  // auto Zero = Polynomial::Term{int64_t(0), Polynomial::Monomial()};
  // auto One = Polynomial::Term{int64_t(1), Polynomial::Monomial()};
  // for i in 0:M+N+K-3, l in max(0,i+1-N):min(M+K-2,i), j in
  // max(0,l+1-K):min(M-1,l)
  //       W[j,i-l] += B[j,l-j]*C[l-j,i-l]
  //
  // Loops: i, l, j

  const llvm::SCEVUnknown *scevW = tlf.getSCEVUnknown(tlf.createArray());
  const llvm::SCEVUnknown *scevB = tlf.getSCEVUnknown(tlf.createArray());
  const llvm::SCEVUnknown *scevC = tlf.getSCEVUnknown(tlf.createArray());
  // for i in 0:M+N+K-3, l in max(0,i+1-N):min(M+K-2,i), j in
  // max(0,l+1-K):min(M-1,l)
  // W[j,i-l] += B[j,l-j]*C[l-j,i-l]
  // 0, 1, 2
  // i, l, j
  // we have three array refs
  // W[j, i - l] // M x N
  const int iId = 0, lId = 1, jId = 2;
  ArrayReference War(scevW, aln, 2); //, axes, indTo
  {
    MutPtrMatrix<int64_t> indMat = War.indexMatrix();
    indMat << 0;
    indMat(jId, 0) = 1;  // j
    indMat(iId, 1) = 1;  // i
    indMat(lId, 1) = -1; // l
    War.sizes[0] = N;
    War.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // llvm::errs() << "War = " << War << "\n";

  // B[j, l - j] // M x K
  ArrayReference Bar(scevB, aln, 2); //, axes, indTo
  {
    MutPtrMatrix<int64_t> indMat = Bar.indexMatrix();
    indMat << 0;
    indMat(jId, 0) = 1;  // j
    indMat(lId, 1) = 1;  // l
    indMat(jId, 1) = -1; // j
    Bar.sizes[0] = K;
    Bar.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // llvm::errs() << "Bar = " << Bar << "\n";

  // C[l-j,i-l] // K x N
  ArrayReference Car(scevC, aln, 2); //, axes, indTo
  {
    MutPtrMatrix<int64_t> indMat = Car.indexMatrix();
    indMat << 0;
    indMat(lId, 0) = 1;  // l
    indMat(jId, 0) = -1; // j
    indMat(iId, 1) = 1;  // i
    indMat(lId, 1) = -1; // l
    Car.sizes[0] = N;
    Car.sizes[1] = SE.getConstant(i64, 8, /*isSigned=*/false);
  }
  // llvm::errs() << "Car = " << Car << "\n";

  llvm::SmallVector<ArrayReference, 0> allArrayRefs{War, Bar, Car};
  llvm::SmallVector<ArrayReference *> ai{
    allArrayRefs.data(), allArrayRefs.data() + 1, allArrayRefs.data() + 2};

  std::optional<std::pair<poly::Loop *, llvm::SmallVector<ArrayReference, 0>>>
    orth{orthogonalize(tlf.getAlloc(), ai)};

  EXPECT_TRUE(orth.has_value());
  assert(orth.has_value());
  poly::Loop *newAln = orth->first;
  llvm::SmallVector<ArrayReference, 0> &newArrayRefs = orth->second;

  for (auto &ar : newArrayRefs) ar.loop = newAln;

  // llvm::errs() << "b=" << PtrVector<MPoly>(newAln->aln->b);
  // llvm::errs() << "Skewed loop nest:\n" << newAln << "\n";
  auto loop2Count = countSigns(newAln->getA(), 2 + newAln->getNumSymbols());
  EXPECT_EQ(loop2Count[0], 1);
  EXPECT_EQ(loop2Count[1], 0);
  newAln = newAln->removeLoop(tlf.getAlloc(), 2);
  auto loop1Count = countSigns(newAln->getA(), 1 + newAln->getNumSymbols());
  EXPECT_EQ(loop1Count[0], 1);
  EXPECT_EQ(loop1Count[1], 0);
  newAln = newAln->removeLoop(tlf.getAlloc(), 1);
  auto loop0Count = countSigns(newAln->getA(), 0 + newAln->getNumSymbols());
  EXPECT_EQ(loop0Count[0], 1);
  EXPECT_EQ(loop0Count[1], 0);

  // llvm::errs() << "New ArrayReferences:\n";
  // for (auto &ar : newArrayRefs)
  //   llvm::errs() << ar << "\n\n";
}

// NOLINTNEXTLINE(modernize-use-trailing-return-type)
TEST(OrthogonalizeMatricesTest, BasicAssertions) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> distrib(-3, 3);

  const size_t M = 7;
  const size_t N = 7;
  DenseMatrix<int64_t> A(DenseDims{M, N});
  DenseMatrix<int64_t> B(DenseDims{N, N});
  const size_t iters = 1000;
  for (size_t i = 0; i < iters; ++i) {
    for (auto &&a : A) a = distrib(gen);
    // llvm::errs() << "Random A =\n" << A << "\n";
    A = math::orthogonalize(std::move(A));
    // llvm::errs() << "Orthogonal A =\n" << A << "\n";
    // note, A'A is not diagonal
    // but AA' is
    B = A * A.transpose();
    // llvm::errs() << "A'A =\n" << B << "\n";
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-else"
#endif
    for (size_t m = 0; m < M; ++m)
      for (size_t n = 0; n < N; ++n)
        if (m != n) EXPECT_EQ(B(m, n), 0);
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  }
}
} // namespace poly
