;; C-x p c (project-compile) will compile the project and run tests
;; https://www.gnu.org/software/emacs/manual/html_node/emacs/Project-File-Commands.html
((nil . ((compile-command . "cmake --build build/test && CTEST_OUTPUT_ON_FAILURE=1 cmake --build build/test --target test"))))



