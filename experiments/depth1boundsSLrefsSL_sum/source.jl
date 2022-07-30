args = (Int[0],)

function foo(a::Vector{T}) where T
    s = zero(T)
    for i in 1:length(a)
        @inbounds s += a[i]
    end
    s
end