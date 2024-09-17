#ifdef USE_MODULE
module;
#else
#pragma once
#endif

#ifndef USE_MODULE
#include "IR/Users.cxx"
#include "IR/TreeResult.cxx"
#include "IR/Predicate.cxx"
#include "IR/Phi.cxx"
#include "IR/Node.cxx"
#include "LinearProgramming/LoopBlock.cxx"
#include "IR/Instruction.cxx"
#include "Dicts/Dict.cxx"
#include "Polyhedra/Dependence.cxx"
#include "IR/Cache.cxx"
#include "IR/BBPredPath.cxx"
#include "IR/Array.cxx"
#include "Polyhedra/Schedule.cxx"
#include "Polyhedra/Loops.cxx"
#include "IR/Address.cxx"
#else
export module IR;
export import :Address;
export import :AffineLoops;
export import :AffineSchedule;
export import :Array;
export import :BBPredPath;
export import :Cache;
export import :Dependence;
export import :Dict;
export import :Instruction;
export import :LinearProgram;
export import :Node;
export import :Phi;
export import :Predicate;
export import :TreeResult;
export import :Users;
#endif