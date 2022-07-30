args = ([[1]], 8,)

function foo(a::Vector{Vector{T}}, c::T) where {T}
    for i in 1:length(a)
        for j in 1:length(a[i])
            @inbounds a[i][j] *= c
        end
    end
end