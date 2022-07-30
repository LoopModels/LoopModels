args = ([0 1; 2 3;],1,2,)

function foo(a::Matrix{T}, idx1, idx2) where {T}
    for i in idx1:idx2
        @inbounds a[i, i] = a[i, i] * 44
    end
    a
end
