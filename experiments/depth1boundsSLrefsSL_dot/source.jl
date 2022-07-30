args = (Int[0], Int[0],)

function foo(x::Vector{T}, y::Vector{T}) where {T}
    @assert length(x) == length(y)
    s = zero(T)
    for i in 1:length(x)
        @inbounds s += x[i]*y[i]
    end
    s
end