
add_library(UnitStep MODULE UnitStep.cpp)
add_library(TurboLoop MODULE TurboLoop.cpp)
# add_llvm_pass_plugin(TurboLoopPass TurboLoopPass.cpp)
target_link_libraries(UnitStep LLVM)
target_link_libraries(TurboLoop LLVM)
