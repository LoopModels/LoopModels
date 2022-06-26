project('LoopModels', ['c', 'cpp'], version : '0.1', default_options : ['cpp_std=gnu++20'])

llvm_dep = dependency('llvm', version : '>=14.0')
incdir = include_directories('include')

if meson.get_compiler('cpp').get_id() == 'gcc'
  add_global_arguments('-fno-semantic-interposition', language : 'cpp')
endif

llvm_rpath = llvm_dep.get_variable(configtool: 'libdir')
debug_args = ['-Wall', '-Wextra', '-Wpedantic']

# require clang for pch, as clang's pch should be clangd-compatible
if meson.get_compiler('cpp').get_id() == 'clang'
  shared_module('TurboLoop', 'lib/TurboLoop.cpp', dependencies : llvm_dep, include_directories: incdir, cpp_args : debug_args, build_rpath : llvm_rpath, cpp_pch : 'include/pch/pch_tests.hpp')
else
  shared_module('TurboLoop', 'lib/TurboLoop.cpp', dependencies : llvm_dep, include_directories: incdir, cpp_args : debug_args, build_rpath : llvm_rpath)
endif

# TESTS
gtest_dep = dependency('gtest', main : true, required : false)
if gtest_dep.found()
  testdeps = [gtest_dep, llvm_dep]

  test_files = [
    'bitset_test',
    'cost_modeling_test',
    'compat_test',
    #'dependence_test2',
    'dependence_test',
    #'edge_detection_test',
    'ir_test',
    'linear_algebra_test',
    'linear_diophantine_test',
    'matrix_test',
    'normal_form_test',
    'orthogonalize_test',
    'poset_test',
    'symbolics_test',
    'unimodularization_test',
  ]

  foreach f : test_files
    if meson.get_compiler('cpp').get_id() == 'clang'
      test_exe = executable(f, 'test' / f + '.cpp', dependencies : testdeps, include_directories: incdir, cpp_args : debug_args, build_rpath : llvm_rpath, cpp_pch : 'include/pch/pch_tests.hpp')
    else
      test_exe = executable(f, 'test' / f + '.cpp', dependencies : testdeps, include_directories: incdir, cpp_args : debug_args, build_rpath : llvm_rpath)
    endif
    test(f, test_exe)
  endforeach
endif

bench_dep = dependency('benchmark', required : false)

if bench_dep.found()
  benchmark_files = [
    'constraint_pruning_benchmark',
    'polynomial_benchmark'
  ]
  benchmarkdeps = [bench_dep, llvm_dep]
  #bench_args = ['-O3', '-DNDEBUG', '-march=native']
  #bench_args = ['-O3', '-DNDEBUG']
  bench_args = ['-DNDEBUG']
  # https://github.com/mesonbuild/meson/issues/5920
  # TODO: add 'buildtype=release' when issue resolved
  foreach f : benchmark_files
    if meson.get_compiler('cpp').get_id() == 'clang'
      benchmark_exe = executable(f, 'benchmark' / f + '.cpp', dependencies : benchmarkdeps, include_directories: incdir, native : true, override_options : ['optimization=3'], cpp_args : bench_args, build_rpath : llvm_rpath, cpp_pch : 'include/pch/pch_tests.hpp')
    else
      benchmark_exe = executable(f, 'benchmark' / f + '.cpp', dependencies : benchmarkdeps, include_directories: incdir, native : true, override_options : ['optimization=3'], cpp_args : bench_args, build_rpath : llvm_rpath)
    endif
    benchmark(f, benchmark_exe)
  endforeach
endif
