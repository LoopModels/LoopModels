args = ([1 2; 3 4;], 1, 1)

function foo(A::Matrix{T}, k1, k2) where {T}
    for i in 1:size(A, 1)
        for j in 1:size(A, 2)
            @inbounds A[i, j] = A[i - k1, j + k2]
        end
    end
    A
end