#!/usr/bin/env bash
# Summarise debug.DumpCFG output as one line per block: the *shape* of the graph.
#
#   scripts/cfgshape.sh <file> [extra clang flags]       # every function
#   FN=sum scripts/cfgshape.sh manifests/p01_hello.cpp   # one function
#
# Columns:  block [ENTRY|EXIT|NORETURN|label]   element-count   terminator   -> successors
# Block order is the order of the dump (ENTRY first, then B1, B2, ... B0 last).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$HERE/dumpcfg.sh" "$@" 2>&1 | awk '
  function flush() {
    if (id == "") return
    if (succ == "") succ = "-"
    printf "  %-4s %-16s %2d elems  %-24s -> %s\n", "B" id, flag, n, (term == "" ? "" : "T: " term), succ
    id = ""
  }
  /^[^ \t\[(-]/ { flush(); print $0; next }
  /^ \[B[0-9]+/ {
    flush()
    line = $0; sub(/^ \[B/, "", line); id = line; sub(/[^0-9].*/, "", id)
    flag = ""; if (line ~ /ENTRY/) flag = "ENTRY"; if (line ~ /EXIT/) flag = "EXIT"; if (line ~ /NORETURN/) flag = "NORETURN"
    n = 0; term = ""; succ = ""; next
  }
  /^  [^ 0-9].*:$/ { lab = $0; sub(/^ +/, "", lab); flag = lab; next }
  /^ +[0-9]+: / { n++; next }
  /^   T: / {
    t = $0; sub(/^   T: /, "", t); gsub(/\[B[0-9.]+\]/, "_", t); term = t; next
  }
  /^   Succs/ { s = $0; sub(/^[^:]*: */, "", s); succ = s; next }
  END { flush() }'
