args = ([1 2; 3 4;], )

function foo(A::Array{T}) where {T}
    for i in 1:size(A, 1)
        for j in 2:size(A, 2)
            @inbounds A[i, j] = A[i - 1, j - i]
        end
    end
end