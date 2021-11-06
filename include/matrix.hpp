#include <cstddef>
#include <cassert>
#include <cstdio>
#include <array>

const size_t MAX_NUM_LOOPS = 16;
const size_t MAX_PROGRAM_VARIABLES = 32;
typedef int32_t Int;

template <class T, size_t M, size_t N>
struct Matrix
{
    static constexpr size_t D = (!M + !N);

    T *content;
    const std::array<size_t, D> dims;

    Matrix(T *content, const std::array<size_t, D> dims) : content(content), dims(dims){};

    size_t getSize(size_t i)
    {
        if (i == 0)
        {
            return (M != 0) ? M : dims[0];
        }
        else
        {
            return (N != 0) ? N : dims[D - 1];
        }
    }

    T &operator()(size_t i, size_t j)
    {
        return content[i + j * getSize(0)];
    }

    void show()
    {
        for (size_t i = 0; i < getSize(0); i++)
        {
            for (size_t j = 0; j < getSize(1); j++)
            {
                std::printf("%17d", (*this)(i, j));
            }
            std::printf("\n");
        }
    }
};

struct RectangularLoopNest
{
    typedef Matrix<Int, MAX_PROGRAM_VARIABLES, 0> M;
    M data;

    RectangularLoopNest(Int *ptr, size_t nloops) : data(M(ptr, std::array<size_t, 1>{{nloops}}))
    {
        assert(nloops <= MAX_NUM_LOOPS);
    };
};

struct Permutation
{
    typedef Matrix<Int, 0, 2> M;
    M data;

    Permutation(Int *ptr, size_t nloops) : data(M(ptr, std::array<size_t, 1>{{nloops}}))
    {
        assert(nloops <= MAX_NUM_LOOPS);
    };

    Int &operator()(size_t i, size_t j)
    {
        return data(i, j);
    }
};

template <typename T>
size_t getNLoops(T x)
{
    return x.data.dims[0];
}

void set_original_order(Permutation p)
{
    Int numloops = getNLoops(p);
    for (Int n = 0; n < numloops; n++)
    {
        p(n, 0) = n;
        p(n, 1) = n;
    }
}

void swap(Permutation p, Int i, Int j)
{
    Int xi = p(i, 0);
    Int xj = p(j, 0);
    p(i, 0) = xj;
    p(j, 0) = xi;
    p(xj, 1) = i;
    p(xi, 1) = j;
}

struct PermutationSubset
{
    Permutation p;
    Int subset_size;
    Int num_interior;
};

struct PermutationLevelIterator
{
    Permutation permobj;
    Int level;
    Int offset;

    PermutationLevelIterator(Permutation permobj, Int lv, Int num_interior) : permobj(permobj) {
        Int nloops = getNLoops(permobj);
        level = nloops - num_interior - lv;
        offset = nloops - num_interior;
    };
};

PermutationSubset initialize_state(PermutationLevelIterator p)
{
    Int num_interior = getNLoops(p.permobj) - p.offset;
    return PermutationSubset{ .p = p.permobj, .subset_size = p.offset - p.level, .num_interior = num_interior };
}

PermutationSubset advance_state(PermutationLevelIterator p, Int i)
{
    if (i == 0){
        return initialize_state(p);
    }
    Int k = p.offset - (((p.level & 1) != 0) ? 1 : i);
    swap(p.permobj, p.offset - p.level, k);
    Int num_interior = getNLoops(p.permobj) - p.offset;
    return PermutationSubset{ .p = p.permobj, .subset_size = p.offset - p.level, .num_interior = num_interior };
}



// bool compatible(RectangularLoopNest l1, RectangularLoopNest l2, Permutation perm1, Permutation perm2, int32_t i1, int32_t i2){

// }
