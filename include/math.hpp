// We'll follow Julia style, so anything that's not a constructor, destructor,
// nor an operator will be outside of the struct/class.
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdint>


const size_t MAX_NUM_LOOPS = 16;
const size_t MAX_PROGRAM_VARIABLES = 32;
typedef int32_t Int;

template <typename T, size_t M> struct Vector {
    static constexpr size_t D = !M;

    T* ptr;
    const std::array<size_t, D> dims;

    Vector(T *ptr, const std::array<size_t, D> dims) : ptr(ptr), dims(dims) {};

    T &operator()(size_t i) { return ptr[i]; }
};

template <typename T, size_t M>
size_t length(Vector<T, M> v) {return (M == 0) ? v.dims[0] : M;}

template <typename T, size_t M>
void show(Vector<T, M> v) {
    for (size_t i = 0; i < length(v); i++) {
        std::printf("%17d", v(i));
    }
    std::printf("\n");
}

template <typename T, size_t M, size_t N> struct Matrix {
    static constexpr size_t D = (!M + !N);

    T *ptr;
    const std::array<size_t, D> dims;

    Matrix(T *ptr, const std::array<size_t, D> dims) : ptr(ptr), dims(dims){};

    T &operator()(size_t i, size_t j) { return ptr[i + j * size((*this), 0)]; }
};

template<typename T, size_t M, size_t N>
size_t size(Matrix<T, M, N> A, size_t i) {
    static constexpr size_t D = (!M + !N);
    if (i == 0) {
        return (M != 0) ? M : A.dims[0];
    } else {
        return (N != 0) ? N : A.dims[D - 1];
    }
}

template<typename T, size_t M, size_t N>
Vector<T, M> getCol(Matrix<T, M, N> A, size_t i) {
    return Vector<T, M>(A.ptr + i * size(A, 0), std::array<size_t, 0>{{}});
}
template<typename T, size_t N>
Vector<T, 0> getCol(Matrix<T, 0, N> A, size_t i) {
    auto s1 = size(A, 0);
    return Vector<T, 0>(A.ptr + i * s1, std::array<size_t, 1>{{s1}});
}

template<typename T, size_t M, size_t N>
void show(Matrix<T, M, N> A) {
    for (size_t i = 0; i < size(A, 0); i++) {
        for (size_t j = 0; j < size(A, 1); j++) {
            std::printf("%17d", A(i, j));
        }
        std::printf("\n");
    }
}

template <typename T> size_t getNLoops(T x) { return x.data.dims[0]; }

typedef Matrix<Int, 0, 2> PermutationData;
typedef Vector<Int, 0> PermutationVector;
struct Permutation {
    PermutationData data;

    Permutation(Int *ptr, size_t nloops)
        : data(PermutationData(ptr, std::array<size_t, 1>{{nloops}})) {
        assert(nloops <= MAX_NUM_LOOPS);
    };

    Int &operator()(size_t i) { return data(i, 0); }
};

PermutationVector inv(Permutation p) {
    return getCol(p.data, 1);
}

Int& inv(Permutation p, size_t j) {
    return p.data(j, 1);
}

Permutation init(Permutation p) {
    Int numloops = getNLoops(p);
    for (Int n = 0; n < numloops; n++) {
        p(n) = n;
        inv(p, n) = n;
    }
    return p;
}

void show(Permutation p) {
    auto numloop = getNLoops(p);
    std::printf("perm: <");
    for (Int j = 0; j < numloop - 1; j++)
        std::printf("%d ", p(j));
    std::printf("%d>\n", p(numloop - 1));
}

void swap(Permutation p, Int i, Int j) {
    Int xi = p(i);
    Int xj = p(j);
    p(i) = xj;
    p(j) = xi;
    inv(p, xj) = i;
    inv(p, xi) = j;
}

struct PermutationSubset {
    Permutation p;
    Int subset_size;
    Int num_interior;
};

struct PermutationLevelIterator {
    Permutation permobj;
    Int level;
    Int offset;

    PermutationLevelIterator(Permutation permobj, Int lv, Int num_interior)
        : permobj(permobj) {
        Int nloops = getNLoops(permobj);
        level = nloops - num_interior - lv;
        offset = nloops - num_interior;
    };

    PermutationLevelIterator(PermutationSubset ps) : permobj(ps.p) {
        auto lv = ps.subset_size + 1;
        Int num_exterior = getNLoops(ps.p) - ps.num_interior;
        Int num_interior = (level >= num_exterior) ? 0 : ps.num_interior;
        level = getNLoops(ps.p) - num_interior - lv;
        offset = getNLoops(ps.p) - num_interior;
    }
};

PermutationSubset initialize_state(PermutationLevelIterator p) {
    Int num_interior = getNLoops(p.permobj) - p.offset;
    return PermutationSubset{.p = p.permobj,
                             .subset_size = p.offset - p.level,
                             .num_interior = num_interior};
}

std::pair<PermutationSubset, bool> advance_state(PermutationLevelIterator p,
                                                 Int i) {
    if (i == 0) {
        auto ps = initialize_state(p);
        return std::make_pair(ps, (i+1) < p.level);
    } else {
        Int k = p.offset - (((p.level & 1) != 0) ? 1 : i);
        swap(p.permobj, p.offset - p.level, k);
        Int num_interior = getNLoops(p.permobj) - p.offset;
        auto ps = PermutationSubset{.p = p.permobj,
                                    .subset_size = p.offset - p.level,
                                    .num_interior = num_interior};
        return std::make_pair(ps, (i + 1) < p.level);
    }
}

typedef Matrix<Int, MAX_PROGRAM_VARIABLES, 0> RektM;

struct RectangularLoopNest {
    RektM data;

    RectangularLoopNest(Int *ptr, size_t nloops)
        : data(RektM(ptr, std::array<size_t, 1>{{nloops}})) {
        assert(nloops <= MAX_NUM_LOOPS);
    };
};

/*
function compatible(l1::TriangularLoopNest, l2::RectangularLoopNest, perm1::Permutation, perm2::Permutation, _i1::Int, _i2::Int = _i1)
  @unpack A, r = l1
  i = perm1[_i1]
  ub1 = getupperbound(r, i)
  ub2 = getupperbound(l2, perm2[_i2])
  Δb = SBVector{32,Int32}(undef)
  for j ∈ eachindex(Δb)
    Δb[j] = ub1[j] - ub2[j]
  end
  # now need to add `A`'s contribution
  iperm = inv(perm1)
  # the first loop adds variables that adjust `i`'s bounds
  for j in SafeCloseOpen(i)
    # Aᵢⱼ = A[i,j]
    Aᵢⱼ = A[j,i] # upper half is reflected
    Aᵢⱼ == zero(Aᵢⱼ) && continue
    j1 = iperm[j]
    j1 < _i1 && return false # j1 < _i1 means it is included in the permutation, but rectangular `l2` definitely does not depend on `j` loop!
    # we have i < C - Aᵢⱼ * j
    if Aᵢⱼ < zero(Aᵢⱼ) # i < C + j*abs(Aᵢⱼ)
      # mx, independent = independent_maximum(A, j, perm)
      # independent || return false
      # TODO: relax restriction
      otherwise_independent(A, j, i) || return false
      ub_temp = getupperbound(r, j)
      for k  eachindex(Δb)
        Δb[k] -= Aᵢⱼ * ub_temp[k]
      end
      Δb[0] += Aᵢⱼ
    else#if Aᵢⱼ > zero(Aᵢⱼ) # i < C - j*abs(Aᵢⱼ)
      # Aᵢⱼ > zero(Aᵢⱼ) means that `j_lower_bounded_by_k` will be false when `k=i`.
      zero_minimum(A, j, j1, perm) || return false
      # otherwise_independent(A, j, i) || return false
    end
  end
  # the second loop here defines additional bounds on `i`
  # If `j` below is in the permutation, we can rule out compatibility with rectangular `l2` loop
  # If it is not in the permutation, then the bound defined by the first loop holds, so no checks/adjustments needed here.
  for j ∈ SafeCloseOpen(i+1, size(A,1))
    Aᵢⱼ = A[j,i]
    Aᵢⱼ == zero(Aᵢⱼ) && continue
    iperm[j] < _i1 && return false # j1 < _i1 means it is included in the permutation, but rectangular `l2` definitely does not depend on `j` loop!
  end
  iszero(Δb[0]) && return all(iszero, Δb)
  ((Δb[0] == Int32(-1)) && all(iszero, Δb[1:end])) || return false
  return zero_inner_iterations_at_maximum(A, ub2, r, i)
end
compatible(r::RectangularLoopNest, t::TriangularLoopNest, perm2::Permutation, perm1::Permutation, _i2, _i1 = _i2) = compatible(t, r, perm1, perm2, _i1, _i2)

# inlined, because we want Δb to be stack allocated.
# maybe just GC preserve the memory instead?
@inline function update_bound_difference!(Δb, l1, A2, perm1, perm2, _i1, i2, flip)
  @unpack A1, r1 = l1
  i1 = perm1[_i1]
  iperm = inv(perm1)
  # the first loop adds variables that adjust `i`'s bounds
  for j in SafeCloseOpen(i1)
    # Aᵢⱼ = A[i,j]
    Aᵢⱼ = A1[j,i1] # upper half is reflected
    Aᵢⱼ == zero(Aᵢⱼ) && continue
    j1 = iperm[j]
    if j1 < _i1
      # j1 < _i1 means it is included in the permutation
      # must check to confirm that A2[j,i] equals
      A2[perm2[j1],i2] ≠ Aᵢⱼ && return false
    end
    # we have i < C - Aᵢⱼ * j
    if Aᵢⱼ < zero(Aᵢⱼ) # i < C + j*abs(Aᵢⱼ)
      # mx, independent = independent_maximum(A, j, perm)
      # independent || return false
      # TODO: relax restriction
      otherwise_independent(A1, j, i1) || return false
      ub_temp = getupperbound(r1, j)
      Aᵢⱼ = ifelse(flip, -Aᵢⱼ, Aᵢⱼ)
      for k ∈ eachindex(Δb)
        Δb[k] -= Aᵢⱼ * ub_temp[k]
      end
      Δb[0] += Aᵢⱼ
    else#if Aᵢⱼ > zero(Aᵢⱼ) # i < C - j*abs(Aᵢⱼ)
      # Aᵢⱼ > zero(Aᵢⱼ) means that `j_lower_bounded_by_k` will be false when `k=i`.
      zero_minimum(A1, j, j1, perm1) || return false
      # otherwise_independent(A, j, i) || return false
    end
  end
  true
end
@inline function check_remaining_bound(l1, A2, perm1, perm2, _i1, i2)
  @unpack A1, r1 = l1
  i1 = perm1[_i1]
  iperm = inv(perm1)
  # the second loop here defines additional bounds on `i`
  # If `j` below is in the permutation, we can rule out compatibility with rectangular `l2` loop
  # If it is not in the permutation, then the bound defined by the first loop holds, so no checks/adjustments needed here.
  for j ∈ SafeCloseOpen(i1+1, size(A1,1))
    Aᵢⱼ = A1[j,i1]
    Aᵢⱼ == zero(Aᵢⱼ) && continue
    j1 = iperm[j]
    if j1 < _i1
      # j1 < _i1 means it is included in the permutation
      # must check to confirm that A2[j,i] equals
      A2[perm2[j1],i2] ≠ Aᵢⱼ && return false
    end
  end
  return true
end

function compatible(l1::TriangularLoopNest, l2::TriangularLoopNest, perm1::Permutation, perm2::Permutation, _i1::Int, _i2::Int = _i1)
  @unpack A1, r1 = l1
  @unpack A2, r2 = l2
  i1 = perm1[_i1]
  i2 = perm2[_i2]
  ub1 = getupperbound(r1, i1)
  ub2 = getupperbound(r2, i2)
  Δb = SBVector{32,Int32}(undef)
  for j ∈ eachindex(Δb)
    Δb[j] = ub1[j] - ub2[j]
  end
  # now need to add `A`'s contribution
  update_bound_difference!(Δb, l1, A2, perm1, perm2, _i1, i2, false) || return false
  update_bound_difference!(Δb, l2, A1, perm2, perm1, _i2, i1, true)  || return false

  check_remaining_bound(l1, A2, perm1, perm2, _i1, i2) || return false
  check_remaining_bound(l2, A1, perm2, perm1, _i2, i1) || return false

  Δb₀ = Δb[0]
  if iszero(Δb₀)
    return all(iszero, Δb)
  elseif Δb₀ == Int32(-1)
    return all(iszero, Δb[1:end]) && zero_inner_iterations_at_maximum(A1, ub2, r1, i1)
  elseif Δb₀ == Int32(1)
    return all(iszero, Δb[1:end]) && zero_inner_iterations_at_maximum(A2, ub1, r2, i2)
  else
    return false
  end
end
*/

/*
bool compatible(RectangularLoopNest l1, RectangularLoopNest l2, Permutation perm1, Permutation perm2, Int i1, Int i2){
    i1 = perm1(i1, 0);
    i2 = perm2(i2, 0);
    for (auto i=0; i < MAX_PROGRAM_VARIABLES; i++) {
        if (l1.data(i, i1) != l2.data(i, i2)) return false;
    }
    return true;
}

typedef Matrix<Int, 0, 0> TrictM;

struct TriangularLoopNest {
    Int *raw;
    size_t nloops;

    TriangularLoopNest(Int *ptr, size_t nloops) : raw(ptr), nloops(nloops) {
        assert(nloops <= MAX_NUM_LOOPS);
    };

    RectangularLoopNest getRekt() {
        return RectangularLoopNest(raw, nloops);
    }

    TrictM getTrit() {
        TrictM A(raw + nloops * MAX_NUM_LOOPS, std::array<size_t, 2>{{nloops,nloops}});
        return A;
    }
};

bool otherwiseIndependent(TrictM A, Int j, Int i) {
    for (auto k = 0; k < j; k++)
        if (!A(k, j)) return false; // A is symmetric
    for (auto k = j+1; k < size(A, 0); k++)
        if (!((k == i) | (A(k, j) == 0))) return false;
    return true;
}

bool zeroMinimum(TrictM A, Int j, Int _j, Permutation perm) {
    for (auto k = j+1; k < size(A, 0); k++) {
        auto j_lower_bounded_by_k = A(k, j) < 0;
        if (!j_lower_bounded_by_k) continue;
        auto _k = perm(k, 1);
        // A[k,j] < 0 means that `k < C + j`, i.e. `j` has a lower bound of `k`
        auto k_in_perm = _k < _j;
        if (k_in_perm) return false;
        // if `k` not in perm, then if `k` has a zero minimum
        // `k` > j`, so it will skip
        if (!zeroMinimum(A, k, _k, perm)) return false;
    }
    return true;
}

bool upperboundDominates(RektM ubi, Int i, RektM ubj, Int j) {
    bool all_le = true;
    for (auto k = 0; k < MAX_PROGRAM_VARIABLES; k++){
        all_le &= (ubi(k,i) >= ubj(k,j));
    }
    return all_le;
}

bool zeroInnerIterationsAtMaximum(TrictM A, RektM ub, RectangularLoopNest r, Int i) {
    for (auto j = 0; j < i; j++) {
        auto Aij = A(i, j);
        if (Aij >= 0) continue;
        if (upperboundDominates(ub, i, r.data, j)) return true;
    }
    for (auto j = i+1; j < size(A, 0); j++) {
        auto Aij = A(i, j);
        if (Aij <= 0) continue;
        if (upperboundDominates(ub, i, r.data, j)) return true;
    }
    return false;
}
*/

/*

function compatible(l1::TriangularLoopNest, l2::RectangularLoopNest, perm1::Permutation, perm2::Permutation, _i1::Int, _i2::Int = _i1)
  @unpack A, r = l1
  i = perm1[_i1]
  ub1 = getupperbound(r, i)
  ub2 = getupperbound(l2, perm2[_i2])
  Δb = SBVector{32,Int32}(undef)
  for j ∈ eachindex(Δb)
    Δb[j] = ub1[j] - ub2[j]
  end
  # now need to add `A`'s contribution
  iperm = inv(perm1)
  # the first loop adds variables that adjust `i`'s bounds
  for j in SafeCloseOpen(i)
    # Aᵢⱼ = A[i,j]
    Aᵢⱼ = A[j,i] # upper half is reflected
    Aᵢⱼ == zero(Aᵢⱼ) && continue
    j1 = iperm[j]
    j1 < _i1 && return false # j1 < _i1 means it is included in the permutation, but rectangular `l2` definitely does not depend on `j` loop!
    # we have i < C - Aᵢⱼ * j
    if Aᵢⱼ < zero(Aᵢⱼ) # i < C + j*abs(Aᵢⱼ)
      # mx, independent = independent_maximum(A, j, perm)
      # independent || return false
      # TODO: relax restriction
      otherwise_independent(A, j, i) || return false
      ub_temp = getupperbound(r, j)
      for k  eachindex(Δb)
        Δb[k] -= Aᵢⱼ * ub_temp[k]
      end
      Δb[0] += Aᵢⱼ
    else#if Aᵢⱼ > zero(Aᵢⱼ) # i < C - j*abs(Aᵢⱼ)
      # Aᵢⱼ > zero(Aᵢⱼ) means that `j_lower_bounded_by_k` will be false when `k=i`.
      zero_minimum(A, j, j1, perm) || return false
      # otherwise_independent(A, j, i) || return false
    end
  end
  # the second loop here defines additional bounds on `i`
  # If `j` below is in the permutation, we can rule out compatibility with rectangular `l2` loop
  # If it is not in the permutation, then the bound defined by the first loop holds, so no checks/adjustments needed here.
  for j ∈ SafeCloseOpen(i+1, size(A,1))
    Aᵢⱼ = A[j,i]
    Aᵢⱼ == zero(Aᵢⱼ) && continue
    iperm[j] < _i1 && return false # j1 < _i1 means it is included in the permutation, but rectangular `l2` definitely does not depend on `j` loop!
  end
  iszero(Δb[0]) && return all(iszero, Δb)
  ((Δb[0] == Int32(-1)) && all(iszero, Δb[1:end])) || return false
  return zero_inner_iterations_at_maximum(A, ub2, r, i)
end
compatible(r::RectangularLoopNest, t::TriangularLoopNest, perm2::Permutation, perm1::Permutation, _i2, _i1 = _i2) = compatible(t, r, perm1, perm2, _i1, _i2)

bool compatible(TriangularLoopNest l1, RectangularLoopNest l2, Permutation perm1, Permutation perm2, Int _i1, Int _i2) {
    auto A = getTrit(l1);
    auto r = getRekt(l1);
    auto i = perm1(_i1, 0);
    return zeroInnerIterationsAtMaximum(A, ub2, r, i)
}
*/
