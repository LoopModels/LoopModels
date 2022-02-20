// struct Function {
//     llvm::SmallVector<Term> terms;
//     llvm::SmallVector<TriangularLoopNest, 0> triln;
//     llvm::SmallVector<RectangularLoopNest, 0> rectln;
//     // Vector<Array, 0> arrays;
//     // Vector<ArrayRefStrides, 0> arrayRefStrides;
//     llvm::SmallVector<ArrayRef, 0> arrayRefs;
//     llvm::SmallVector<Const> constants;
//     llvm::SmallVector<bool> visited;
//     IndexTree initialLoopTree;
//     // Vector<Schedule, 0> bestschedules;
//     // Matrix<Schedule, 0, 0> tempschedules;
//     Matrix<double, 0, 0> tempcosts;
//     FastCostSummaries fastcostsum;
//     Vector<Vector<Int, 0>, 0> triloopcache;
//     llvm::SmallVector<llvm::SmallVector<std::pair<size_t, size_t>>>
//         arrayReadsToTermMap;
//     llvm::SmallVector<llvm::SmallVector<std::pair<size_t, size_t>>>
//         arrayWritesToTermMap;
//     llvm::SmallVector<ValueRange>
//         rangeMap; // vector of length num_program_variables
//     llvm::SmallVector<llvm::SmallVector<ValueRange>>
//         diffMap; // comparisons, e.g. x - y via orderMap[x][y]; maybe we don't
//                  // know anything about `x` or `y` individually, but do know `x
//                  // - y > 0`.
//     size_t ne;
//     // char *data;
// 
//     /*
//       // Makes more sense to initialize empty and take builder approach.
//     Function(Vector<Term, 0> terms, Vector<TriangularLoopNest, 0> triln,
//              Vector<RectangularLoopNest, 0> rectln, // Vector<Array, 0> arrays,
//              // Vector<ArrayRefStrides, 0> arrayRefStrides,
//              Vector<ArrayRef, 0> arrayRefs, Vector<Const, 0> constants,
//              Vector<bool, 0> visited, IndexTree initialLoopTree,
//              // Vector<Schedule, 0> bestschedules,
//              // Matrix<Schedule, 0, 0> tempschedules,
//              Matrix<double, 0, 0> tempcosts, FastCostSummaries fastcostsum,
//              Vector<Vector<Int, 0>, 0> triloopcache,
//              size_t numArrays) // FIXME: triloopcache type
//         : terms(terms), triln(triln),
//           rectln(rectln), // arrays(arrays),
//                           // arrayRefStrides(arrayRefStrides),
//           arrayRefs(arrayRefs), constants(constants), visited(visited),
//           initialLoopTree(initialLoopTree),
//           // bestschedules(bestschedules),
//           // tempschedules(tempschedules),
//           tempcosts(tempcosts), fastcostsum(fastcostsum),
//           triloopcache(triloopcache) {
//         size_t edge_count = 0;
//         for (size_t j = 0; j < length(terms); ++j)
//             edge_count += length(terms(j).dsts);
//         ne = edge_count;
//         for (size_t j = 0; j < length(triloopcache); ++j) {
//             Vector<Int, 0> trlc = triloopcache(j);
//             for (size_t k = 0; k < length(trlc); ++k) {
//                 trlc(k) = UNSET_COST;
//             }
//         }
//         arrayReadsToTermMap.resize(numArrays);
//         arrayWritesToTermMap.resize(numArrays);
//     }
//     */
// };
// 
// ValueRange valueRange(Function const &fun, size_t id) {
//     return fun.rangeMap[id];
// }
// template <typename C>
// ValueRange
// valueRange(Function const &fun,
//            Polynomial::MultivariateTerm<C, Polynomial::Monomial> const &x) {
//     ValueRange p = ValueRange(x.coefficient);
//     for (auto it = x.cbegin(); it != x.cend(); ++it) {
//         p *= fun.rangeMap[*it];
//     }
//     return p;
// }
// template <typename C>
// ValueRange
// valueRange(Function const &fun,
//            Polynomial::Multivariate<C, Polynomial::Monomial> const &x) {
//     ValueRange a(0);
//     for (auto it = x.cbegin(); it != x.cend(); ++it) {
//         a += valueRange(fun, *it);
//     }
//     return a;
// }
// 
// Order cmpZero(intptr_t x) {
//     return x ? (x > 0 ? GreaterThan : LessThan) : EqualTo;
// }
// Order cmpZero(Function &fun, size_t id) {
//     return valueRange(fun, id).compare(0);
// }
// template <typename C>
// Order cmpZero(Function &fun,
//               Polynomial::MultivariateTerm<C, Polynomial::Monomial> &x) {
//     return valueRange(fun, x).compare(0);
// };
// 
// // std::pair<ArrayRef, ArrayRefStrides> getArrayRef(Function fun, size_t id) {
// //     ArrayRef ar = fun.arrayRefs[id];
// //     ArrayRefStrides ars = fun.arrayRefStrides[ar.strideId];
// //     return std::make_pair(ar, ars);
// // };
// inline ArrayRef &getArrayRef(Function &fun, size_t id) {
//     return fun.arrayRefs[id];
// };
// 
// UpperBounds &getUpperBounds(Function &fun, ArrayRef &ar) {
//     size_t triID = upperHalf(ar.arrayID);
//     if (triID) {
//         return fun.triln[triID].getUpperbounds();
//     } else {
//         return fun.rectln[ar.arrayID].getUpperbounds();
//     }
// }
// UpperBounds &fillUpperBounds(Function &fun, ArrayRef &ar) {
//     size_t triID = upperHalf(ar.arrayID);
//     if (triID) {
//         TriangularLoopNest &tri = fun.triln[triID];
//         tri.fillUpperBounds();
//         return tri.getUpperbounds();
//     } else {
//         return fun.rectln[ar.arrayID].getUpperbounds();
//     }
// }
// 
// void partitionArrayStrides(Function &fun, ArrayRef &ar) {
//     // NOTE: this will be the firstg call, so we fill ehre.
//     UpperBounds &upperBounds = fillUpperBounds(fun, ar);
//     size_t numInds = ar.inds.size();
//     /*
//     llvm::SmallVector<uint32_t> indDegrees(numInds, uint32_t(0));
// 
//     for (size_t i = 0; i < numInds; ++i){
//         indDegrees[i] = std::get<0>(ar.inds[i]).degree();
//     }
//     */
//     size_t maxDegreeInd = 0;
//     size_t maxDegree = 0;
//     for (size_t i = 0; i < numInds; ++i) {
//         size_t d = std::get<0>(ar.inds[i]).degree();
//         if (d > maxDegree) {
//             maxDegree = d;
//             maxDegreeInd = i;
//         }
//     }
// }
// 
// // Array getArray(Function fun, ArrayRef ar) { return fun.arrays(ar.arrayid); }
// 
// void clearVisited(Function &fun) {
//     for (size_t j = 0; j < length(fun.visited); ++j) {
//         fun.visited[j] = false;
//     }
// }
// bool visited(Function &fun, size_t i) { return fun.visited[i]; }
// size_t nv(Function &fun) { return length(fun.terms); }
// size_t ne(Function &fun) { return fun.ne; }
// Vector<std::pair<size_t, VarType>, 0> outNeighbors(Term &t) {
//     return t.dsts;
// }
// Vector<std::pair<size_t, VarType>, 0> outNeighbors(Function &fun, size_t i) {
//     return outNeighbors(fun.terms[i]);
// }
// Vector<std::pair<size_t, VarType>, 0> inNeighbors(Term &t) { return t.srcs; }
// Vector<std::pair<size_t, VarType>, 0> inNeighbors(Function &fun, size_t i) {
//     return inNeighbors(fun.terms[i]);
// }
// 
// Term &getTerm(Function &fun, size_t tidx) { return fun.terms[tidx]; }
// 
// struct TermBundle {
//     // llvm::SmallVector<size_t> termIDs;
//     BitSet termIds;
//     BitSet srcTerms;
//     BitSet dstTerms;
//     BitSet srcTermsDirect;
//     BitSet dstTermsDirect;
//     BitSet loads;  // arrayRef ids
//     BitSet stores; // arrayRef ids
//     // llvm::SmallVector<CostSummary> costSummary; // vector of
//     // length(numLoopDeps);
//     BitSet srcTermBundles; // termBundles
//     BitSet dstTermBundles; // termBundles
// 
//     llvm::SmallSet<size_t, 4> RTWs;
//     llvm::SmallSet<size_t, 4> WTRs;
//     CostSummary costSummary;
//     TermBundle(size_t maxTerms, size_t maxArrayRefs, size_t maxTermBundles)
//         : termIds(BitSet(maxTerms)), srcTerms(BitSet(maxTerms)),
//           dstTerms(BitSet(maxTerms)), srcTermsDirect(BitSet(maxTerms)),
//           dstTermsDirect(BitSet(maxTerms)), loads(BitSet(maxArrayRefs)),
//           stores(BitSet(maxArrayRefs)), srcTermBundles(BitSet(maxTermBundles)),
//           dstTermBundles(BitSet(maxTermBundles)) {}
// };
// 
// // inline uint32_t lowerQuarter(uint32_t x) { return x & 0x000000ff; }
// // inline uint64_t lowerQuarter(uint64_t x) { return x & 0x000000000000ffff; }
// // inline uint32_t upperHalf(uint32_t x) { return x & 0xffff0000; }
// // inline uint64_t upperHalf(uint64_t x) { return x & 0xffffffff00000000; }
// 
// void push(TermBundle &tb, llvm::SmallVector<size_t> &termToTermBundle,
//           Function &fun, size_t idx, size_t tbId) {
//     termToTermBundle[idx] = tbId;
//     Term t = fun.terms[idx];
//     push(tb.termIds, idx);
//     // MEMORY, TERM, CONSTANT, LOOPINDUCTVAR, WTR, RTW
//     // Here, we fill out srcTerms and dstTerms; only later once all Term <->
//     // TermBundle mappings are known do we fill srcTermbundles/dstTermbundles
//     for (size_t i = 0; i < length(t.srcs); ++i) {
//         auto [srcId, srcTyp] = t.srcs[i];
//         switch (srcTyp) {
//         case VarType::Memory:
//             push(tb.loads, srcId);
//             break;
//         case VarType::Term:
//             push(tb.srcTerms, srcId);
//             push(tb.srcTermsDirect, srcId);
//             break;
//         // case WTR: // this is the read, so write is src
//         //     push(tb.srcTerms, lowerHalf(srcId));
//         //     push(tb.loads, secondQuarter(srcId));
//         //     tb.WTRs.insert(srcId);
//         //     break;
//         // case RTW: // this is the read, so write is dst
//         //     push(tb.dstTerms, lowerHalf(srcId));
//         //     push(tb.loads, firstQuarter(srcId));
//         //     // tb.RTWs.insert(srcId);
//         default:
//             break;
//         }
//     }
//     for (size_t i = 0; i < length(t.dsts); ++i) {
//         auto [dstId, dstTyp] = t.dsts[i];
//         switch (dstTyp) {
//         case VarType::Memory:
//             push(tb.stores, dstId);
//             break;
//         case VarType::Term:
//             push(tb.dstTerms, dstId);
//             push(tb.dstTermsDirect, dstId);
//             break;
//         // case WTR: // this is the write, so read is dst
//         //     push(tb.dstTerms, lowerHalf(dstId));
//         //     push(tb.stores, firstQuarter(dstId));
//         //     break;
//         // case RTW: // this is the write, so read is src
//         //     push(tb.srcTerms, lowerHalf(dstId));
//         //     push(tb.stores, secondQuarter(dstId));
//         //     tb.RTWs.insert(dstId);
//         //     break;
//         default:
//             break;
//         }
//     }
//     tb.costSummary += t.costSummary;
// }
// 
// void fillDependencies(TermBundle &tb, size_t tbId,
//                       llvm::SmallVector<size_t> &termToTermBundle) {
//     for (auto I = tb.srcTerms.begin(); I != tb.srcTerms.end(); ++I) {
//         size_t srcId = termToTermBundle[*I];
//         if (srcId != tbId)
//             push(tb.srcTermBundles, srcId);
//     }
//     for (auto I = tb.dstTerms.begin(); I != tb.dstTerms.end(); ++I) {
//         size_t dstId = termToTermBundle[*I];
//         if (dstId != tbId)
//             push(tb.dstTermBundles, dstId);
//     }
// }
// 
// BitSet &outNeighbors(TermBundle &tb) { return tb.dstTermBundles; }
// BitSet &inNeighbors(TermBundle &tb) { return tb.srcTermBundles; }
// 
// // for `shouldPrefuse` to work well, calls should try to roughly follow
// // topological order as best as they can. This is because we rely on inclusion
// // of `Term t` in either the `srcTermsDirect` or `dstTermsDirect. Depending on
// // the order we iterate over the graph, it may be that even though we ultimately
// // append `t` into `srcTermsDirect` or `dstTermsDirect`, it is not yet included
// // at the time we call `shouldPrefuse`.
// //
// // Note that `shouldPrefuse` is used in `prefuse` as an optimization meant to
// // speed up this library by reducing the search space; correctness does not
// // depend on it.
// bool shouldPrefuse(Function &fun, TermBundle &tb, size_t tid) {
//     Term &t = fun.terms[tid];
//     size_t members = length(tb.termIds);
//     if (members) {
//         Term &firstMember = fun.terms[tb.termIds[0]];
//         if (firstMember.loopNestId != t.loopNestId)
//             return false;
//         return contains(tb.srcTermsDirect, tid) |
//                contains(tb.dstTermsDirect, tid);
//     }
//     return true;
// }
// 
// struct TermBundleGraph {
//     llvm::SmallVector<TermBundle, 0> tbs;
//     llvm::SmallVector<size_t>
//         termToTermBundle; // mapping of `Term` to `TermBundle`.
//     // llvm::SmallVector<llvm::SmallVector<bool>> visited;
// 
//     TermBundleGraph(Function &fun, llvm::SmallVector<Int> &wcc)
//         : termToTermBundle(llvm::SmallVector<size_t>(length(fun.terms))) {
//         // iterate over the weakly connected component wcc
//         // it should be roughly topologically sorted,
//         // so just iterate over terms, greedily checking whether each
//         // shouldPrefusees the most recent `TermBundle`. If so, add them to the
//         // previous. If not, allocate a new one and add them to it. Upon
//         // finishing, construct the TermBundleGraph from this collection of
//         // `TermBundle`s, using a `termToTermBundle` map.
//         size_t maxTerms = length(fun.terms);
//         size_t maxArrayRefs = length(fun.arrayRefs);
//         size_t maxTermBundles = length(wcc);
//         for (size_t i = 0; i < wcc.size(); ++i) {
//             bool doPrefuse = i > 0;
//             size_t idx = wcc[i];
//             if (doPrefuse)
//                 doPrefuse = shouldPrefuse(fun, last(tbs), idx);
//             if (!doPrefuse)
//                 tbs.emplace_back(
//                     TermBundle(maxTerms, maxArrayRefs, maxTermBundles));
//             push(last(tbs), termToTermBundle, fun, idx, tbs.size() - 1);
//         }
//         // Now, all members of the wcc have been added.
//         // Thus, `termToTerBmundle` should be full, and we can now fill out the
//         // `srcTermBundles` and `dstTermBundles`.
//         for (size_t i = 0; i < tbs.size(); ++i) {
//             fillDependencies(tbs[i], i, termToTermBundle);
//         }
//     }
// };
// 
// struct WeaklyConnectedComponentOptimizer {
//     TermBundleGraph tbg;
//     // Schedule bestSchedule;
//     // Schedule tempSchedule;
//     llvm::SmallVector<llvm::SmallVector<Int>>
//         stronglyConnectedComponents; // strongly connected components within the
//                                      // weakly connected component
// };
// 
// BitSet &outNeighbors(TermBundleGraph &tbg, size_t tbId) {
//     TermBundle &tb = tbg.tbs[tbId];
//     return outNeighbors(tb);
// }
// BitSet &inNeighbors(TermBundleGraph &tbg, size_t tbId) {
//     TermBundle &tb = tbg.tbs[tbId];
//     return inNeighbors(tb);
// }
// 
// // returns true if `abs(x) < y`
// template <typename C>
// bool absLess(Function const &fun,
//              Polynomial::Multivariate<C, Polynomial::Monomial> const &x,
//              Polynomial::Multivariate<C, Polynomial::Monomial> const &y) {
//     ValueRange delta = valueRange(fun, y - x); // if true, delta.lowerBound >= 0
//     if (delta.lowerBound < 0.0) {
//         return false;
//     }
//     ValueRange sum = valueRange(fun, y + x); // if true, sum.lowerBound >= 0
//     // e.g. `x = [M]; y = [M]`
//     // `delta = 0`, `sum = 2M`
//     return sum.lowerBound >= 0.0;
// }
// 
// // should now be able to WCC |> prefuse |> SCC.
// 
// /*
// void clearVisited(TermBundleGraph &tbg, size_t level) {
//     llvm::SmallVector<bool> &visited = tbg.visited[level];
//     for (size_t i = 0; i < visited.size(); ++i) {
//         visited[i] = false;
//     }
//     return;
// }
// void clearVisited(TermBundleGraph &tbg) { clearVisited(tbg, 0); }
// bool visited(TermBundleGraph &tbg, size_t i, size_t level) {
//     llvm::SmallVector<bool> &visited = tbg.visited[level];
//     return visited[i];
// }
// bool visited(TermBundleGraph &tbg, size_t i) { return visited(tbg, i, 0); }
// 
// void markVisited(TermBundleGraph &tbg, size_t tb, size_t level) {
//     llvm::SmallVector<bool> &visited = tbg.visited[level];
//     visited[tb] = true;
//     return;
// }
// 
// bool allSourcesVisited(TermBundleGraph &tbg, size_t tbId, size_t level) {
//     llvm::SmallVector<bool> &visited = tbg.visited[level];
//     // TermBundle &tb = tbg.tbs[tbId];
//     llvm::SmallVector<size_t> &srcs = inNeighbors(tbg, tbId);
//     bool allVisited = true;
//     for (size_t i = 0; i < srcs.size(); ++i) {
//         allVisited &= visited[srcs[i]];
//     }
//     return allVisited;
// }
// 
// // returns set of all outNeighbors that are covered
// llvm::SmallVector<size_t> getIndexSet(TermBundleGraph &tbg, size_t node,
//                                 size_t level) {
//     llvm::SmallVector<size_t> &dsts = outNeighbors(tbg, node);
//     llvm::SmallVector<size_t> indexSet; // = tbg.indexSets[level];
//     // indexSet.clear();
//     for (size_t i = 0; i < dsts.size(); ++i) {
//         size_t dstId = dsts[i];
//         if (allSourcesVisited(tbg, dstId, level))
//             indexSet.push_back(dstId);
//     }
//     return indexSet;
// }
// 
// VarType sourceType(TermBundleGraph &tbg, size_t srcId, size_t dstId) {
//     TermBundle &dst = tbg.tbs[dstId];
//     BitSet& srcV = inNeighbors(dst);
//     for (size_t i = 0; i < length(srcV); ++i) {
//         if (srcV[i] == srcId) {
//             return dst.srcTyp[i];
//         }
//     }
//     assert("source not found");
//     return TERM;
// }
// */
// 
// // Will probably handle this differently, i.e. check source type, and then
// // only call given .
// /*
// uint32_t compatibleLoops(Function &fun, TermBundleGraph &tbg, size_t srcId,
// size_t dstId, size_t level){ VarType srcTyp = sourceType(tbg, srcId, dstId);
//     switch (srcTyp) {
//     case TERM:
//         // return same loop as srcId
//         return ;
//     case RBW/WBR:
//         // rotation is possible, so return vector of possiblities
//         return compatibleLoops();
//     default:
//         assert("invalid src type");
//     }
// }
// */
// 
// uint32_t getLoopDeps(Function &fun, TermBundle &tb) {
//     Term t = getTerm(fun, tb.termIds[0]);
//     return t.loopDeps;
// }
// 
// // for i in 1:I, j in 1:J;
// //   s = 0.0
// //   for ik in 1:3, jk in 1:3
// //      s += A[i + ik, j + jk] * kern[ik,jk];
// //   end
// //   out[i,j] = s
// // end
// //
// // for i in 1:I, j in 1:J
// //    out[i,j] = x[i,j] + x[i,j-1] + x[i,j+1];
// // end
// //
// // i + M*j     = i + M*j
// // i + M*(j-1) = i + M*j - M
// // i + M*(j+1) = i + M*j + M
// //
// // across the three above, we have x = -1, 0, 1
// // 1 [ 0  1  0  [ 1
// // M   x  0  1    i
// //                j ]
// //
// //
// // we have multiple terms with memory references to the same array (`x`)
// // with the smae arrayid, indTyp, indID, programVariableCombinations
// // We are checking for
// // 1. different offsets, and different
// // 2. first rows of coef.
// //
// 
// /*
// ArrayRef getArrayRef(Function fun, TermBundle tb) {
//     Term t = getTerm(fun, tb.termIDs[0]);
//     return fun.arrayrefs[];
// }
// */
// 
// // Flatten affine term relationships
// // struct AffineRelationship{
// // };
// 
// /*
// bool isContiguousTermIndex(Function fun, Term t, Int mlt, size_t level) {
//     // VarType srct0, srct1;
//     while (true) {
//         switch (t.op) {
//         case ADD:
//             for (size_t i = 0; i < 2; i++) {
//                 switch (t.srct(i)) {
//                 case MEMORY:
//                     ArrayRef ar = fun.arrayrefs(t.src(i));
//                     if (ar.loopnest_to_array_map(level) != 0)
//                         return false;
//                     break;
//                 case TERM:
// 
//                     break;
//                 case CONSTANT:
// 
//                     break;
//                 case LOOPINDUCTVAR:
// 
//                     break;
//                 default:
//                     assert("unreachable");
//                 }
//             }
//             srct1 = t.srct[1];
//             break;
//         case SUB1:
//             t = getsrc(t, 0);
//             mlt *= -1;
//             break;
//         case SUB2:
//             break;
//         case IDENTITY:
//             t = getsrc(t, 0);
//             break;
//         default:
//             return false;
//         }
//     }
// }
// 
// bool isContiguousReference(Function fun, ArrayRef ar, Array a, size_t level) {
//     switch (ar.ind_typ[0]) {
//     case LOOPINDUCTVAR:
//         // contiguous requires:
//         // stride mlt of 1, first dim dense
//         return (ar.mlt_off_ids(0, 0) == 1) & (a.dense_knownStride(0, 0));
//     Case MEMORY:
//         return false;
//     case TERM:
//         // Here, we need to parse terms
//         Term t = getterm(fun, ar, 0);
//         Int mlt = ar.mlt_off_ids(0, 0);
//         return isContiguousTermIndex(fun, t, mlt, level);
//     case CONSTANT:
//         return false;
//     default:
//         assert("unreachable");
//     }
// }
// bool isContiguousReference(Function fun, ArrayRef ar, size_t i) {
//     // loop index `i` must map only to first index
//     if (ar.loopnest_to_array_map[i] != 0x00000001)
//         &&return false;
//     return isContiguousReference(fun, ar, getArray(fun, ar));
// }
// 
// //
// size_t memoryCost(Function fun, ArrayRef ar, size_t v, size_t u1, size_t u2) {
//     Array a = getArray(fun, ar);
//     // __builtin_ctz(n)
//     if (isContiguousReference(ar, a, v)) {
// 
//     } else {
//     }
// }
// */
