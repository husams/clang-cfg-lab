#!/usr/bin/env bash
# What does turning on BuildOptions field(s) change for one function?
#
#   scripts/optdiff.sh <file> <function> <Field[,Field...]> [base p02_options flags...]
#   scripts/optdiff.sh manifests/p02_options.cpp dtors AddImplicitDtors
#   scripts/optdiff.sh manifests/p02_options.cpp aggregate AddCXXDefaultInitExprInAggregates --always-add=all
#   MODE=dump scripts/optdiff.sh ...     # diff the full CFG text instead of the summary
#
# The "base" is `p02_options <file> --func=<function>` plus any extra flags you
# give; the "with" run adds --set=<Field,...> to it.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 3 ]] || { sed -n '2,10p' "${BASH_SOURCE[0]}"; exit 2; }
FILE="$1"; FN="$2"; OPT="$3"; shift 3
BIN="$ROOT/build/bin/p02_options"
[[ -x "$BIN" ]] || "$ROOT/scripts/build.sh" p02_options >&2
MODEFLAG=(--summary)
[[ "${MODE:-summary}" == dump ]] && MODEFLAG=()
BASE=$("$BIN" "$FILE" --func="$FN" ${MODEFLAG[@]+"${MODEFLAG[@]}"} "$@" 2>&1) || true
WITH=$("$BIN" "$FILE" --func="$FN" ${MODEFLAG[@]+"${MODEFLAG[@]}"} --set="$OPT" "$@" 2>&1) || true
if [[ "$BASE" == "$WITH" ]]; then echo "no change"; exit 0; fi
diff -U0 --label "without" --label "+$OPT" <(echo "$BASE") <(echo "$WITH") | grep -v '^@@' || true
