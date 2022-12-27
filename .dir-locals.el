;; C-x p c (project-compile) will compile the project and run tests
;; https://www.gnu.org/software/emacs/manual/html_node/emacs/Project-File-Commands.html
((nil . ((compile-command . "cmake --build build/test && cmake --build build/test --target test"))))



