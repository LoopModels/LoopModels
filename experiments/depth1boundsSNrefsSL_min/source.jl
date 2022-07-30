args = ([0 1; 2 3;],)

function foo(a::Matrix{T}) where {T}
    for i in 2:min(size(a, 1), size(a, 2))
        @inbounds a[i, i] = a[i, i] - a[i-1, i-1]
    end
    a
end
