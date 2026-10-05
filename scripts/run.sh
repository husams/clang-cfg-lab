#!/usr/bin/env bash
# Run a lab tool:  scripts/run.sh <tool> <source-file> [tool options] [-- compiler flags]
#
#   scripts/run.sh p02_walk manifests/p02_walk.cpp
#   scripts/run.sh p02_options manifests/p01_basic.cpp --preset=sema
#   scripts/run.sh p02_walk manifests/p02_cxx.cpp -- -std=c++20
#
# What it adds on top of calling build/bin/<tool> directly:
#   * builds the tool first if build/bin/<tool> is missing
#   * resolves a bare file name against manifests/ (p01_basic.cpp -> manifests/p01_basic.cpp)
#   * the compiler flags the embedded front end needs on macOS (-resource-dir,
#     Homebrew libc++ via -nostdinc++ -isystem, -isysroot) are added by
#     tools/common/cfglab.h inside the tool; `scripts/flags.sh` prints the same
#     set for use with clang-query or a plain clang invocation.
#   * if you give no "--", it appends "-- -std=c++17" (or -std=c11 for .c)
#
# Set CFGLAB_RAW=1 to disable the platform flags (to see why they exist).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 1 ]] || { sed -n '2,15p' "${BASH_SOURCE[0]}"; exit 2; }
TOOL="$1"; shift
BIN="$ROOT/build/bin/$TOOL"
[[ -x "$BIN" ]] || "$ROOT/scripts/build.sh" "$TOOL" >&2

ARGS=()
for a in "$@"; do
  if [[ "$a" != -* && ! -e "$a" && -e "$ROOT/manifests/$a" ]]; then
    ARGS+=("$ROOT/manifests/$a")
  else
    ARGS+=("$a")
  fi
done
exec "$BIN" "${ARGS[@]}"
