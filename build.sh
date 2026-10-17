#!/bin/sh
cd "$(dirname "$0")" || exit 1
clang++ -std=c++17 -Wall -Wextra -O2 src/*.cpp -o compiler
