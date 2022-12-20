#pragma once
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/raw_ostream.h>

template <typename T> void show(llvm::SmallVectorImpl<T> const &x) {
  printVector(llvm::errs(), x);
}
template <typename T>
concept LeftLeftPrint = requires(llvm::raw_ostream &os, const T &a) {
                          { os << a };
                        };
void show(LeftLeftPrint auto x) { llvm::errs() << x; }
void showln(auto x) {
  show(x);
  llvm::errs() << "\n";
}
