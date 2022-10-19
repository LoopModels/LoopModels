#pragma once
#include "./VarTypes.hpp"
#include <cstdint>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/ValueMap.h>

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
struct ValueToVarMap {
    // llvm::ValueMap<llvm::Value *, size_t> forward;
    llvm::DenseMap<llvm::Value *, VarID> forward;
    llvm::SmallVector<llvm::Value *, 0> backward;
    VarID pushNewValue(llvm::Value *i) {
        backward.push_back(i);
        uint32_t j = backward.size();
        forward.insert(std::make_pair(i, VarID{j}));
        return j;
    }
    VarID push(llvm::Value *i) {
	auto it = forward.find(i);
	if (it != forward.end())
	    return it->second;
	return pushNewValue(i);
    }
    // 0 is sentinal value for not found
    llvm::Optional<VarID> getForward(llvm::Value *i) {
	auto it = forward.find(i);
	if (it != forward.end())
	    return it->second;
	return {};
    }
    // nullptr is sentinal value for not found
    llvm::Value *getBackward(VarID vid) {
	// if (vid.isParam() && (vid.id < backward.size()))
	if (vid.id < backward.size())
	    return backward[vid.id];
        return nullptr;
    }
};
