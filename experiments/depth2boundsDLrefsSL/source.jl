args = ([1 2; 3 4;], )

function foo(A::Array{T}) where {T}
    for i in 1:size(A, 1)-9
        for j in i+1:size(A, 2)-4
            @inbounds A[i, j] = A[i, j] - A[i, i]
        end
    end
    A
end