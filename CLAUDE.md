# Clang CFG Lab — Agent Guide

A hands-on lab on **Clang's control-flow graph APIs** (`clang/Analysis/CFG.h`), the classic analyses built on it, and the FlowSensitive dataflow framework (`clang::dataflow`), from `clang --analyze` dumps up to a flow-sensitive checker. It is the sibling of `libtooling-lab/` (AST-level LibTooling) and assumes only a little of it.

The lab runs **entirely locally on macOS** against Homebrew LLVM 22.1.8. No VM, no cluster.

## Response Style (strict)

Default to a **single short sentence** per response. Only expand into detail, lists, or code when the user explicitly asks for details — or when presenting a lab section, which follows the flow below.

## How to Present This Lab

Present the lab **interactively, section by section**. Do NOT dump entire parts at once.

### Flow
1. Check `docs/PROGRESS.md` to see where the user left off.
2. Present ONE section at a time, in the order **Why → What to Do → Verify → Expected**. Explain the concept before showing commands.
3. After each section with hands-on steps:
   - Ask if the user wants to run the commands
   - Offer to execute them if the user agrees
   - Wait for confirmation before proceeding
4. After the user reports output, explain what they observed.
5. **Quizzes:** several sections end with a quiz (`> [!hint]- Quiz: ...` followed by `> [!success]- Answer`, both collapsed Obsidian callouts). Ask the question and let the user answer before revealing anything. Offer the hint only if they are stuck; only then the answer. Never skip a quiz, never answer it unprompted. Never use raw HTML (`<details>` etc.) in lab docs.
6. After completing a part, update `docs/PROGRESS.md` (mark sections `[x]`) and ask "Ready for Part N+1?".

### Before Making Changes
Show what will change in a tool or sample and why, make the change, rebuild (`scripts/build.sh <tool>`), and confirm the result together before moving on.

### Progress Tracking
Update `docs/PROGRESS.md` after each completed section. Mark sections with `[x]`.

## Lab Structure

```
clang-cfg-lab/
├── CLAUDE.md                  ← this file
├── README.md                  ← pointer + quick start
├── docs/
│   ├── README.md              ← TOC, environment, conventions
│   ├── PROGRESS.md            ← section checklist
│   ├── AUTHORING.md           ← how to add tools / samples / sections (read before writing a part)
│   ├── part_1_reading_cfgs_cli.md       ← Part 1
│   ├── part_2_building_cfgs.md          ← Part 2
│   ├── part_3_cxx_semantics.md          ← Part 3
│   ├── part_4_graph_algorithms.md       ← Part 4
│   ├── part_5_classic_analyses.md       ← Part 5
│   ├── part_6_dataflow_framework.md     ← Part 6
│   └── part_7_capstone.md               ← Part 7
├── manifests/                 ← sample inputs: pNN_<name>.cpp / .c / .m (p01_*, p02_* … p07_*)
├── tools/                     ← ONE CMake project; each tools/pNN_<name>/ is an executable
│   ├── CMakeLists.txt         ← add_cfg_tool(); auto-discovers tools/pNN_*/
│   ├── common/cfglab.h        ← shared helpers (platform flags, presets, names, per-function driver)
│   ├── _template/main.cpp     ← copy to start a new tool
│   └── pNN_<name>/            ← one directory per tool (p07_plugin / p07_tidy carry their own CMakeLists):
│       p00: smoke
│       p02: edges, exercises, export, forced, observer, options, skeleton, stmtmap, terminators, walk
│       p03: compare, ctors, eh, elems, lambdas
│       p04: context, dom, graphs, orders, reach, slice, solver
│       p05: calledonce, consumed, deadstores, lifetime, liveness, pipeline, tautology, tsa, uninit, unreachable
│       p06: adorned, constprop, contract, env, flowcond, log, optional, sat, taint, widen
│       p07: combined, ctx, movecheck, persist, plugin, tidy, tu, verify
├── scripts/
│   ├── build.sh               ← build all tools or named ones (scripts/build.sh --clean, --list)
│   ├── run.sh                 ← run a tool; resolves bare manifest names
│   ├── dumpcfg.sh             ← `clang --analyze -Xclang -analyzer-checker=debug.DumpCFG` wrapper (FN=, CHECKER=, LANGMODE=)
│   ├── cfgshape.sh            ← one-line-per-block view of a dump
│   ├── viewcfg.sh             ← debug.ViewCFG → .dot files without opening a viewer
│   ├── optdiff.sh             ← what does one BuildOptions field change for one function
│   ├── flags.sh               ← the macOS platform flags as a string
│   ├── doccheck.py            ← run the doc commands and check/refresh `text expected` blocks
│   ├── check_links.py         ← link + PROGRESS validation
│   ├── check.sh               ← build + smoke test + doccheck + links + site
│   ├── build_site.py          ← docs/*.md → site/index.html + site/part_N.html (dark-mode HTML)
│   └── check_site.py          ← every part/section anchor and internal link exists in site/
├── build/                     ← Ninja output; binaries in build/bin/      (git-ignored)
├── out/                       ← scratch output of lab commands            (git-ignored)
└── site/                      ← generated HTML: python3 scripts/build_site.py, then open site/index.html  (git-ignored)
```

Keep lesson code in `tools/pNN_*/`; keep sample inputs in `manifests/`. Do not create new top-level projects unless the user asks.

## Environment

| Item | Value |
|------|-------|
| Toolchain | Homebrew LLVM **22.1.8** (`brew install llvm cmake ninja graphviz`) at `$(brew --prefix llvm)` |
| Compiler for tools | `$(brew --prefix llvm)/bin/clang++` — **never Apple clang** |
| Build | `scripts/build.sh` (CMake + Ninja, `tools/CMakeLists.txt`) |
| Run | `build/bin/<tool> <file> [options]` — the tools add `-resource-dir`, Homebrew libc++ and the SDK themselves |
| Ground truth for APIs | the installed headers under `/opt/homebrew/opt/llvm/include/clang/Analysis/…`; research page `~/workspace/wiki/pages/research/clang-cfg-api.md` |

### Common commands
```bash
scripts/build.sh                                       # build everything
scripts/build.sh p02_walk                              # build one tool
FN=sign scripts/dumpcfg.sh manifests/p01_hello.cpp     # Static Analyzer CFG dump
build/bin/p02_walk manifests/p01_hello.cpp --func=sign # your own walker
scripts/check.sh --quick                               # build + smoke + docs + links + site
python3 scripts/build_site.py && open site/index.html  # interactive HTML edition (dark mode, sidebar, progress)
```

The site is generated from `docs/*.md` (never edit `site/`); `scripts/check_site.py` verifies every part/section anchor, every fenced block byte-for-byte, that every `dot` fence became one inline diagram, tables, callouts and internal links. Its "done" checkboxes live in the browser's localStorage and are seeded from `docs/PROGRESS.md`.

Nothing in the lab flow is expected to fail: the tools handle the platform plumbing. Present commands as "this just works", not failure-then-fix — except in the sections that deliberately show a failure (Part 2.1, `CFGLAB_RAW=1`).

### Troubleshooting (only if something unexpectedly breaks)
- **`fatal error: 'cmath' file not found` / `'stdarg.h' file not found` at tool runtime**: a tool that does not use `cfglab::runTool` / `runPerFunction` is missing `addPlatformFlags` (see `docs/AUTHORING.md`).
- **`Option 'x' registered more than once` + abort, or `reference to 'X' is ambiguous` at compile time**: a `cl::opt` variable or name collides with something in LLVM/`clang::`. Rename it.
- **Assertion `Name.isIdentifier()`**: called `NamedDecl::getName()` on a constructor/destructor; use `getNameAsString()`.
- **`no member named 'Build' in 'clang::CFGStmtMap'`**: old tutorial; Clang 22 has a constructor (Part 2.7).
- After a `brew upgrade llvm`: `scripts/build.sh --clean` and suspect API drift (the lab is verified against 22.1.8).

## Rules for editing docs

- The Expected blocks (` ```text expected `) are produced by `scripts/doccheck.py --fill docs/part_N_*.md`. **Never hand-edit them**; change the command or the tool and re-fill. `scripts/doccheck.py docs/part_N_*.md` must report 0 problems.
- Diagrams are ```dot fences (Graphviz, rendered to interactive SVG by `scripts/build_site.py`), never ASCII art, and carry no colors/fonts: meaning goes in `class=` attributes. See the "Diagrams" subsection of `docs/AUTHORING.md`.
- Section titles must match between `docs/part_N_*.md`, `docs/PROGRESS.md` and `docs/README.md`. `scripts/check_links.py` checks the first two.
- Follow `docs/AUTHORING.md` for naming, the section template and the checklist.
