project('LoopModels', ['c', 'cpp'], version : '0.1', default_options : ['cpp_std=gnu++20'])

llvm_dep = dependency('llvm', version : ['>=14.0', '< 15'])
incdir = include_directories('include')

if meson.get_compiler('cpp').get_id() == 'gcc'
  add_global_arguments('-fno-semantic-interposition', language : 'cpp')
  #extra_args = ['-fno-semantic-interposition']
#else
 # extra_args = []
endif

extra_args = []

shared_module('TurboLoop', 'lib/TurboLoop.cpp', dependencies : llvm_dep, include_directories: incdir, cpp_args : extra_args)

# TESTS
gtest_dep = dependency('gtest', main : true, required : false)
testdeps = [gtest_dep, llvm_dep]

test_files = [
  'bitset_test',
  'compat_test',
  #'dependence_test2',
  #'dependence_test',
  #'edge_detection_test',
  #'highs_test',
  'ir_test',
  'linear_algebra_test',
  'linear_diophantine_test',
  'matrix_test',
  'normal_form_test',
  'orthogonalize_test',
  #'perm_test',
  'poset_test',
  'symbolics_test',
  'unimodularization_test',
]

foreach f : test_files
  test_exe = executable(f, 'test' / f + '.cpp', dependencies : testdeps, include_directories: incdir, cpp_args : extra_args, build_rpath : llvm_dep.get_variable(configtool: 'libdir'))
  test(f, test_exe)
endforeach

bench_dep = dependency('benchmark')

benchmark_files = [
  'constraint_pruning_benchmark',
  'polynomial_benchmark'
]
benchmarkdeps = [bench_dep, llvm_dep]
#bench_args = ['-O3', '-DNDEBUG', '-march=native'] + extra_args
bench_args = ['-O3', '-DNDEBUG'] + extra_args
foreach f : benchmark_files
  benchmark_exe = executable(f, 'benchmark' / f + '.cpp', dependencies : benchmarkdeps, include_directories: incdir, native : true, override_options : ['buildtype=release'], cpp_args : bench_args)
  #benchmark_exe = build_target(f, 'benchmark' / f + '.cpp', dependencies : benchmarkdeps, include_directories: incdir, native : true, override_options : ['buildtype=release'], target_type : 'executable')
  benchmark(f, benchmark_exe)
endforeach

#normal_form_test = executable('normal_form_test', 'test/normal_form_test.cpp', dependencies : testdeps, include_directories: incdir)
#test('gtest test', normal_form_test)
#
#poset_test = executable('poset_test', 'test/poset_test.cpp', dependencies : testdeps, include_directories: incdir)
#test('gtest test', poset_test)
