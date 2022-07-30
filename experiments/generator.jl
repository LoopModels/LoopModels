
#=
    Writes the llvm IR of the function with signature `f(args...)` into a
    file, and applies several opt passes to clean the IR a bit
=#
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

#=
    Traverses directory `dir` recursively in search of files `sourcefile`,
    which contain the function named `foo`; when such file is found, writes
    its llvm IR to a `llvmirfile` file
=#
function generatellvmir(; 
                        dir = ".", 
                        sourcefile="source.jl", 
                        llvmirfile = "llvmir.ll")
    for (root, dirs, files) in walkdir(dir)
        if sourcefile in files
            sourcepath = joinpath(root, sourcefile)
            @info "Including $sourcepath.."
            include(sourcepath)
            write_code_llvm(foo, args...; 
                            filename=joinpath(root, llvmirfile))
        end
    end
end

generatellvmir(dir=".")