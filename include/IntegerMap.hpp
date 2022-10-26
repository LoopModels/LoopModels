#pragma once
#include <cstdint>
#include <llvm/ADT/SmallVector.h>

// IntegerMap imap;
// imap.push(2); // adds mapping 2 -> 1
// imap.push(5); // adds mapping 5 -> 2
// imap.getForward(2) == 1 // true
// imap.getForward(5) == 2 // true
// imap.getForward(7) == -1 // true
// imap.getBackward(0) == -1 // true
// imap.getBackward(1) == 2 // true
// imap.getBackward(2) == 5 // true
// imap.getBackward(7) == -1 // true
//
struct IntegerMap {
    llvm::SmallVector<size_t, 0> forward;
    llvm::SmallVector<size_t, 0> backward;
    size_t push(size_t i) {
        if (forward.size() <= i) {
            forward.resize(i + 1, 0);
        } else if (int64_t j = forward[i]) {
            return j;
        }
        backward.push_back(i);
        size_t j = backward.size();
        forward[i] = j;
        return j;
    }
    // 0 is sentinal for not found
    size_t getForward(size_t i) {
        if (i <= forward.size())
            return forward[i];
        return 0;
    }
    // 1 is sentinal for not found
    int64_t getBackward(size_t j) {
        if (--j <= backward.size())
            return backward[j];
        return -1;
    }
};
