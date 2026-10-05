#!/usr/bin/env bash
# Full lab self-check: clean build of every tool, toolchain smoke test,
# documentation commands, documentation links, generated HTML site.
#
#   scripts/check.sh            # everything
#   scripts/check.sh --quick    # skip the clean rebuild
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ "${1:-}" == "--quick" ]]; then
  echo "== build (incremental)"; scripts/build.sh >/dev/null
else
  echo "== build (clean)"; scripts/build.sh --clean >/dev/null
fi
echo "   tools: $(scripts/build.sh --list | tr '\n' ' ')"

echo "== smoke test (every library family the lab uses)"
build/bin/p00_smoke manifests/p01_hello.cpp

echo "== documentation commands"
shopt -s nullglob
scripts/doccheck.py docs/part_*.md

echo "== documentation links and PROGRESS"
scripts/check_links.py

echo "== interactive HTML site (generate + verify anchors, code blocks, links)"
python3 scripts/build_site.py
python3 scripts/check_site.py
