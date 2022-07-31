args = ([1 2; 3 4;],)

function foo(a::Matrix{T}) where {T}
    s = zero(T)
    for i in 1:size(a, 1)
        for j in 4:size(a, 2)
            @inbounds s += a[min(i, j), max(i, j - 1)*j]
        end
    end
    s
end