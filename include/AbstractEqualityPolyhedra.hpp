#pragma once

#include "./Polyhedra.hpp"
#include "Constraints.hpp"

template <class P, typename T>
struct AbstractEqualityPolyhedra : public AbstractPolyhedra<P, T> {
    IntMatrix E;
    llvm::SmallVector<T, 8> q;

    using AbstractPolyhedra<P, T>::A;
    using AbstractPolyhedra<P, T>::b;
    using AbstractPolyhedra<P, T>::removeVariable;
    using AbstractPolyhedra<P, T>::getNumVar;
    using AbstractPolyhedra<P, T>::pruneBounds;
    AbstractEqualityPolyhedra(size_t numIneq, size_t numEq, size_t numVar)
        : AbstractPolyhedra<P, T>(numIneq, numVar), E(numEq, numVar),
          q(numEq){};

    AbstractEqualityPolyhedra(IntMatrix A, llvm::SmallVector<T, 8> b,
                              IntMatrix E, llvm::SmallVector<T, 8> q)
        : AbstractPolyhedra<P, T>(std::move(A), std::move(b)), E(std::move(E)),
          q(std::move(q)) {}

    bool isEmpty() const { return (b.size() | q.size()) == 0; }
    size_t getNumEqualityConstraints() const { return E.numRow(); }
    bool pruneBounds() {
        return AbstractPolyhedra<P, T>::pruneBounds(A, b, E, q);
    }
    void removeVariable(const size_t i) {
        AbstractPolyhedra<P, T>::removeVariable(A, b, E, q, i);
    }
    void removeExtraVariables(size_t numVarKeep) {
        ::removeExtraVariables(A, b, E, q, numVarKeep);
        pruneBounds();
    }
    void zeroExtraVariables(size_t numVarKeep) {
        A.truncateCols(numVarKeep);
        E.truncateCols(numVarKeep);
        dropEmptyConstraints();
        pruneBounds();
    }
    void dropEmptyConstraints() {
        ::dropEmptyConstraints(A, b);
        // ::dropEmptyConstraints(E, q);
        divByGCDDropZeros(E, q);
    }
    void removeExtraThenZeroExtraVariables(size_t numNotRemove,
                                           size_t numVarKeep) {
        ::removeExtraVariables(A, b, E, q, numNotRemove);
        A.truncateCols(numVarKeep);
        E.truncateCols(numVarKeep);
        dropEmptyConstraints();
        pruneBounds();
    }
    friend std::ostream &operator<<(std::ostream &os,
                                    const AbstractEqualityPolyhedra<P, T> &p) {
        return printConstraints(printConstraints(os, p.A, p.b, true), p.E, p.q,
                                false);
    }
};

struct IntegerEqPolyhedra
    : public AbstractEqualityPolyhedra<IntegerEqPolyhedra, int64_t> {

    IntegerEqPolyhedra(IntMatrix A, llvm::SmallVector<int64_t, 8> b,
                       IntMatrix E, llvm::SmallVector<int64_t, 8> q)
        : AbstractEqualityPolyhedra<IntegerEqPolyhedra, int64_t>(
              std::move(A), std::move(b), std::move(E), std::move(q)){};
    IntegerEqPolyhedra(size_t numIneq, size_t numEq, size_t numVar)
        : AbstractEqualityPolyhedra<IntegerEqPolyhedra, int64_t>(numIneq, numEq,
                                                                 numVar){};
    bool knownLessEqualZeroImpl(int64_t x) const { return x <= 0; }
    bool knownGreaterEqualZeroImpl(int64_t x) const { return x >= 0; }
};
struct SymbolicEqPolyhedra
    : public AbstractEqualityPolyhedra<SymbolicEqPolyhedra, MPoly> {
    PartiallyOrderedSet poset;

    SymbolicEqPolyhedra(IntMatrix A, llvm::SmallVector<MPoly, 8> b, IntMatrix E,
                        llvm::SmallVector<MPoly, 8> q,
                        PartiallyOrderedSet poset)
        : AbstractEqualityPolyhedra(std::move(A), std::move(b), std::move(E),
                                    std::move(q)),
          poset(std::move(poset)){};
    bool knownLessEqualZeroImpl(MPoly x) const {
        return poset.knownLessEqualZero(std::move(x));
    }
    bool knownGreaterEqualZeroImpl(const MPoly &x) const {
        return poset.knownGreaterEqualZero(x);
    }
};
