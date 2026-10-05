# Clang CFG Lab

A hands-on lab for the **Clang C++ control-flow-graph APIs and the analyses built on them** — from reading `clang --analyze` dumps to writing your own flow-sensitive checker with the FlowSensitive dataflow framework. Fully local on macOS with Homebrew LLVM 22.1.8; no VM, no cluster.


Start with [`docs/README.md`](docs/README.md) (table of contents, environment, conventions) and keep your place in [`docs/PROGRESS.md`](docs/PROGRESS.md).

```bash
brew install llvm cmake ninja graphviz      # once
scripts/build.sh                            # compile every tool in tools/
FN=sign scripts/dumpcfg.sh manifests/p01_hello.cpp
build/bin/p02_walk manifests/p01_hello.cpp --func=sign
```

**Interactive HTML edition** (dark mode, collapsible sidebar, search, progress tracking, copy buttons), generated from `docs/*.md` so it never drifts:

```bash
python3 scripts/build_site.py     # docs/*.md -> site/index.html + site/part_N.html (stdlib only)
open site/index.html
python3 scripts/check_site.py     # every part/section anchor, code block and internal link
```

Authors adding parts: read [`docs/AUTHORING.md`](docs/AUTHORING.md). Agents presenting the lab: read [`CLAUDE.md`](CLAUDE.md).
