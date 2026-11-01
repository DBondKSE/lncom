#!/bin/sh
cd "$(dirname "$0")" || exit 1
LLVM_CONFIG=${LLVM_CONFIG:-$(command -v llvm-config || echo /opt/homebrew/opt/llvm/bin/llvm-config)}
if [ ! -x "$LLVM_CONFIG" ]; then
  echo "llvm-config not found: install LLVM or set LLVM_CONFIG" >&2
  exit 1
fi
clang++ $("$LLVM_CONFIG" --cxxflags | sed 's/-I/-isystem /g') -Wall -Wextra -O2 src/*.cpp \
  $("$LLVM_CONFIG" --ldflags --libs core --system-libs) -o compiler
