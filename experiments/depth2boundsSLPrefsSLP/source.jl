args = ([1 2; 3 4;], 1, 1, 1, 1, 1, 1)

function foo(A::Matrix{T}, a1, a2, b1, b2, k1, k2) where {T}
    for i in a1:2*b1
        for j in a2:(4*b2 - 2)
            @inbounds A[i, j] = A[3*i - k1, j + k2 - 1]
        end
    end
    A
end