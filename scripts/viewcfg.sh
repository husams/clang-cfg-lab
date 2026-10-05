#!/usr/bin/env bash
# Capture the Graphviz graphs that debug.ViewCFG produces, without opening a viewer.
#
#   scripts/viewcfg.sh <file> [outdir]          # default outdir: out/dot
#   scripts/viewcfg.sh manifests/p01_hello.cpp
#   scripts/viewcfg.sh manifests/p01_hello.cpp out/dot --svg   # also render with dot
#
# debug.ViewCFG calls llvm::ViewGraph: it writes CFG-<random>.dot into $TMPDIR and
# then tries to launch a viewer (`open` on macOS). We point TMPDIR at outdir and
# give the process an empty PATH so no viewer can start, then rename the files
# <stem>.<n>.<function>.dot in the order the checker visited the functions.
set -euo pipefail
LLVM="${LLVM:-$(brew --prefix llvm 2>/dev/null || echo /opt/homebrew/opt/llvm)}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FILE="$1"; OUT="${2:-$ROOT/out/dot}"; SVG="${3:-}"
mkdir -p "$OUT"
STEM="$(basename "${FILE%.*}")"
case "$FILE" in
  *.c) LANGFLAGS=(-x c -std=c11) ;;
  *)   LANGFLAGS=(-x c++ -std=c++17 -fcxx-exceptions) ;;
esac

# 1. function names, in visiting order (DumpCFG headers are the unindented lines)
FNS=()
while IFS= read -r line; do FNS+=("$line"); done < <("$LLVM/bin/clang" --analyze \
  -Xclang -analyzer-checker=debug.DumpCFG -o /dev/null "${LANGFLAGS[@]}" "$FILE" 2>&1 |
  awk '/^[^ \t\[(-]/ { print }' | sed -E 's/\(.*//; s/.*[ *&:]//')

# 2. the .dot files, in the same order
TMP="$(mktemp -d "$OUT/.tmp.XXXXXX")"
DOTS=()
while IFS= read -r line; do DOTS+=("$line"); done < <(env TMPDIR="$TMP" PATH=/var/empty \
  "$LLVM/bin/clang" --analyze -Xclang -analyzer-checker=debug.ViewCFG -o /dev/null \
  "${LANGFLAGS[@]}" "$FILE" 2>&1 | sed -n "s/^Writing '\(.*\)'\.\.\.  done\. *$/\1/p")

i=0
for d in "${DOTS[@]}"; do
  i=$((i + 1))
  name="${FNS[$((i - 1))]:-fn$i}"
  dest="$OUT/$STEM.$i.$name.dot"
  mv "$d" "$dest"
  echo "${dest#$ROOT/}"
  if [[ "$SVG" == "--svg" ]]; then dot -Tsvg "$dest" -o "${dest%.dot}.svg"; echo "${dest%.dot}.svg" | sed "s|^$ROOT/||"; fi
done
rm -rf "$TMP"
