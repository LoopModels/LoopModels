#!/usr/bin/env bash

set -e

failedFiles=( )
cd "$(dirname "$1")/release_build/benchmark/"
for file in *; do
    if [[ -x "$file" && "$file" != "CMakeFiles" ]]
    then
        if ! ./$file; then
            failedFiles=(${failedFiles[@]} "$file")
        fi
    fi
done

if [[ ${failedFiles[@]} ]]; then
    echo
    echo "The following benchmarks failed:"
    for file in "${failedFiles[@]}"
    do
        echo $file
    done
    exit -1
fi
