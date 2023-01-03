;; C-x p c (project-compile) will compile the project and run tests
;; https://www.gnu.org/software/emacs/manual/html_node/emacs/Project-File-Commands.html
((nil . ((compile-command . "cmake --build buildgcc/test && CTEST_OUTPUT_ON_FAILURE=1 cmake --build buildgcc/test --target test && gcovr --coveralls coverage-final.json && cmake --build buildclang/test && CTEST_OUTPUT_ON_FAILURE=1 cmake --build buildclang/test --target test"))))



