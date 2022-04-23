#pragma once

#include "./Math.hpp"
#include "./POSet.hpp"
#include "./Symbolics.hpp"
#include <cstdint>

// the AbstractPolyhedra defines methods we reuse across Polyhedra with known
// (`Int`) bounds, as well as with unknown (symbolic) bounds.
// In either case, we assume the matrix `A` consists of known integers.
template <class T, typename P> struct AbstractPolyhedra {
    Matrix<intptr_t, 0, 0, 0> A;
    llvm::SmallVector<P, 8> b;
    llvm::SmallVector<Matrix<intptr_t, 0, 0, 0>, 0> lowerA;
    llvm::SmallVector<Matrix<intptr_t, 0, 0, 0>, 0> upperA;
    llvm::SmallVector<llvm::SmallVector<P, 1>, 0> lowerb;
    llvm::SmallVector<llvm::SmallVector<P, 1>, 0> upperb;

    AbstractPolyhedra(const Matrix<intptr_t, 0, 0, 0> A,
                      const llvm::SmallVector<P, 8> b)
        : A(std::move(A)), b(std::move(b)), lowerA(A.size(0)),
          upperA(A.size(0)), lowerb(A.size(0)), upperb(A.size(0)){};

    size_t getNumVar() const { return A.size(0); }
    size_t getNumConstraints() const { return A.size(1); }

    // methods required to support AbstractPolyhedra
    bool knownLessEqualZero(P x) const {
        return static_cast<const T *>(this)->knownLessEqualZeroImpl(
            std::move(x));
    }
    bool knownGreaterEqualZero(const P &x) const {
        return static_cast<const T *>(this)->knownGreaterEqualZeroImpl(x);
    }
    size_t currentToOriginalPerm(intptr_t i) const {
        return static_cast<const T *>(this)->currentToOriginalPermImpl(i);
    }

    // setBounds(a, b, la, lb, ua, ub, i)
    // `la` and `lb` correspond to the lower bound of `i`
    // `ua` and `ub` correspond to the upper bound of `i`
    // Eliminate `i`, and set `a` and `b` appropriately.
    // Returns `true` if `a` still depends on another variable.
    static bool setBounds(PtrVector<intptr_t> a, P &b,
                          llvm::ArrayRef<intptr_t> la, const P &lb,
                          llvm::ArrayRef<intptr_t> ua, const P &ub, size_t i) {
        intptr_t cu = ua[i];
        intptr_t cl = la[i];
        b = cu * lb;
        Polynomial::fnmadd(b, ub, cl);
        size_t N = la.size();
        for (size_t n = 0; n < N; ++n) {
            a[n] = cu * la[n] - cl * ua[n];
        }
        a[i] = 0;
        return std::ranges::any_of(a, [](intptr_t ai) { return ai != 0; });
    }
    // independentOfInner(a, i)
    // checks if any `a[j] != 0`, such that `j != i`.
    // I.e., if this vector defines a hyper plane otherwise independent of `i`.
    static inline bool independentOfInner(llvm::ArrayRef<intptr_t> a,
                                          size_t i) {
        for (size_t j = 0; j < a.size(); ++j) {
            if ((a[j] != 0) & (i != j)) {
                return false;
            }
        }
        return true;
    }
    // eliminateVariable(&A, &b, const &AOld, const &bOld, i)
    // For `AOld' * x <= bOld, eliminates `i` from `AOld` and `bOld` using
    // Fourier–Motzkin elimination, storing the updated equations in `A` and
    // `b`.
    void eliminateVariable(Matrix<intptr_t, 0, 0, 128> &A,
                           llvm::SmallVectorImpl<P> &b,
                           const Matrix<intptr_t, 0, 0, 128> &AOld,
                           const llvm::SmallVectorImpl<P> &bOld,
                           const size_t i) const {
        // eliminate variable `i` according to original order
        const size_t numVarBase = getNumVar();
        size_t numNeg = 0;
        size_t numPos = 0;
        const auto [numVar, numCol] = AOld.size();
        for (size_t j = 0; j < numCol; ++j) {
            intptr_t Aij = AOld(i, j);
            numNeg += (Aij < 0);
            numPos += (Aij > 0);
        }
        const size_t numExclude = numCol - numNeg - numPos;
        const size_t numColA = numNeg * numPos + numExclude;
        A.resizeForOverwrite(numVar, numColA);
        b.resize(numColA);

        // assign to `A = Aold[:,exlcuded]`
        for (size_t j = 0, c = 0; c < numExclude; ++j) {
            if (AOld(i, j)) {
                continue;
            }
            for (size_t k = 0; k < numVar; ++k) {
                A(k, c) = AOld(k, j);
            }
            b[c++] = bOld[j];
        }
        size_t c = numExclude;
        for (size_t u = 0; u < numCol; ++u) {
            if (AOld(i, u) > 0) {
                bool independentOfInnerU =
                    independentOfInner(AOld.getCol(u), i);
                for (size_t l = 0; l < numCol; ++l) {
                    if (AOld(i, l) < 0) {
                        if ((independentOfInnerU &&
                             independentOfInner(AOld.getCol(l), i)) ||
                            (differentAuxiliaries(AOld, l, u, numVarBase))) {
                            // if we've eliminated everything, then this bound
                            // does not provide any useful information.
                            //
                            // We also check for differentAuxiliaries, as we
                            // don't care about any bounds comparing them, we
                            // only care about them in isolation.
                            continue;
                        }
                        c += setBounds(A.getCol(c), b[c], AOld.getCol(l),
                                       bOld[l], AOld.getCol(u), bOld[u], i);
                    }
                }
            }
        }
        A.resize(numVar, c);
        b.resize(c);
    }
    // Returns true if `sum(A[start:end,l] .| A[start:end,u]) > 1`
    // We use this as we are not interested in comparing the auxiliary
    // variables with one another.
    static bool differentAuxiliaries(const Matrix<intptr_t, 0, 0, 128> &A,
                                     size_t l, size_t u, size_t start) {
        size_t numVar = A.size(0);
        size_t orCount = 0;
        for (size_t i = start; i < numVar; ++i) {
            orCount += ((A(i, l) != 0) | (A(i, u) != 0));
        }
        return orCount > 1;
        // size_t lFound = false, uFound = false;
        // for (size_t i = start; i < numVar; ++i) {
        //     bool Ail = A(i, l) != 0;
        //     bool Aiu = A(i, u) != 0;
        //     // if the opposite was found on a previous iteration, then that
        //     must
        //     // mean both have different auxiliary variables.
        //     if ((Ail & uFound) | (Aiu & lFound)) {
        //         return true;
        //     }
        //     uFound |= Aiu;
        //     lFound |= Ail;
        // }
        // return false;
    }
    // returns std::make_pair(numNeg, numPos);
    // countNonZeroSign(Matrix A, i)
    // counts how many negative and positive elements there are in row `i`.
    // A row corresponds to a particular variable in `A'x <= b`.
    static std::pair<size_t, size_t>
    countNonZeroSign(const Matrix<intptr_t, 0, 0, 0> &A, size_t i) {
        size_t numNeg = 0;
        size_t numPos = 0;
        size_t numCol = A.size(1);
        for (size_t j = 0; j < numCol; ++j) {
            intptr_t Aij = A(i, j);
            numNeg += (Aij < 0);
            numPos += (Aij > 0);
        }
        return std::make_pair(numNeg, numPos);
    }
    // takes `A'x <= b`, and seperates into lower and upper bound equations w/
    // respect to `i`th variable
    static void categorizeBounds(Matrix<intptr_t, 0, 0, 0> &lA,
                                 Matrix<intptr_t, 0, 0, 0> &uA,
                                 llvm::SmallVectorImpl<P> &lB,
                                 llvm::SmallVectorImpl<P> &uB,
                                 const Matrix<intptr_t, 0, 0, 0> &A,
                                 const llvm::SmallVectorImpl<P> &b, size_t i) {
        auto [numLoops, numCol] = A.size();
        const auto [numNeg, numPos] = countNonZeroSign(A, i);
        lA.resize(numLoops, numNeg);
        lB.resize(numNeg);
        uA.resize(numLoops, numPos);
        uB.resize(numPos);
        // fill bounds
        for (size_t j = 0, l = 0, u = 0; j < numCol; ++j) {
            intptr_t Aij = A(i, j);
            if (Aij > 0) {
                for (size_t k = 0; k < numLoops; ++k) {
                    uA(k, u) = A(k, j);
                }
                uB[u++] = b[j];
            } else if (Aij < 0) {
                for (size_t k = 0; k < numLoops; ++k) {
                    lA(k, l) = A(k, j);
                }
                lB[l++] = b[j];
            }
        }
    }

    void categorizeBounds(const Matrix<intptr_t, 0, 0, 0> &A,
                          const llvm::SmallVectorImpl<P> &b, size_t i) {

        categorizeBounds(lowerA[i], upperA[i], lowerb[i], upperb[i], A, b, i);
    }
    static void appendBounds(const Matrix<intptr_t, 0, 0, 0> &lA,
                             const Matrix<intptr_t, 0, 0, 0> &uA,
                             const llvm::SmallVectorImpl<P> &lB,
                             const llvm::SmallVectorImpl<P> &uB,
                             Matrix<intptr_t, 0, 0, 0> &A,
                             llvm::SmallVectorImpl<P> &b, size_t i) {
        const size_t numNeg = lB.size();
        const size_t numPos = uB.size();
#ifdef EXPENSIVEASSERTS
        for (auto &lb : lB) {
            if (auto c = lb.getCompileTimeConstant()) {
                if (c.getValue() == 0) {
                    assert(lb.terms.size() == 0);
                }
            }
        }
#endif
        auto [numLoops, numCol] = A.size();
        A.resize(numLoops, numCol + numNeg * numPos);
        b.resize_for_overwrite(numCol + numNeg * numPos);
        size_t c = numCol;
        for (size_t l = 0; l < numNeg; ++l) {
            for (size_t u = 0; u < numPos; ++u) {
                c += setBounds(A.getCol(c), b[c], lA.getCol(l), lB[l],
                               uA.getCol(u), uB[u], i);
            }
        }
        A.resize(numLoops, c);
        b.resize(c);
    }
    void appendBounds(Matrix<intptr_t, 0, 0, 0> &A, llvm::SmallVectorImpl<P> &b,
                      size_t i) {

        appendBounds(lowerA[i], upperA[i], lowerb[i], upperb[i], A, b, i);
    }
    void pruneBounds(){
	for (size_t i = 0; i < getNumConstraints(); ++i){
	    pruneBounds(A, b, i);
	}
    }
    // removes bounds on `i` that are redundant.
    void pruneBounds(Matrix<intptr_t, 0, 0, 0> &A, llvm::SmallVector<P, 8> &b,
                     const size_t i) const {

        const auto [numNeg, numPos] = countNonZeroSign(A, i);
        if ((numNeg > 1) | (numPos > 1)) {
            pruneBounds(A, b, i, numNeg, numPos);
        }
    }
    // removes bounds on `i` that are redundant.
    void pruneBounds(Matrix<intptr_t, 0, 0, 0> &Aold,
                     llvm::SmallVector<P, 8> &bold, const size_t i,
                     const size_t numNeg, const size_t numPos) const {

        const size_t numVarBase = getNumVar();
        const size_t numCol = Aold.size(1);
        // if we have multiple bounds, we try to prune
        // First, do we have dependencies in these bounds that must be
        // eliminated?
        intptr_t dependencyToEliminate = -1;
        for (size_t j = 0; j < numCol; ++j) {
            intptr_t Aij = Aold(i, j);
            if (Aij) {
                for (size_t k = 0; k < numVarBase; ++k) {
                    intptr_t Akj = Aold(k, j);
                    bool depends = ((Akj != 0) & (k != i));
                    dependencyToEliminate =
                        (depends ? k : dependencyToEliminate);
                }
                if (dependencyToEliminate != -1) {
                    break;
                }
            }
        }
	// we may have
	// i <= j
	// i <= N // redundant
	// j <= M
	// poset has: M <= N
	// so i <= j <= M <= N
	// renders i <= N redundant
        if (dependencyToEliminate >= 0) {
            // hopefully stack allocate scratch space
            const size_t numAuxiliaryVar = bin2(numNeg) + bin2(numPos);
            const size_t numVar = numVarBase + numAuxiliaryVar;
            const size_t totalCol = numCol + 2 * numAuxiliaryVar;
            Matrix<intptr_t, 0, 0, 128> AOld(numVar, totalCol);
            Matrix<intptr_t, 0, 0, 128> ANew;
            llvm::SmallVector<P, 16> bOld(totalCol);
            llvm::SmallVector<P, 16> bNew;
            // simple mapping of `k` to particular bounds
            llvm::SmallVector<std::pair<unsigned, unsigned>, 16> boundDiffPairs;
            boundDiffPairs.reserve(numAuxiliaryVar);
            size_t c = numCol;
            for (size_t j = 0; j < numCol; ++j) {
                bOld[j] = bold[j];
                for (size_t k = 0; k < numVarBase; ++k) {
                    AOld(k, j) = Aold(k, j);
                }
                bOld[j] = bold[j];
                if (intptr_t Aij = AOld(i, j)) {
                    bool positive = Aij > 0;
                    intptr_t absAij = std::abs(Aij);
                    for (size_t d = j + 1; d < numCol; ++d) {
                        // index Aold as we haven't yet copied d > j to
                        // AOld
                        intptr_t Aid = Aold(i, d);
                        if ((Aid != 0) & ((Aid > 0) == positive)) {
                            intptr_t absAid = std::abs(Aid);
                            // Aij * i <= b_j - a_j*k
                            // Aid * i <= b_d - a_d*k
                            // Aij*abs(Aid) * i <=
                            //        abs(Aid)*b_j - abs(Aid)*a_j*k
                            // abs(Aij)*Aid * i <=
                            //        abs(Aij)*b_d - abs(Aij)*a_d*k
                            //
                            // So, we define bound difference:
                            // bd_jd = (abs(Aid)*b_j - abs(Aid)*a_j*k)
                            //       - (abs(Aij)*b_d - abs(Aij)*a_d*k)
                            //
                            // we now introduce bd_jd as a variable to
                            // our inequalities, by defining both (0)
                            // bd_jd <= (abs(Aid)*b_j - abs(Aid)*a_j*k)
                            //            - (abs(Aij)*b_d -
                            //            abs(Aij)*a_d*k)
                            // (1) bd_jd >= (abs(Aid)*b_j -
                            // abs(Aid)*a_j*k)
                            //            - (abs(Aij)*b_d -
                            //            abs(Aij)*a_d*k)
                            //
                            // These can be rewritten as
                            // (0) bd_jd + abs(Aid)*a_j*k -
                            // abs(Aij)*a_d*k
                            //       <= abs(Aid)*b_j - abs(Aij)*b_d
                            // (1) -bd_jd - abs(Aid)*a_j*k +
                            // abs(Aij)*a_d*k
                            //       <= -abs(Aid)*b_j + abs(Aij)*b_d
                            //
                            // Then, we try to prove that either
                            // bd_jd <= 0, or that bd_jd >= 0
                            //
                            // Note that for Aij>0 (i.e. they're upper
                            // bounds), we want to keep the smaller
                            // bound, as the other will never come into
                            // play. For Aij<0 (i.e., they're lower
                            // bounds), we want the larger bound for the
                            // same reason. However, as solving for `i`
                            // means dividing by a negative number, this
                            // means we're actually looking for the
                            // smaller of `abs(Aid)*b_j -
                            // abs(Aid)*a_j*k` and `abs(Aij)*b_d -
                            // abs(Aij)*a_d*k`. This was the reason for
                            // multiplying by `abs(...)` rather than raw
                            // values, so that we could treat both paths
                            // the same.
                            //
                            // Thus, if bd_jd <= 0, we keep the `j`
                            // bound else if bd_jd >= 0, we keep the `d`
                            // bound else, we must keep both.
                            for (size_t l = 0; l < numVarBase; ++l) {
                                intptr_t Alc =
                                    absAid * AOld(l, j) - absAij * Aold(l, d);
                                AOld(l, c) = Alc;
                                AOld(l, c + 1) = -Alc;
                            }
                            P delta = absAij * bold[d];
                            Polynomial::fnmadd(delta, bOld[j], absAid);
                            bOld[c] = -delta;
                            bOld[c + 1] = std::move(delta);
                            size_t newVarId =
                                numVarBase + boundDiffPairs.size();
                            AOld(newVarId, c++) = 1;
                            AOld(newVarId, c++) = -1;
                            boundDiffPairs.emplace_back(j, d);
                        }
                    }
                }
            }
            llvm::SmallVector<int8_t, 32> provenBoundsDeltas(numAuxiliaryVar,
                                                             0);
            llvm::SmallVector<unsigned, 32> rowsToErase;
            do {
                eliminateVariable(ANew, bNew, AOld, bOld,
                                  size_t(dependencyToEliminate));
                std::swap(ANew, AOld);
                std::swap(bNew, bOld);
                dependencyToEliminate = -1;
                for (size_t j = 0; j < AOld.size(1); ++j) {
                    for (size_t k = numVarBase; k < numVar; ++k) {
                        if (intptr_t Akj = AOld(k, j)) {
                            bool dependsOnOthers = false;
                            // note that we don't add equations for
                            // comparing the different bounds diffs,
                            // therefore `AOld(l, j) == 0` for all `l !=
                            // k
                            // && l >= numLoops`
                            for (size_t l = 0; l < numVarBase; ++l) {
                                // if ((AOld(l, j) != 0) & (l != i)) {
                                if (AOld(l, j)) {
                                    dependencyToEliminate = l;
                                    dependsOnOthers = true;
                                    break;
                                }
                            }
                            // Akj * boundDelta <= bOld[j]
                            if (!dependsOnOthers) {
                                // we can eliminate this equation, but
                                // check if we can eliminate the bound
                                // if (AOld(i,j) == 0){
                                if (knownLessEqualZero(bOld[j])) {
                                    // boundDelta = b - d
                                    // Akj * boundDelta <= bOld[j]
                                    // (b-d) >= 0 === b >= d === (d-b) <= 0
                                    // -boundDelta <= 0
                                    // boundDelta >= 0
                                    // erase b
                                    //
                                    // (d-b) >= 0 === d >= b === (b-d) <= 0
                                    // boundDelta <= 0
                                    // erase d
                                    provenBoundsDeltas[k - numVarBase] =
                                        Akj > 0 ? -1 : 1;
                                }
                                if ((rowsToErase.size() == 0) ||
                                    (rowsToErase.back() != j)) {
                                    rowsToErase.push_back(j);
                                }
                            }
                        }
                    }
                }
                for (auto it = rowsToErase.rbegin(); it != rowsToErase.rend();
                     ++it) {
                    AOld.eraseRow(*it);
                    bOld.erase(bOld.begin() + (*it));
                }
                rowsToErase.clear();
            } while (dependencyToEliminate >= 0);
            for (size_t l = 0; l < provenBoundsDeltas.size(); ++l) {
                size_t k = provenBoundsDeltas[l];
                // eliminate bound difference k
                // bound difference is j - d
                if (k) {
                    auto [j, d] = boundDiffPairs[l];
                    // if Akj * k >= 0, discard j, else discard d
                    rowsToErase.push_back(k == 1 ? j : d);
                }
            }
            erasePossibleNonUniqueElements(Aold, bold, rowsToErase);

        } else {
            // no dependencyToEliminate
            llvm::SmallVector<unsigned, 32> rowsToErase;
            rowsToErase.reserve(numNeg * numPos);
            for (size_t j = 0; j < numCol - 1; ++j) {
                if (intptr_t Aij = Aold(i, j)) {
                    for (size_t k = j + 1; k < numCol; ++k) {
                        intptr_t Aik = Aold(i, k);
                        if ((Aik != 0) & ((Aik > 0) == (Aij > 0))) {
                            P delta = bold[k] * std::abs(Aij);
                            Polynomial::fnmadd(delta, bold[j], std::abs(Aik));
                            // delta = k - j

                            if (knownGreaterEqualZero(delta)) {
                                rowsToErase.push_back(k);
                            } else if (knownLessEqualZero(std::move(delta))) {
                                rowsToErase.push_back(j);
                            }
                        }
                    }
                }
            }
            erasePossibleNonUniqueElements(Aold, bold, rowsToErase);
        }
    }
    void deleteBounds(Matrix<intptr_t, 0, 0, 0> &A,
                      llvm::SmallVectorImpl<P> &b, size_t i) {
        llvm::SmallVector<unsigned, 16> deleteBounds;
        for (size_t j = 0; j < b.size(); ++j) {
            if (A(i, j)) {
                deleteBounds.push_back(j);
            }
        }
        for (auto it = deleteBounds.rbegin(); it != deleteBounds.rend(); ++it) {
            A.eraseRow(*it);
            b.erase(b.begin() + (*it));
        }
    }
    // A'x <= b
    // removes variable `i` from `A`
    void removeVariable(Matrix<intptr_t, 0, 0, 0> &A,
                        llvm::SmallVector<P, 8> &b, const size_t i) {
        pruneBounds(A, b, i);
        categorizeBounds(A, b, i);
        deleteBounds(A, b, i);
        appendBounds(A, b, i);
    }
    void removeVariable(const size_t i){
	removeVariable(A, b, i);
    }
    static void erasePossibleNonUniqueElements(
        Matrix<intptr_t, 0, 0, 0> &A, llvm::SmallVectorImpl<P> &b,
        llvm::SmallVectorImpl<unsigned> &rowsToErase) {
        std::ranges::sort(rowsToErase);
        for (auto it = std::unique(rowsToErase.begin(), rowsToErase.end());
             it != rowsToErase.begin();) {
            --it;
            A.eraseRow(*it);
            b.erase(b.begin() + (*it));
        }
    }

    void printLowerBound(std::ostream &os, size_t i) const {

        auto &lb = lowerb[i];
        auto &lA = lowerA[i];
        for (size_t j = 0; j < lb.size(); ++j) {
            if (lA(i, j) == -1) {
                os << "i_" << i << " >= ";
            } else {
                os << -lA(i, j) << "*i_" << i << " >= ";
            }
            bool printed = !isZero(lb[j]);
            if (printed) {
                os << -lb[j];
            }
            for (size_t k = 0; k < getNumVar(); ++k) {
                if (k == i) {
                    continue;
                }
                if (intptr_t lakj = lA(k, j)) {
                    if (lakj < 0) {
                        os << " - ";
                    } else if (printed) {
                        os << " + ";
                    }
                    lakj = std::abs(lakj);
                    if (lakj != 1) {
                        os << lakj << "*";
                    }
                    os << "i_" << k;
                    printed = true;
                }
            }
            if (!printed) {
                os << 0;
            }
            os << std::endl;
        }
    }
    void printUpperBound(std::ostream &os, size_t i) const {
        auto &ub = upperb[i];
        auto &uA = upperA[i];
        for (size_t j = 0; j < ub.size(); ++j) {
            if (uA(i, j) == 1) {
                os << "i_" << i << " <= ";
            } else {
                os << uA(i, j) << "*i_" << i << " <= ";
            }
            bool printed = (!isZero(ub[j]));
            if (printed) {
                os << ub[j];
            }
            for (size_t k = 0; k < getNumVar(); ++k) {
                if (k == i) {
                    continue;
                }
                if (intptr_t uakj = uA(k, j)) {
                    if (uakj > 0) {
                        os << " - ";
                    } else if (printed) {
                        os << " + ";
                    }
                    uakj = std::abs(uakj);
                    if (uakj != 1) {
                        os << uakj << "*";
                    }
                    os << "i_" << k;
                    printed = true;
                }
            }
            if (!printed) {
                os << 0;
            }
            os << std::endl;
        }
    }
    // prints in current permutation order.
    // TODO: decide if we want to make AffineLoopNest a `SymbolicPolyhedra`
    // in which case, we have to remove `currentToOriginalPerm`,
    // which menas either change printing, or move prints `<<` into
    // the derived classes.
    friend std::ostream &operator<<(std::ostream &os,
                                    const AbstractPolyhedra<T, P> &alnb) {
        const size_t numLoops = alnb.getNumVar();
        for (size_t _i = 0; _i < numLoops; ++_i) {
            os << "Variable " << _i << " lower bounds: " << std::endl;
            size_t i = alnb.currentToOriginalPerm(_i);
            alnb.printLowerBound(os, i);
            os << "Variable " << _i << " upper bounds: " << std::endl;
            alnb.printUpperBound(os, i);
        }
        return os;
    }
    void dump() const { std::cout << *this; }

    bool constraintAllZero(size_t i){
	for (size_t j = 0; j < getNumVar(); ++j){
	    if (A(j,i)) return false;
	}
	return true;
    }
    bool hasEmptyConstraint(){
	// A'x <= b
	for (size_t i = 0; i < getNumConstraints(); ++i){
	    if (constraintAllZero(i) && knownGreaterEqualZero(-1 - b[i])){
		// if b < 0
		return true;
	    }
	}
	return false;
    }
    bool isEmpty(){
	// inefficient (compared to ILP + Farkas Lemma approach)
	auto copy = *this;
	for (size_t i = 0; i < getNumVar(); ++i){
	    copy.removeVariable(copy.A, copy.b, i);
	    if (copy.hasEmptyConstraint()){
		return true;
	    }
	}
	return false;
    }
    
};

struct IntegerPolyhedra : public AbstractPolyhedra<IntegerPolyhedra, intptr_t> {
    bool knownLessEqualZeroImpl(intptr_t x) const { return x <= 0; }
    bool knownGreaterEqualZeroImpl(intptr_t x) const { return x >= 0; }
    IntegerPolyhedra(Matrix<intptr_t, 0, 0, 0> A,
                     llvm::SmallVector<intptr_t, 8> b)
        : AbstractPolyhedra<IntegerPolyhedra, intptr_t>(std::move(A),
                                                        std::move(b)){};
    intptr_t currentToOriginalPermImpl(size_t i) const { return i; }
};

struct SymbolicPolyhedra : public AbstractPolyhedra<SymbolicPolyhedra, MPoly> {
    PartiallyOrderedSet poset;
    SymbolicPolyhedra(Matrix<intptr_t, 0, 0, 0> A,
                      llvm::SmallVector<MPoly, 8> b, PartiallyOrderedSet poset)
        : AbstractPolyhedra<SymbolicPolyhedra, MPoly>(std::move(A),
                                                      std::move(b)),
          poset(std::move(poset)){};

    bool knownLessEqualZeroImpl(MPoly x) const {
        return poset.knownLessEqualZero(std::move(x));
    }
    bool knownGreaterEqualZeroImpl(const MPoly &x) const {
        return poset.knownGreaterEqualZero(x);
    }
    intptr_t currentToOriginalPermImpl(size_t i) const { return i; }
};
