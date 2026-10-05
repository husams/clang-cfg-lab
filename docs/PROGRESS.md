# Lab Progress

Mark a section `[x]` when you have finished it. Titles here must match the `## Section N.M` headings of the part files; `scripts/check_links.py` enforces that.

## Part 1 — Reading CFGs on the Command Line
- [ ] 1.1 — What a CFG is, and your first dump
- [ ] 1.2 — Reading the dump format
- [ ] 1.3 — Branch constructs
- [ ] 1.4 — `-analyzer-config cfg-*`: what the CFG models
- [ ] 1.5 — Graphviz: `debug.ViewCFG`
- [ ] 1.6 — Companion dumps: dominators, liveness, call graph
- [ ] 1.7 — C versus C++
- [ ] 1.8 — Checkpoint

## Part 2 — Building CFGs in C++
- [ ] 2.1 — A LibTooling skeleton for Clang 22
- [ ] 2.2 — `BuildOptions` I: what the CFG records
- [ ] 2.3 — `BuildOptions` II: shape, hooks and presets
- [ ] 2.4 — Walking blocks and elements
- [ ] 2.5 — Edges, `AdjacentBlock` and pruning
- [ ] 2.6 — Terminators, conditions, labels and loop targets
- [ ] 2.7 — `CFGStmtMap` and `ParentMap`
- [ ] 2.8 — Exporting JSON and DOT
- [ ] 2.9 — Exercises: complexity, loops, return-in-loop
- [ ] 2.10 — Checkpoint

## Part 3 — C++ Semantics in the CFG
- [ ] 3.1 — Implicit destructors: automatic, temporary, member, base, delete
- [ ] 3.2 — Lifetimes, scopes and cleanup attributes
- [ ] 3.3 — Constructors and `ConstructionContext`
- [ ] 3.4 — Initialisers, default member initialisers and aggregates
- [ ] 3.5 — `new`, `delete`, static-init and virtual-base branches
- [ ] 3.6 — Exceptions: try/catch, `throw`, `AddEHEdges`, `noreturn`
- [ ] 3.7 — Lambdas and templates
- [ ] 3.8 — Option presets: Sema versus `AdornedCFG` versus the analyzer
- [ ] 3.9 — Checkpoint

## Part 4 — Graph Algorithms over the CFG
- [ ] 4.1 — `AnalysisDeclContext`: lazy CFGs, `getAnalysis<T>`, pruned versus unoptimised
- [ ] 4.2 — Iteration orders: `PostOrderCFGView` and weak topological order
- [ ] 4.3 — Reachability
- [ ] 4.4 — Dominators, post-dominators and natural loops
- [ ] 4.5 — Control dependence and a program-slicing toy
- [ ] 4.6 — LLVM's generic graph algorithms through `GraphTraits`
- [ ] 4.7 — Worklists and your own fixpoint solver
- [ ] 4.8 — Checkpoint

## Part 5 — Classic CFG Analyses
- [ ] 5.1 — Hand-written bit-vector liveness versus `LiveVariables`
- [ ] 5.2 — `LiveVariables::Observer`: a dead-store finder
- [ ] 5.3 — `UninitializedValues`: kinds of uninitialised use
- [ ] 5.4 — `ReachableCode` and how Sema picks `BuildOptions`
- [ ] 5.5 — Thread safety, consumed typestate and called-once
- [ ] 5.6 — Lifetime safety (experimental)
- [ ] 5.7 — `CFGCallback`: always-true comparisons
- [ ] 5.8 — How `AnalysisBasedWarnings` composes everything
- [ ] 5.9 — Checkpoint

## Part 6 — The FlowSensitive Dataflow Framework
- [ ] 6.1 — Lattices in code: constant propagation with `VarMapLattice`
- [ ] 6.2 — Running an analysis: `AdornedCFG`, `runDataflowAnalysis`, element callbacks
- [ ] 6.3 — Widening, loops and `MaxBlockVisits`
- [ ] 6.4 — `Environment`: storage locations, values, properties, synthetic fields
- [ ] 6.5 — Flow conditions and SAT: `assume`, `proves`, `allows`, `transferBranch`
- [ ] 6.6 — `CFGMatchSwitch` transfer functions and `ValueModel`
- [ ] 6.7 — Built-in models: `UncheckedOptionalAccessModel` and friends
- [ ] 6.8 — Debugging: `-dataflow-log`, HTML logs, `Environment::dump`
- [ ] 6.9 — Checkpoint

## Part 7 — Capstone & Engineering
- [ ] 7.1 — Design a checker
- [ ] 7.2 — Implement it with `Environment`, synthetic fields and a diagnoser
- [ ] 7.3 — A whole-translation-unit driver: skipping, budgets, `llvm::Expected`
- [ ] 7.4 — A test harness: fixtures, expected diagnostics, regressions
- [ ] 7.5 — Context-sensitive analysis and its limits
- [ ] 7.6 — Packaging: clang-tidy-style check, plugin, comparison with the Static Analyzer
- [ ] 7.7 — Performance and persistence
- [ ] 7.8 — Checkpoint

## Part 8 — Call Graph Fundamentals
- [ ] 8.1 — What a call graph is: `debug.DumpCallGraph`, the dump order and `< root >`
- [ ] 8.2 — The container: `CallGraph`, `CallGraphNode`, `CallRecord` and canonical declarations
- [ ] 8.3 — Names and identity: `printQualifiedName`, template arguments, lambdas, blocks, selectors and the USR
- [ ] 8.4 — The builder: a `DynamicRecursiveASTVisitor` with two doors, four flags and an incremental `addToCallGraph`
- [ ] 8.5 — What `CGBuilder` records: calls, constructors, `new`, initialisers, default arguments, lambdas and blocks
- [ ] 8.6 — What stays out: function pointers, block variables, `delete`, implicit destructors, virtual targets and Objective-C's rule
- [ ] 8.7 — `AnyCall`: one interface for every call-like expression and declaration
- [ ] 8.8 — Your own call graph: a visitor that records what `CGBuilder` skips, diffed against the library's
- [ ] 8.9 — Exports: the lab's DOT and JSON, the library's `WriteGraph`, `print`, `dump` and `viewGraph`
- [ ] 8.10 — Checkpoint

## Part 9 — Call Graph Algorithms
- [ ] 9.1 — `GraphTraits<CallGraph*>` and the generic traversals: `depth_first`, `breadth_first`, `post_order` and `ReversePostOrderTraversal`
- [ ] 9.2 — Strongly connected components: `scc_iterator`, recursion and `clang-tidy`'s `misc-no-recursion`
- [ ] 9.3 — Reachability, roots and dead functions
- [ ] 9.4 — Callers: the reverse graph, the missing `Inverse<CallGraph*>` and transitive callers
- [ ] 9.5 — Metrics: fan-in, fan-out, call sites, height and the SCC condensation
- [ ] 9.6 — Witness paths: shortest call chains and `pathfindSomeCycle`
- [ ] 9.7 — The Static Analyzer's order: `HandleDeclsCallGraph`, `-analyzer-display-progress`, `-analyzer-note-analysis-entry-points` and `debug.Stats`
- [ ] 9.8 — Inlining configuration: `mode`, `ipa`, `max-inlinable-size`, `ipa-always-inline-size`, `-analyzer-inline-max-stack-depth`, `-analyzer-inlining-mode` and the statistics switches
- [ ] 9.9 — Checkpoint

## Part 10 — Interprocedural Analysis
- [ ] 10.1 — Call sites in the CFG: `AnyCall` on elements, the `missing:` classes and the options that decide what the CFG has
- [ ] 10.2 — An interprocedural walk: descending into resolved callees
- [ ] 10.3 — Bottom-up summaries over SCCs: a transitive `noreturn` analysis with a fixed point
- [ ] 10.4 — Widening and the limits of a summary: `depth`, `inf` and call-site context
- [ ] 10.5 — Context sensitivity: k-limited call strings
- [ ] 10.6 — How the Static Analyzer goes interprocedural: `CallEvent` kinds, inlining versus conservative evaluation, `RuntimeDefinition` and the exploded graph
- [ ] 10.7 — `BodyFarm`: synthesised bodies for `dispatch_once`, `std::call_once` and friends
- [ ] 10.8 — Context sensitivity in the FlowSensitive framework: `ContextSensitiveOptions`, `pushCall`, `popCall`, `canDescend`
- [ ] 10.9 — Checkpoint

## Part 11 — Indirect Calls, Cross-TU and Scale
- [ ] 11.1 — Function pointers: address-taken sets, signature matching and the recursion they hide
- [ ] 11.2 — Virtual calls: `getDevirtualizedMethod`, class-hierarchy analysis and rapid type analysis
- [ ] 11.3 — Soundness and precision: what each rule trades, and the analyzer's `ipa=dynamic` / `dynamic-bifurcate`
- [ ] 11.4 — `clang::index` as a second source of call edges: `SymbolRole::Call`, `RelationCalledBy` and `Dyn`
- [ ] 11.5 — Cross-TU merge by USR
- [ ] 11.6 — Clang's real CTU: `clang-extdef-mapping`, `-emit-ast`, `ctu-dir`, on-demand parsing and `-analyzer-output=text`
- [ ] 11.7 — Capstone: a cross-TU recursion and sink checker
- [ ] 11.8 — Scale: a 3000-function translation unit, costs and persistence
- [ ] 11.9 — Checkpoint
