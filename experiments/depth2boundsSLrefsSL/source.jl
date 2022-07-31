args = ([1 2; 3 4;], 8,)

#            %0            %1
function foo(a::Matrix{T}, c::T) where {T}
    #=
        size(a, 1)  --  %7
        size(a, 2)  --  %10

        i   --  %value_phi3
        j   --  %value_phi8  
    =#
    for i in 1:size(a, 1)
        for j in 1:size(a, 2)
            @inbounds a[i + 1, j] = a[i, 2*j + 1] + c
        end
    end
    a
end