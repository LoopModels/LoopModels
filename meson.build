project('LoopModels', ['cpp'], version : '0.1', default_options : ['cpp_std=gnu++20'])

llvm_dep = dependency('llvm', version : '>=14.0')

incdir = include_directories('include')

if meson.get_compiler('cpp').get_id() == 'gcc'
  add_global_arguments('-fno-semantic-interposition', language : 'cpp')
  add_global_arguments('-fmax-errors=1', language : 'cpp')
else
  if meson.get_compiler('cpp').get_id() == 'clang'
    add_global_arguments('-ferror-limit=1', language : 'cpp')
  endif
endif
add_global_arguments('-fno-rtti', language : 'cpp')
add_global_arguments('-fno-exceptions', language : 'cpp')

llvm_rpath = llvm_dep.get_variable(configtool: 'libdir')
debug_args = ['-Wall', '-Wextra', '-Wpedantic']

# require clang for pch, as clang's pch should be clangd-compatible
# if meson.get_compiler('cpp').get_id() == 'clang'
turbo_module = shared_module('TurboLoop', 'lib/TurboLoop.cpp', dependencies : llvm_dep, include_directories: incdir, cpp_args : debug_args, build_rpath : llvm_rpath, cpp_pch : 'include/pch/turbo.hpp')
# else
#   shared_module('TurboLoop', 'lib/TurboLoop.cpp', dependencies : llvm_dep, include_directories: incdir, cpp_args : debug_args, build_rpath : llvm_rpath)
# endif

# TESTS
if get_option('test')
  gtest_dep = dependency('gtest', main : true, fallback : ['gtest', 'gtest_main_dep'])
  if gtest_dep.found()
    
    remark_test_exe = executable('remarks_test', 'test' / 'remarks_test.cpp', include_directories: incdir, cpp_args : debug_args, build_rpath : llvm_rpath)
    test('remarks_test', remark_test_exe, depends: [turbo_module], args: [turbo_module.full_path(), meson.source_root() / 'test' / 'examples'])

    testdeps = [gtest_dep, llvm_dep]

    test_files = [
      'bitset_test',
      'cost_modeling_test',
      'comparator_test',
      'compat_test',
      'dependence_test',
      'graph_test',
      'linear_algebra_test',
      'linear_diophantine_test',
      'matrix_test',
      'normal_form_test',
      'orthogonalize_test',
      'simplex_test',
      'string_to_intmat_test',
      'unimodularization_test',
    ]

    foreach f : test_files
      test_exe = executable(f, 'test' / f + '.cpp', dependencies : testdeps, include_directories: incdir, cpp_args : debug_args, build_rpath : llvm_rpath, cpp_pch : 'include/pch' / f + '.hpp')
      test(f, test_exe)
    endforeach
  endif
endif

if get_option('benchmark')
  bench_dep = dependency('benchmark', fallback : ['google-benchmark', 'google_benchmark_dep'])

  if bench_dep.found()
    benchmark_files = [
      'constraint_pruning_benchmark',
    ]
    benchmarkdeps = [bench_dep, llvm_dep]
    #bench_args = ['-O3', '-DNDEBUG', '-march=native']
    #bench_args = ['-O3', '-DNDEBUG']
    bench_args = ['-DNDEBUG']
    # https://github.com/mesonbuild/meson/issues/5920
    # TODO: add 'buildtype=release' when issue resolved
    foreach f : benchmark_files
      benchmark_exe = executable(f, 'benchmark' / f + '.cpp', dependencies : benchmarkdeps, include_directories: incdir, native : true, override_options : ['optimization=3'], cpp_args : bench_args, build_rpath : llvm_rpath, cpp_pch : 'include/pch/turbo.hpp')
      benchmark(f, benchmark_exe)
    endforeach
  endif
endif
