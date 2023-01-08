;; C-x p c (project-compile) will compile the project and run tests
;; https://www.gnu.org/software/emacs/manual/html_node/emacs/Project-File-Commands.html
((nil . ((compile-command . "UBSAN_OPTIONS='print_stacktrace=1' cmake --build buildgcc/test && UBSAN_OPTIONS='print_stacktrace=1' CTEST_OUTPUT_ON_FAILURE=1 cmake --build buildgcc/test --target test && gcovr --coveralls coverage-final.json && UBSAN_OPTIONS='print_stacktrace=1' cmake --build buildclang/test && UBSAN_OPTIONS='print_stacktrace=1' CTEST_OUTPUT_ON_FAILURE=1 cmake --build buildclang/test --target test"))))



