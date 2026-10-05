#!/usr/bin/env bash
# Dump CFGs with the Static Analyzer's debug checkers (Part 1).
#
#   scripts/dumpcfg.sh <file> [extra clang flags...]
#   scripts/dumpcfg.sh manifests/p01_basic.cpp
#   scripts/dumpcfg.sh manifests/p01_cxx.cpp -Xclang -analyzer-config -Xclang cfg-lifetime=true
#   CHECKER=debug.DumpDominators scripts/dumpcfg.sh manifests/p01_basic.cpp
#   FN=f scripts/dumpcfg.sh manifests/p01_basic.cpp      # only function(s) named f
#   LANGMODE=c++ scripts/dumpcfg.sh manifests/p01_both.c  # compile a .c file as C++
#
# Uses the *driver* form (`clang --analyze -Xclang -analyzer-checker=...`),
# which finds the SDK and libc++ by itself. .c files compile as C, everything
# else as C++17 with exceptions enabled.
set -euo pipefail
LLVM="${LLVM:-$(brew --prefix llvm 2>/dev/null || echo /opt/homebrew/opt/llvm)}"
CHECKER="${CHECKER:-debug.DumpCFG}"
FILE="$1"; shift
# LANGMODE=c|c++ overrides the file-extension guess (to compile a .c file as C++).
LANGMODE="${LANGMODE:-}"
if [[ -z "$LANGMODE" ]]; then
  case "$FILE" in *.c) LANGMODE=c ;; *) LANGMODE=c++ ;; esac
fi
case "$LANGMODE" in
  c)   LANGFLAGS=(-x c -std=c11) ;;
  c++) LANGFLAGS=(-x c++ -std=c++17 -fcxx-exceptions) ;;
  *)   echo "LANGMODE must be c or c++" >&2; exit 2 ;;
esac
run() {
  # -o /dev/null: otherwise the analyzer writes <file>.plist into the cwd
  "$LLVM/bin/clang" --analyze -Xclang -analyzer-checker="$CHECKER" -o /dev/null \
    "${LANGFLAGS[@]}" "$@" "$FILE"
}
if [[ -n "${FN:-}" ]]; then
  # Keep only the block of text that starts at a line mentioning the function
  # and ends at the next function header (a non-indented line).
  run "$@" 2>&1 | awk -v fn="$FN" '
    /^[^ \t\[(-]/ { show = ($0 ~ "(^|[ :*&])" fn "\\(") }
    show { print }'
else
  run "$@" 2>&1
fi
