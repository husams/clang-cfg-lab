#!/usr/bin/env bash
# Print the compiler flags a standalone Clang front end needs on macOS with
# Homebrew LLVM (the same set tools/common/cfglab.h adds in-process).
#
#   clang-query file.cpp -- -std=c++17 $(scripts/flags.sh)
set -euo pipefail
LLVM="${LLVM:-$(brew --prefix llvm 2>/dev/null || echo /opt/homebrew/opt/llvm)}"
RES="$("$LLVM/bin/clang" -print-resource-dir)"
SDK="$(xcrun --show-sdk-path)"
echo "-resource-dir $RES -nostdinc++ -isystem $LLVM/include/c++/v1 -isysroot $SDK"
