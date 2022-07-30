args = (Int[0,1,2,3,4],)

function foo(a::Vector{T}) where T
    s = zero(T)
    for i in 1:length(a)
        @inbounds s += a[3]
    end
    s
end