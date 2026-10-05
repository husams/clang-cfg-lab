#!/usr/bin/env bash
# Build tools in tools/pNN_<name>/ against Homebrew LLVM 22.
#
#   scripts/build.sh                 build every tool
#   scripts/build.sh p02_walk ...    build only the named tools
#   scripts/build.sh --clean [...]   wipe build/ first (full rebuild)
#   scripts/build.sh --list          list discoverable tools
#
# Environment:
#   LLVM   LLVM prefix (default: $(brew --prefix llvm))
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM="${LLVM:-$(brew --prefix llvm 2>/dev/null || echo /opt/homebrew/opt/llvm)}"
BUILD="$ROOT/build"

if [[ "${1:-}" == "--list" ]]; then
  for d in "$ROOT"/tools/p[0-9][0-9]_*/; do basename "$d"; done
  exit 0
fi
if [[ "${1:-}" == "--clean" ]]; then
  shift
  rm -rf "$BUILD"
fi

if [[ ! -x "$LLVM/bin/clang++" ]]; then
  echo "error: $LLVM/bin/clang++ not found. brew install llvm cmake ninja" >&2
  exit 1
fi

# Configure on every call: it is cheap, and it picks up newly added tool dirs.
mkdir -p "$BUILD"
cmake -G Ninja -S "$ROOT/tools" -B "$BUILD" \
  -DCMAKE_PREFIX_PATH="$LLVM" \
  -DCMAKE_C_COMPILER="$LLVM/bin/clang" \
  -DCMAKE_CXX_COMPILER="$LLVM/bin/clang++" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo > "$BUILD/configure.log" 2>&1 \
  || { cat "$BUILD/configure.log" >&2; exit 1; }

if [[ $# -eq 0 ]]; then
  cmake --build "$BUILD"
else
  cmake --build "$BUILD" --target "$@"
fi
