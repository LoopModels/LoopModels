#!/usr/bin/env bash

set -e

failedFiles=( )
cd "$(dirname "$1")/build/test/"
for file in *; do
    if [[ -x "$file" && "$file" != "CMakeFiles" ]]
    then
        echo "Running $file"
        if ! ./$file; then
            failedFiles=(${failedFiles[@]} "$file")
        fi
    fi
done

if [[ ${failedFiles[@]} ]]; then
    echo
    echo "The following tests failed:"
    for file in "${failedFiles[@]}"
    do
        echo $file
    done
    exit -1
fi
