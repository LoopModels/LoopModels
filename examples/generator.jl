

function optimizedloop(n)
    s = 0
    for i in 1:n
        s += i
    end
    s
end

function loop(a::Vector{T}) where T
    s = zero(T)
    for i in 1:length(a)
        s += a[i]
    end
    s
end

function loopstore!(a::Vector{T}, val::T) where T
    for i in 1:length(a)
        a[i] = val
    end
end

function dot(x::Vector{T}, y::Vector{T}) where {T}
    @assert length(x) == length(y)
    s = zero(T)
    for i in 1:length(x)
        s += x[i]*y[i]
    end
    s
end

function twoloops(default, a)
    s = default
    half = div(length(a), 2)
    for i in 1:half
        s += a[i]
    end
    for j in half+1:length(a)
        s += a[j]
    end
    s
end

function nestedloop_1!(a::Vector{Vector{T}}, c::T) where {T}
    for i in 1:length(a)
        for j in 1:length(a[i])
            a[i][j] *= c
        end
    end
end

function nestedloop_2!(a::Matrix{T}, c::T, k::Int) where {T}
    for i in 1:size(a, 1)
        for j in 1:size(a, 2)
            @inbounds a[i, j] = a[k*i + 1, 3*j + 4] + c
        end
    end
    return a
end

function minBounds(a::Matrix{T}) where {T}
    for i in 2:min(size(a, 1), size(a, 2))
        @inbounds a[i, i] = a[i, i] - a[i - 1, i]
        @inbounds a[i, i] = a[i, i] - a[i, i - 1]
    end
    return a
end

function minMaxBounds(a::Matrix{T}) where {T}
    m, n = size(a)
    for i in min(m, n):max(m, n)
        @inbounds a[i, i] = a[i, i] - a[i - 1, i]
        @inbounds a[i, i] = a[i, i] - a[i, i - 1]
    end
    return a
end

#                             %0            %1  %2  %3  %4
# function symnestedloop_1!(A::Matrix{T}, a1, a2, k1, k2) where {T}
function symnestedloop_1!(A::Matrix{T}, a1, a2, k1, k2) where {T}
    for i in a1:size(A, 1)
        for j in a2:size(A, 2)
            @inbounds A[i, j] = A[i - k1, j + k2]
        end
    end
    return A
end

#                             %0            %1  %2  %3  %4  %5  %6
# function symnestedloop_2!(A::Matrix{T}, a1, a2, b1, b2, k1, k2) where {T}
function symnestedloop_2!(A::Matrix{T}, a1, a2, b1, b2, k1, k2) where {T}
    for i in a1:b2
        for j in a2:b2
            @inbounds A[i, j] = A[i - k1, j + k2]
        end
    end
    return A
end

function write_code_llvm(f, args...; filename = nothing, passes = "default<Oz>,function(simplifycfg,instcombine,early-cse,loop-rotate,lcssa,indvars)")
    path, io = mktemp()
    code_llvm(io, f, map(typeof, args); 
                    raw = true, 
                    dump_module = true, 
                    optimize = false, 
                    debuginfo = :none)
    close(io)
    newfilename = filename === nothing ? path * ".ll" : filename
    mv(path, newfilename, force=true)
    run(`opt --passes="$passes" $newfilename -S -o $newfilename`)
    return newfilename
end

write_code_llvm(optimizedloop, 2, filename="optimizedloop.ll")
write_code_llvm(loop, [2], filename="loop.ll")
write_code_llvm(loopstore!, [2], 5, filename="loopstore.ll")
write_code_llvm(dot, [1], [2], filename="dot.ll")
write_code_llvm(twoloops, 0, [1, 5, 17], filename="twoloops.ll")
write_code_llvm(nestedloop_1!, [Int[1]], 5, filename="nestedloop_1.ll")
write_code_llvm(symnestedloop_1!, [1 2; 3 4;], 1, 1, 1, 1, filename="symnestedloop_1.ll")
write_code_llvm(symnestedloop_2!, [1 2; 3 4;], 1, 1, 2, 2, 1, 1, filename="symnestedloop_2.ll")