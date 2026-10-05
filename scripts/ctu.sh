#!/usr/bin/env bash
# Run Clang's real cross-translation-unit (CTU) analysis (Part 11.6).
#
#   scripts/ctu.sh <main.cpp> <other.cpp...>
#   scripts/ctu.sh manifests/p11_ctu_a.cpp manifests/p11_ctu_b.cpp
#   CTU_MODE=ondemand scripts/ctu.sh manifests/p11_ctu_a.cpp manifests/p11_ctu_b.cpp
#   CHECKER=core scripts/ctu.sh manifests/p11_ctu_a.cpp manifests/p11_ctu_b.cpp
#   CTU_DIR=out/ctu2 scripts/ctu.sh ...
#
# The analyzer imports the definition of a function from another translation unit's AST
# (CrossTranslationUnitContext::getCrossTUDefinition), found through an index that maps USRs to
# files. This script builds that index for the <other> files and analyses <main>:
#
#   CTU_MODE=ast       (default) every other file is compiled to <name>.ast (clang -emit-ast) in
#                      CTU_DIR; externalDefMap.txt maps each USR to that .ast, relative to CTU_DIR.
#   CTU_MODE=ondemand  no .ast files: the map keeps the absolute source paths and
#                      invocations.yaml tells the analyzer how to compile each one when it is
#                      needed (ctu-invocation-list).
#   CHECKER            analyzer checkers (default core,debug.ExprInspection)
#   CTU_DIR            where the index and the .ast files go (default <lab>/out/ctu); the files this
#                      script writes there are replaced, nothing else is touched
#
# The commands it runs are printed first, prefixed with `+`; then the analyzer's own output
# follows on stderr. -analyzer-output=text is on because a report whose path crosses files can
# only be written in that format (any other prints "Path diagnostic report is not generated"
# and drops the path). .c files compile as C, everything else as C++17; the .ast files, the
# on-demand compiles and <main> must agree on the flags.
set -euo pipefail
LLVM="${LLVM:-$(brew --prefix llvm 2>/dev/null || echo /opt/homebrew/opt/llvm)}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CTU_MODE="${CTU_MODE:-ast}"
CHECKER="${CHECKER:-core,debug.ExprInspection}"
CTU_DIR="${CTU_DIR:-$ROOT/out/ctu}"

if [[ $# -lt 2 ]]; then
  echo "usage: scripts/ctu.sh <main.cpp> <other.cpp...>" >&2
  exit 2
fi
case "$CTU_MODE" in ast | ondemand) ;; *) echo "CTU_MODE must be ast or ondemand" >&2; exit 2 ;; esac
MAIN="$1"; shift
OTHERS=("$@")

# the compile flags of one file; callers leave $(lang ...) unquoted so it splits into two words
lang() { case "$1" in *.c) echo "-x c -std=c11" ;; *) echo "-x c++ -std=c++17" ;; esac; }
# print a command the way `set -x` would, on stderr, before it runs
show() { printf '+'; printf ' %s' "$@"; printf '\n'; } >&2

mkdir -p "$CTU_DIR"
rm -f "$CTU_DIR/externalDefMap.txt" "$CTU_DIR/invocations.yaml" "$CTU_DIR"/*.ast

# 1. the index of external definitions: "<len>:<usr> <file>" per function
: > "$CTU_DIR/externalDefMap.txt"
declare -a SEEN=()
for f in "${OTHERS[@]}"; do
  base="$(basename "$f")"
  for s in ${SEEN[@]+"${SEEN[@]}"}; do
    [[ "$s" == "$base" ]] && { echo "two files named $base: the .ast names would collide" >&2; exit 2; }
  done
  SEEN+=("$base")
  show clang-extdef-mapping "$f" -- $(lang "$f")
  if [[ "$CTU_MODE" == ast ]]; then
    # the tool prints the absolute source path; the analyzer wants the .ast, relative to ctu-dir
    "$LLVM/bin/clang-extdef-mapping" "$f" -- $(lang "$f") 2>/dev/null |
      sed -E "s| [^ ]+\$| $base.ast|" >> "$CTU_DIR/externalDefMap.txt"
    show clang++ $(lang "$f") -emit-ast -o "$CTU_DIR/$base.ast" "$f"
    "$LLVM/bin/clang++" $(lang "$f") -emit-ast -o "$CTU_DIR/$base.ast" "$f"
  else
    "$LLVM/bin/clang-extdef-mapping" "$f" -- $(lang "$f") 2>/dev/null >> "$CTU_DIR/externalDefMap.txt"
    abs="$(cd "$(dirname "$f")" && pwd)/$base"
    {
      echo "\"$abs\":"
      echo "  - \"$LLVM/bin/clang++\""
      for a in $(lang "$f"); do echo "  - \"$a\""; done
      echo "  - \"$abs\""
    } >> "$CTU_DIR/invocations.yaml"
  fi
done

# 2. the analysis of <main>
CFG="experimental-enable-naive-ctu-analysis=true,ctu-dir=$CTU_DIR,display-ctu-progress=true"
[[ "$CTU_MODE" == ondemand ]] && CFG+=",ctu-invocation-list=$CTU_DIR/invocations.yaml"
show clang++ --analyze $(lang "$MAIN") -Xclang -analyzer-checker="$CHECKER" -Xclang -analyzer-output=text \
  -Xclang -analyzer-config -Xclang "$CFG" -o /dev/null "$MAIN"
exec "$LLVM/bin/clang++" --analyze $(lang "$MAIN") -Xclang -analyzer-checker="$CHECKER" -Xclang -analyzer-output=text \
  -Xclang -analyzer-config -Xclang "$CFG" -o /dev/null "$MAIN"
