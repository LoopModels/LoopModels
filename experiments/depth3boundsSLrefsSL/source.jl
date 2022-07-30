args = (rand(3, 3, 3),)

function foo(A::Array{T}) where {T}
    for i in 1:size(A, 1)
        for j in 1:size(A, 2)
            for k in 1:size(A, 3) 
                @inbounds A[i-1, j-2, k+8] = A[k, 3*i + 4, j]
            end
        end
    end
    A
end