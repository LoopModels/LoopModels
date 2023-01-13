;; C-x p c (project-compile) will compile the project and run tests
;; https://www.gnu.org/software/emacs/manual/html_node/emacs/Project-File-Commands.html
((nil . ((compile-command . "UBSAN_OPTIONS='print_stacktrace=1' cmake --build buildgcc/test && UBSAN_OPTIONS='print_stacktrace=1' CTEST_OUTPUT_ON_FAILURE=1 cmake --build buildgcc/test --target test && gcovr --coveralls coverage-final.json && UBSAN_OPTIONS='print_stacktrace=1' cmake --build buildclang/test && UBSAN_OPTIONS='print_stacktrace=1' CTEST_OUTPUT_ON_FAILURE=1 cmake --build buildclang/test --target test"))))
;; ((nil . ((compile-command . "UBSAN_OPTIONS='print_stacktrace=1' cmake --build buildgcc/test && LD_PRELOAD=/usr/lib64/libasan.so.8 LSAN_OPTIONS='suppressions=../../test/leak_warning_suppressions.txt' UBSAN_OPTIONS='print_stacktrace=1' CTEST_OUTPUT_ON_FAILURE=1 cmake --build buildgcc/test --target test && gcovr --coveralls coverage-final.json && UBSAN_OPTIONS='print_stacktrace=1' cmake --build buildclang/test && LD_PRELOAD=$(clang -print-file-name=libclang_rt.asan-x86_64.so) LSAN_OPTIONS='suppressions=../../test/leak_warning_suppressions.txt' UBSAN_OPTIONS='print_stacktrace=1' CTEST_OUTPUT_ON_FAILURE=1 cmake --build buildclang/test --target test"))))




