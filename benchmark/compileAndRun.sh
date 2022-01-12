#!/bin/bash

g++ -O3 -march=native benchmark.cpp -std=c++20 -isystem benchmark/include -Lbenchmark/build/src -lbenchmark -lpthread -o mybenchmark
./mybenchmark
