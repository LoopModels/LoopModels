# https://stackoverflow.com/questions/68105648/configure-cmake-to-work-with-homebrew-libraries-instead-system-provided-librarie
set(HOMEBREW_PREFIX
    "/usr/local"
    CACHE PATH "Path to Homebrew installation"
)

# set(CMAKE_C_COMPILER "${HOMEBREW_PREFIX}/bin/gcc-12") set(CMAKE_CXX_COMPILER
# "${HOMEBREW_PREFIX}/bin/g++-12")

set(CMAKE_PREFIX_PATH
    "${HOMEBREW_PREFIX}"
    # These libraries are keg-only and not loaded into the root prefix by default (to avoid
    # clashes).
    "${HOMEBREW_PREFIX}/opt/gcc/lib/gcc/12"
)

list(TRANSFORM CMAKE_PREFIX_PATH APPEND "/include" OUTPUT_VARIABLE
                                                   CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
)
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES "${CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES}")

set(CMAKE_FIND_FRAMEWORK NEVER)
set(CMAKE_FIND_APPBUNDLE NEVER)

set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
