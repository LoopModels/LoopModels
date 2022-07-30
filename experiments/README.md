
### Experiments

The directory contains the sources of experiments with the LoopModels frontend.

Subdirectories contain examples in Julia; one subdirectory -- one function in the `source.jl` file. The subdirectory name specifies the depth of the loop, the type of loop bounds and array references used in the example. Precisely, the format is

`depth{A}bounds{BC}refs{BC}_{comment}`

where 
    
- `A` is a single number, the depth of nested loops

- `B` is a single letter, the expression dynamics. *S* for *static*, *D* for *dynamic*

- `C` is a single letter, the expression type. *C* for *const*, *L* for *linear*, *N* for *nonlinear*

- Occasionally, letter *P* (for *parametric*) is used to indicate that the expression contains parameters other than array sizes and induction variables 

For example, `depth2boundsSLrefsDLP` will contain a function with a loop of *depth 2*, *static linear* loop bounds, and *dynamic linear parametric* array accesses.

