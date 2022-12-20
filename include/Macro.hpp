#pragma once

#define SHOW(ex) llvm::errs() << #ex << " = " << ex;
#define CSHOW(ex) llvm::errs() << "; " << #ex << " = " << ex;
#define SHOWLN(ex) llvm::errs() << #ex << " = " << ex << "\n";
#define CSHOWLN(ex) llvm::errs() << "; " << #ex << " = " << ex << "\n";
