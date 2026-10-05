"""Tests for outviz.cg: `python3 -m unittest discover -s scripts/outviz` (or run this file).

The samples are real outputs of this lab's call-graph tools (build/bin/p08_* ... p11_*), of clang-tidy and of debug.DumpCallGraph, trimmed.
Every figure is also rendered with the site's dot flags: no warning, and at most 900 px wide.
"""
import re
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import build_site  # noqa: E402
import outviz  # noqa: E402
from outviz import cg  # noqa: E402

# --------------------------------------------------------------------------
# samples
# --------------------------------------------------------------------------
BASIC_DUMP = """ --- Call graph Dump ---
  Function: < root > calls: g f fact unused main
  Function: main calls: f fact
  Function: unused calls:
  Function: fact calls: fact
  Function: f calls: g g
  Function: g calls:
"""

# the dump as clang prints it: a space after every name, `operator new` and `< >` have a space in them,
# two instantiations of `twice` and two lambdas print the same
INCLUDE_DUMP = (
    " --- Call graph Dump --- \n"
    "  Function: < root > calls: twice twice Holder::Holder operator new < > helper use use()::(lambda)::operator() use()::(lambda)::operator() \n"
    "  Function: use calls: operator new Holder::Holder twice twice use()::(lambda)::operator() use()::(lambda)::operator() \n"
    "  Function: use()::(lambda)::operator() calls: \n"
    "  Function: use()::(lambda)::operator() calls: helper \n"
    "  Function: operator new calls: \n"
    "  Function: < > calls: helper \n"
    "  Function: helper calls: \n"
    "  Function: Holder::Holder calls: \n"
    "  Function: twice calls: \n"
    "  Function: twice calls: \n"
)

NODES = """== p08_include.cpp: 12 nodes, 9 edges
node "operator new" kind=decl
node Base::Base kind=implicit
node Derived::Derived kind=implicit
node die kind=def
node fail kind=decl noreturn
node file_local kind=def static
node leaf kind=def
node twice<int> kind=tpl
node two_lambdas kind=def
node two_lambdas()::(lambda@L59)::operator() kind=lambda
node with_new kind=def
node <block@L70> kind=block
edge Derived::Derived -> Base::Base @L32 CXXConstructExpr kind=ctor
edge die -> fail @L17 CallExpr kind=call
edge file_local -> leaf @L24 CallExpr kind=call
edge two_lambdas -> two_lambdas()::(lambda@L59)::operator() @L61 CXXOperatorCallExpr kind=op
edge two_lambdas()::(lambda@L59)::operator() -> leaf @L59 CallExpr kind=call
edge with_new -> "operator new" @L49 CXXNewExpr kind=new
edge with_new -> twice<int> @L50 CallExpr kind=call
edge with_new -> twice<int> @L51 CallExpr kind=call
edge <block@L70> -> leaf @L70 CallExpr kind=call
"""

NODES_PLAIN = """== p08_basic.cpp: 5 nodes, 4 edges
node f
node fact
node g
node main
node unused
edge f -> g
edge fact -> fact
edge main -> f
edge main -> fact
"""

NODES_ROOT = """== p08_basic.cpp: 6 nodes, 9 edges
node <root>
node f
node g
node main
edge <root> -> f
edge <root> -> g
edge <root> -> main
edge f -> g
edge main -> f
"""

NODES_NO_EDGES = """== p08_basic.cpp: 5 nodes, 4 edges
node f
node fact
node g
node main
node unused
"""

EMIT_DOT = """digraph p08_basic {
  f [tooltip="kind=def"];
  fact [tooltip="kind=def", class="recursive"];
  g [tooltip="kind=def"];
  main [tooltip="kind=def", class="entry"];
  unused [tooltip="kind=def"];
  f -> g;
  fact -> fact [class="back"];
  main -> f;
  main -> fact;
}
"""

WALK_SCCS = """scc 0 cyclic self: fact
scc 1 cyclic mutual: is_even is_odd
scc 2 cyclic mutual: pang ping pong
scc 3 cyclic mutual: a b c
"""

WALK_REACH = """reach main: file_local helper leaf main
dead: Base::run callback_target helper_of_unused static_unused unused
"""

WALK_ORDER = """order rpo: main file_local helper leaf
reach main: file_local helper leaf main
"""

CALLERS = "callers leaf: Base::run file_local helper unused\n"
CALLERS_STAR = CALLERS + "callers* leaf: Base::run callback_target file_local helper helper_of_unused main static_unused unused\n"

# p09_walk manifests/p09_recursion.cpp --sccs --order=rpo --edges
RECURSION = """order rpo: main Grid::area measure Shape::area step a b c ping pong pang is_even is_odd fact
scc 0 cyclic self: fact
scc 1 cyclic mutual: is_even is_odd
scc 2 cyclic mutual: pang ping pong
scc 3 cyclic mutual: a b c
scc 4: step
scc 5: Shape::area
scc 6: measure
scc 7: Grid::area
scc 8: main
edge Grid::area -> measure
edge a -> b
edge b -> a
edge b -> c
edge c -> b
edge fact -> fact
edge is_even -> is_odd
edge is_odd -> is_even
edge main -> a
edge main -> fact
edge main -> is_even
edge main -> is_odd
edge main -> ping
edge main -> step
edge measure -> Shape::area
edge pang -> ping
edge ping -> pong
edge pong -> pang
"""

REACH_EDGES = """reach main: file_local helper leaf main
dead: Base::run callback_target helper_of_unused static_unused unused
edge Base::run -> leaf @L17 CallExpr
edge callback_target -> helper @L21 CallExpr
edge file_local -> leaf @L12 CallExpr
edge helper -> leaf @L5 CallExpr
edge helper_of_unused -> helper @L8 CallExpr
edge main -> file_local @L25 CallExpr
edge main -> helper @L25 CallExpr
edge static_unused -> file_local @L13 CallExpr
edge unused -> helper_of_unused @L9 CallExpr
edge unused -> leaf @L9 CallExpr
"""

SUMMARY_SINK = """scc 0: fail iter=1
summary fail: sink=always
scc 1: die iter=1
summary die: sink=always via fail
scc 3: guarded iter=1
summary guarded: sink=may via die
scc 4: safe iter=1
summary safe: sink=none
scc 6 cyclic: fact iter=1
trace scc 6 iter 1: fact=none
summary fact: sink=none
scc 8 cyclic: ring_a ring_b ring_c iter=4
trace scc 8 iter 1: ring_a=none ring_b=none ring_c=may
trace scc 8 iter 2: ring_a=none ring_b=may ring_c=may
trace scc 8 iter 3: ring_a=may ring_b=may ring_c=may
trace scc 8 iter 4: ring_a=may ring_b=may ring_c=may
summary ring_a: sink=may via ring_b
summary ring_b: sink=may via ring_c
summary ring_c: sink=may via fail
"""

SUMMARY_DEPTH = """scc 0: fail iter=1
summary fail: depth=0
scc 1: die iter=1
summary die: depth=1 via fail
scc 5 cyclic: retry iter=1
summary retry: depth=inf
scc 9: level4 iter=1
summary level4: depth=0
scc 10: level3 iter=1
summary level3: depth=1 via level4
scc 14: main iter=1
summary main: depth=2 via die
"""

SUMMARY_SINK_EDGES = SUMMARY_SINK + """scc 9: top iter=1
summary top: sink=none
edge always_dies -> die
edge always_dies -> fail
edge die -> fail
edge fact -> fact
edge guarded -> die
edge ring_a -> ring_b
edge ring_b -> ring_c
edge ring_c -> fail
edge ring_c -> ring_a
edge top -> safe
"""

SITES = """== main: 3 blocks, 7 sites
site main B1.2 Constructor Holder::Holder resolved
site main B1.33 Function external missing:decl
site main B1.39 Allocator "operator new" missing:decl
site main B1.43 Destructor Holder::~Holder missing:delete
site main B1.55 Destructor Holder::~Holder missing:implicit-dtor
== apply: 3 blocks, 1 sites
site apply B1.5 Function ? missing:indirect
== dispatch: 3 blocks, 1 sites
site dispatch B1.4 Function ? virtual static=Base::run
"""

SITES_SUCCS = """== branches: 10 blocks, 3 sites
site branches B7.6 Function helper resolved
site branches B6.6 Function leaf resolved
site branches B3.6 Function leaf resolved
succ branches B9 -> B8
succ branches B8 -> B7 T
succ branches B8 -> B6 F
succ branches B7 -> B5
succ branches B6 -> B5
succ branches B5 -> B4
succ branches B4 -> B3 T
succ branches B4 -> B1 F
succ branches B3 -> B2
succ branches B2 -> B4
succ branches B1 -> B0
"""

SITES_EMPTY = """== leaf: 3 blocks, 0 sites
== helper: 3 blocks, 0 sites
"""

SITES_WALK = """walk 0 main:B2
walk 0 main:B1
walk 0 main:B1 -> Holder::Holder
walk 1 Holder::Holder:B2
walk 1 Holder::Holder:B1
walk 1 Holder::Holder:B1 -> leaf
walk 2 leaf:B2
walk 2 leaf:B1
walk 1 Holder::Holder:B0
walk 0 main:B1 -> branches
walk 1 branches:B6
walk 1 branches:B6 -> leaf
walk 2 leaf:B2
walk 1 branches:B3
walk 1 branches:B3 -> leaf
walk 2 leaf:B2
"""

RESOLVE_VIRTUAL = """add call_final -> Final::run @L28 reason=devirt-final
add call_local -> Derived::run @L32 reason=devirt-static
add dispatch -> Derived::run @L26 reason=cha candidates=4
add dispatch -> Final::run @L26 reason=cha candidates=4
skip use_orphan @L48 reason=no-overrider
add use_shape -> Square::area @L42 reason=cha candidates=1
"""

RESOLVE_FNPTR = """add apply -> add_one @L11 reason=fnptr-sig candidates=3
add apply -> recurse_via_ptr @L11 reason=fnptr-sig candidates=3
add apply -> sub_one @L11 reason=fnptr-sig candidates=3
skip fire @L18 reason=unknown-type
add via_table -> add_one @L14 reason=fnptr-sig candidates=3
scc 4 cyclic mutual: apply recurse_via_ptr
"""

XTU = """[1/2] Processing file manifests/p11_xtu_a.cpp.
[2/2] Processing file manifests/p11_xtu_b.cpp.
== merged: 9 nodes, 9 edges, 2 tus
node a_fn kind=def tus=p11_xtu_a.cpp
node b_fn kind=def tus=p11_xtu_b.cpp
node cleanup kind=def tus=p11_xtu_b.cpp
node die kind=def tus=p11_xtu_b.cpp
node fail kind=decl noreturn tus=p11_xtu_b.cpp
node local@p11_xtu_a.cpp kind=def static tus=p11_xtu_a.cpp
node local@p11_xtu_b.cpp kind=def static tus=p11_xtu_b.cpp
node main kind=def tus=p11_xtu_a.cpp
node shared kind=def tus=p11_xtu_a.cpp,p11_xtu_b.cpp
edge a_fn -> b_fn @p11_xtu_a.cpp:5 xtu
edge a_fn -> local@p11_xtu_a.cpp @p11_xtu_a.cpp:5
edge b_fn -> a_fn @p11_xtu_b.cpp:7 xtu
edge b_fn -> local@p11_xtu_b.cpp @p11_xtu_b.cpp:7
edge cleanup -> die @p11_xtu_b.cpp:9
edge die -> fail @p11_xtu_b.cpp:8
edge local@p11_xtu_a.cpp -> shared @p11_xtu_a.cpp:4
edge local@p11_xtu_b.cpp -> shared @p11_xtu_b.cpp:4
edge main -> a_fn @p11_xtu_a.cpp:6
"""

XTU_SINGLE = """== merged: 5 nodes, 4 edges, 1 tus
node a_fn kind=def tus=p11_xtu_a.cpp
node b_fn kind=decl tus=p11_xtu_a.cpp
node local kind=def static tus=p11_xtu_a.cpp
node main kind=def tus=p11_xtu_a.cpp
node shared kind=def tus=p11_xtu_a.cpp
edge a_fn -> b_fn @p11_xtu_a.cpp:5
edge a_fn -> local @p11_xtu_a.cpp:5
edge local -> shared @p11_xtu_a.cpp:4
edge main -> a_fn @p11_xtu_a.cpp:6
unresolved b_fn (decl in p11_xtu.h)
"""

CHECK_PLAIN = """diag manifests/p11_xtu_a.cpp:5: recursion: a_fn -> b_fn -> a_fn
diag manifests/p11_xtu_b.cpp:8: reaches-sink: die -> fail
diag manifests/p11_xtu_b.cpp:9: reaches-sink: cleanup -> die -> fail
summary: 8 functions, 2 recursive, 2 reach a sink
"""

CHECK_TRACE = """[1/2] Processing file manifests/p11_xtu_a.cpp.
[2/2] Processing file manifests/p11_xtu_b.cpp.
diag manifests/p11_xtu_a.cpp:5: recursion: a_fn -> b_fn -> a_fn
trace recursion: a_fn -> b_fn @manifests/p11_xtu_a.cpp:5
trace recursion: b_fn -> a_fn @manifests/p11_xtu_b.cpp:7
diag manifests/p11_xtu_b.cpp:8: reaches-sink: die -> fail
trace reaches-sink: die -> fail @manifests/p11_xtu_b.cpp:8
diag manifests/p11_xtu_b.cpp:9: reaches-sink: cleanup -> die -> fail
trace reaches-sink: cleanup -> die @manifests/p11_xtu_b.cpp:9
trace reaches-sink: die -> fail @manifests/p11_xtu_b.cpp:8
summary: 8 functions, 2 recursive, 2 reach a sink
"""


# --- the call-graph track: real outputs of build/bin/<tool>, trimmed where the comment says so
BUILD_IMPLICIT = """flags implicit=0 instantiations=1 typelocs=0 lambda-body=1
== p08_include.cpp: 35 nodes, 31 edges
node <block@L70> door=block
node Base::run door=def
node Derived::Derived door=callee
node blk_now door=def
node calls_decl door=def
node declared_only door=callee
node die door=def
node fail door=callee
node "operator new" door=callee
node with_new door=def
edge Base::run -> leaf
edge blk_now -> <block@L70>
edge calls_decl -> declared_only
edge die -> fail
edge with_new -> "operator new"
diff: nodes 37 -> 35 (-Base::Base -Derived::~Derived) edges 33 -> 31
"""  # p08_build manifests/p08_include.cpp --implicit=0 --doors --edges, trimmed
BUILD_INSTANTIATIONS = """flags implicit=1 instantiations=0 typelocs=0 lambda-body=1
== p08_include.cpp: 37 nodes, 29 edges
node use_tpl door=def
edge use_tpl -> twice<double>
edge use_tpl -> twice<int>
diff: nodes 37 -> 37 edges 33 -> 29
"""  # --instantiations=0 --doors --edges --func=use_tpl
BUILD_INCREMENTAL = """flags implicit=1 instantiations=1 typelocs=0 lambda-body=1
after fact: size=2 new: fact
after is_even: size=4 new: is_even is_odd
after is_odd: size=4 new: -
after ping: size=6 new: ping pong
after pong: size=7 new: pang
after pang: size=7 new: -
after a: size=9 new: a b
after b: size=10 new: c
after c: size=10 new: -
after step: size=11 new: step
after Shape::area: size=12 new: Shape::area
after measure: size=13 new: measure
after Grid::area: size=14 new: Grid::area
after main: size=15 new: main
== p09_recursion.cpp: 14 nodes, 18 edges
"""  # --incremental on p09_recursion.cpp, without the node list
ANYCALL_MAIN = """call main @L32 CXXConstructExpr kind=Constructor decl=Counter::Counter params=0 ret=void
call main @L33 CallExpr kind=Function decl=twice params=1 ret=int ident=twice
call main @L34 CXXMemberCallExpr kind=Function decl=Counter::bump params=1 ret=int ident=bump
call main @L35 CXXOperatorCallExpr kind=Function decl=Counter::operator() params=1 ret=int
call main @L36 CallExpr kind=Function decl=? params=0 ret=int
call main @L37 CXXConstructExpr kind=Constructor decl=Holder::Holder params=1 ret=void
call main @L38 CXXNewExpr kind=Allocator decl="operator new" params=1 ret=void *
call main @L38 CXXConstructExpr kind=Constructor decl=Holder::Holder params=1 ret=void
call main @L39 CXXDeleteExpr kind=Deallocator decl="operator delete" params=2 ret=void
call main @L40 CXXConstructExpr kind=Constructor decl=Derived::Base params=1 ret=void
call main @L42 CXXOperatorCallExpr kind=Function decl=main()::(lambda@L41)::operator() params=1 ret=int
call main @L43 CallExpr kind=Function decl=through_block params=1 ret=int ident=through_block
call main @L44 CallExpr kind=Block decl=<block@L44> params=1 ret=int
kinds: Function=6 ObjCMethod=0 Block=1 Destructor=0 Constructor=4 InheritedConstructor=0 Allocator=1 Deallocator=1
"""  # p08_anycall manifests/p08_calls.cpp --func=main --kinds
MINE_DIFF = """== p08_include.cpp: lib 37 nodes 33 edges, mine 43 nodes 40 edges
node "?(int (*)(int))" in=mine
node "?(int (^)(int))" in=mine
node Member::m in=mine
node __inline_helper in=mine
node "operator delete" in=mine
node twice in=mine
edge Member::Member -> helper only-lib @L54
edge Member::m -> helper only-mine @L54
edge blk_var -> "?(int (^)(int))" only-mine @L67
edge dflt -> leaf only-mine @L52
edge indirect -> "?(int (*)(int))" only-mine @L75
edge twice -> helper only-mine @L20
edge twice -> helper only-mine @L20
edge use_default -> leaf only-lib @L52
edge uses_inline_helper -> __inline_helper only-mine @L9
edge with_new -> Holder::~Holder only-mine @L49
edge with_new -> "operator delete" only-mine @L49
"""  # p08_mine manifests/p08_include.cpp | grep -v both
METRICS_REACH = """metric Base::run in=0 out=1 sites=1 height=1 scc=6 dead root
metric callback_target in=0 out=1 sites=1 height=2 scc=7 dead root
metric file_local in=2 out=1 sites=1 height=1 scc=4
metric helper in=3 out=1 sites=1 height=1 scc=1
metric helper_of_unused in=1 out=1 sites=1 height=2 scc=2 dead
metric leaf in=4 out=0 sites=0 height=0 scc=0 leaf
metric main in=0 out=2 sites=2 height=2 scc=8 root
metric static_unused in=0 out=1 sites=1 height=2 scc=5 dead root
metric unused in=0 out=2 sites=2 height=3 scc=3 dead root
edge Base::run -> leaf
edge callback_target -> helper
edge file_local -> leaf
edge helper -> leaf
edge helper_of_unused -> helper
edge main -> file_local
edge main -> helper
edge static_unused -> file_local
edge unused -> helper_of_unused
edge unused -> leaf
totals: functions=9 edges=10 sites=10 sccs=9 cyclic=0 dead=5 leaves=1 roots=5 height=3
"""  # p09_metrics manifests/p09_reach.cpp --edges
WALK_BFS = """order bfs main: main@0 helper@1 file_local@1 leaf@2
edge Base::run -> leaf
edge callback_target -> helper
edge file_local -> leaf
edge helper -> leaf
edge helper_of_unused -> helper
edge main -> file_local
edge main -> helper
edge static_unused -> file_local
edge unused -> helper_of_unused
edge unused -> leaf
"""
WALK_BFS_REVERSE = """order bfs reverse leaf: leaf@0 Base::run@1 file_local@1 helper@1 unused@1 main@2 static_unused@2 callback_target@2 helper_of_unused@2
edge Base::run -> leaf
edge callback_target -> helper
edge file_local -> leaf
edge helper -> leaf
edge helper_of_unused -> helper
edge main -> file_local
edge main -> helper
edge static_unused -> file_local
edge unused -> helper_of_unused
edge unused -> leaf
"""
WALK_PO_REVERSE = """order po reverse: Base::run callback_target main static_unused file_local unused helper_of_unused helper leaf
edge Base::run -> leaf
edge callback_target -> helper
edge file_local -> leaf
edge helper -> leaf
edge helper_of_unused -> helper
edge main -> file_local
edge main -> helper
edge static_unused -> file_local
edge unused -> helper_of_unused
edge unused -> leaf
"""
WALK_DFS = """order dfs main: main helper leaf file_local
edge Base::run -> leaf
edge callback_target -> helper
edge file_local -> leaf
edge helper -> leaf
edge helper_of_unused -> helper
edge main -> file_local
edge main -> helper
edge static_unused -> file_local
edge unused -> helper_of_unused
edge unused -> leaf
"""
PATH = """path main -> guarded -> die -> fail (3 calls)
"""
PATHS = """path main -> guarded -> die -> fail (3 calls)
path main -> ping -> pong -> die -> fail (4 calls)
paths: 2 shown, 2 found, limit 8
"""
PATH_NONE = """path fail -> main: none
"""
CYCLE = """cycle ring_a -> ring_b -> ring_c -> ring_a
"""
CYCLE_NONE = """cycle safe: none
"""
PATH_EDGES = """path main -> guarded -> die -> fail (3 calls)
edge always_dies -> die
edge always_dies -> fail
edge die -> fail
edge fact -> fact
edge guarded -> die
edge level1 -> level2
edge level2 -> level3
edge level3 -> level4
edge main -> fact
edge main -> guarded
edge main -> ping
edge main -> top
edge ping -> pong
edge pong -> die
edge pong -> ping
edge retry -> fail
edge retry -> retry
edge ring_a -> ring_b
edge ring_b -> ring_c
edge ring_c -> fail
edge ring_c -> ring_a
edge top -> level1
edge top -> safe
"""
CS_K1_TRACE = """ctx k=1 divide[…,pass_on@L20]: a=nonzero b=nonzero
trace divide[…,pass_on@L20] <- pass_on[…,forward@L21] @L20: a=nonzero b=nonzero
ctx k=1 divide[…,risky@L18]: a=top b=zero
trace divide[…,risky@L18] <- risky[main@L29] @L18: a=nonzero b=zero
trace divide[…,risky@L18] <- risky[…,loop@L24] @L18: a=top b=zero
warn p10_callstrings.cpp:14: divide[…,risky@L18] divides by b=zero
ctx k=1 divide[…,safe@L16]: a=nonzero b=nonzero
trace divide[…,safe@L16] <- safe[main@L30] @L16: a=nonzero b=nonzero
ctx k=1 forward[main@L31]: v=nonzero
trace forward[main@L31] <- main[] @L31: v=nonzero
ctx k=1 loop[main@L32]: n=nonzero
trace loop[main@L32] <- main[] @L32: n=nonzero
ctx k=1 loop[…,loop@L25]: n=top
trace loop[…,loop@L25] <- loop[main@L32] @L25: n=top
trace loop[…,loop@L25] <- loop[…,loop@L25] @L25: n=top
ctx k=1 main[]: -
ctx k=1 pass_on[…,forward@L21]: d=nonzero
trace pass_on[…,forward@L21] <- forward[main@L31] @L21: d=nonzero
ctx k=1 risky[main@L29]: x=nonzero
trace risky[main@L29] <- main[] @L29: x=nonzero
ctx k=1 risky[…,loop@L24]: x=top
trace risky[…,loop@L24] <- loop[main@L32] @L24: x=nonzero
trace risky[…,loop@L24] <- loop[…,loop@L25] @L24: x=top
ctx k=1 safe[main@L30]: -
trace safe[main@L30] <- main[] @L30: -
summary: k=1 contexts=11 functions=7 warnings=1
"""  # p10_callstrings manifests/p10_callstrings.cpp --k=1 --trace
CS_K1_EDGES = """ctx k=1 divide[…,pass_on@L20]: a=nonzero b=nonzero
ctx k=1 divide[…,risky@L18]: a=top b=zero
warn p10_callstrings.cpp:14: divide[…,risky@L18] divides by b=zero
ctx k=1 divide[…,safe@L16]: a=nonzero b=nonzero
ctx k=1 forward[main@L31]: v=nonzero
ctx k=1 loop[main@L32]: n=nonzero
ctx k=1 loop[…,loop@L25]: n=top
ctx k=1 main[]: -
ctx k=1 pass_on[…,forward@L21]: d=nonzero
ctx k=1 risky[main@L29]: x=nonzero
ctx k=1 risky[…,loop@L24]: x=top
ctx k=1 safe[main@L30]: -
summary: k=1 contexts=11 functions=7 warnings=1
edge forward -> pass_on
edge loop -> loop
edge loop -> risky
edge main -> forward
edge main -> loop
edge main -> risky
edge main -> safe
edge pass_on -> divide
edge risky -> divide
edge safe -> divide
"""  # --k=1 --edges
CS_K0 = """ctx k=0 divide[]: a=top b=top
warn p10_callstrings.cpp:14: divide[] divides by b=top
ctx k=0 forward[]: v=nonzero
ctx k=0 loop[]: n=top
ctx k=0 main[]: -
ctx k=0 pass_on[]: d=nonzero
ctx k=0 risky[]: x=top
ctx k=0 safe[]: -
summary: k=0 contexts=7 functions=7 warnings=1
"""  # --k=0: ctx, warn and summary only
FARM_C = """farm clang_analyzer_eval: synthesized=no
farm dispatch_once: synthesized=yes kind=dispatch_once
if (*predicate != ~0L) {
    *predicate = ~0L;
    block();
}

 [B3 (ENTRY)]
   Succs (1): B2

 [B1]
   1: *predicate = ~0L
   2: block()
   Preds (1): B2
   Succs (1): B0

 [B2]
   1: *predicate != ~0L
   T: if [B2.1]
   Preds (1): B3
   Succs (2): B1 B0

 [B0 (EXIT)]
   Preds (2): B1 B2

farm main: synthesized=no
"""  # p10_farm manifests/p10_farm.c -- -fblocks
FARM_CPP = """farm clang_analyzer_eval: synthesized=no
farm "std::call_once<void (&)(int &),<int &>>": synthesized=yes kind=call_once
if (!flag.__state_) {
    f(args);
    flag.__state_ = 1;
}

 [B3 (ENTRY)]
   Succs (1): B2

 [B1]
   1: f(args)
   2: flag.__state_ = 1
   Preds (1): B2
   Succs (1): B0

 [B2]
   1: !flag.__state_
   T: if [B2.1]
   Preds (1): B3
   Succs (2): B1 B0

 [B0 (EXIT)]
   Preds (2): B1 B2

farm set: synthesized=no
farm main: synthesized=no
"""  # p10_farm manifests/p10_farm.cpp
INDEX_REFS = """== p11_fnptr.cpp: 8 call occurrences, 5 references
edge apply -> f@param @L11 roles=Call
edge call_wide -> g@param @L16 roles=Call
edge fire -> h@param @L18 roles=Call
edge main -> apply @L23 roles=Call
edge main -> call_wide @L23 roles=Call
edge main -> recurse_via_ptr @L23 roles=Call
edge main -> via_table @L23 roles=Call
edge recurse_via_ptr -> apply @L21 roles=Call
ref main -> add_one @L23
ref main -> widen @L23
ref recurse_via_ptr -> recurse_via_ptr @L21
ref table -> add_one @L13
ref table -> sub_one @L13
"""  # p11_index manifests/p11_fnptr.cpp --refs
INDEX_DIFF = """== p08_include.cpp: 29 call occurrences, 1 references
edge <block@L66> -> leaf @L66 only-graph
edge <block@L70> -> helper @L70 only-graph
edge Derived::Derived -> Base::Base @L32 only-graph
edge Member::Member -> helper @L54 only-graph
edge Member::m -> helper @L54 roles=Call,NoRelCall only-index
edge blk_now -> <block@L70> @L70 only-graph
edge blk_now -> helper @L70 roles=Call only-index
edge blk_var -> b@var @L67 roles=Call only-index
edge blk_var -> leaf @L66 roles=Call only-index
edge dflt -> leaf @L52 roles=Call,NoRelCall only-index
edge indirect -> fp@var @L75 roles=Call only-index
edge twice -> helper @L20 roles=Call only-index
edge twice -> helper @L20 roles=Call only-index
edge twice<double> -> helper @L20 only-graph
edge twice<double> -> helper @L20 only-graph
edge twice<int> -> helper @L20 only-graph
edge twice<int> -> helper @L20 only-graph
edge two_lambdas -> helper @L60 roles=Call only-index
edge two_lambdas -> leaf @L59 roles=Call only-index
edge two_lambdas()::(lambda@L59)::operator() -> leaf @L59 only-graph
edge two_lambdas()::(lambda@L60)::operator() -> helper @L60 only-graph
edge use_default -> leaf @L52 only-graph
edge use_derived -> Derived::Derived @L35 only-graph
edge use_member -> Member::Member @L55 only-graph
edge use_tpl -> twice @L21 roles=Call only-index
edge use_tpl -> twice @L21 roles=Call only-index
edge use_tpl -> twice<double> @L21 only-graph
edge use_tpl -> twice<int> @L21 only-graph
edge uses_inline_helper -> __inline_helper @L9 roles=Call only-index
edge with_new -> "operator new" @L49 only-graph
ref fp -> leaf @L74
diff: only-index=13 only-graph=17 both=16
"""  # p11_index manifests/p08_include.cpp --diff | grep -v ' both$'
RESOLVE_RTA = """instantiated: Derived Final Square
add call_final -> Derived::run @L28 reason=rta candidates=3
add call_final -> Final::run @L28 reason=rta candidates=3
add call_local -> Derived::run @L32 reason=rta candidates=3
add call_local -> Final::run @L32 reason=rta candidates=3
add dispatch -> Derived::run @L26 reason=rta candidates=3
add dispatch -> Final::run @L26 reason=rta candidates=3
skip use_orphan @L48 reason=no-overrider
add use_shape -> Square::area @L42 reason=rta candidates=1
stats: edges 21 -> 28, indirect sites 5, resolved 4
"""  # p11_resolve manifests/p11_virtual.cpp --rta --counts
XTU_LOAD = """== merged: 9 nodes, 9 edges, 2 tus from 2 json files
node a_fn kind=def tus=p11_xtu_a.cpp
node b_fn kind=def tus=p11_xtu_b.cpp
node cleanup kind=def tus=p11_xtu_b.cpp
node die kind=def tus=p11_xtu_b.cpp
node fail kind=decl noreturn tus=p11_xtu_b.cpp
node local@p11_xtu_a.cpp kind=def static tus=p11_xtu_a.cpp
node local@p11_xtu_b.cpp kind=def static tus=p11_xtu_b.cpp
node main kind=def tus=p11_xtu_a.cpp
node shared kind=def tus=p11_xtu_a.cpp,p11_xtu_b.cpp
edge a_fn -> b_fn @p11_xtu_a.cpp:5 xtu
edge a_fn -> local@p11_xtu_a.cpp @p11_xtu_a.cpp:5
edge b_fn -> a_fn @p11_xtu_b.cpp:7 xtu
edge b_fn -> local@p11_xtu_b.cpp @p11_xtu_b.cpp:7
edge cleanup -> die @p11_xtu_b.cpp:9
edge die -> fail @p11_xtu_b.cpp:8
edge local@p11_xtu_a.cpp -> shared @p11_xtu_a.cpp:4
edge local@p11_xtu_b.cpp -> shared @p11_xtu_b.cpp:4
edge main -> a_fn @p11_xtu_a.cpp:6
"""  # p11_xtu --load a.json b.json --edges
TIDY_RAW = """manifests/p09_recursion.cpp:4:5: warning: function 'fact' is within a recursive call chain [misc-no-recursion]
    4 | int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
      |     ^
manifests/p09_recursion.cpp:4:5: note: example recursive call chain, starting from function 'fact'
manifests/p09_recursion.cpp:4:43: note: Frame #1: function 'fact' calls function 'fact' here:
    4 | int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
      |                                           ^
manifests/p09_recursion.cpp:4:43: note: ... which was the starting point of the recursive call chain; there may be other cycles
manifests/p09_recursion.cpp:8:5: warning: function 'is_even' is within a recursive call chain [misc-no-recursion]
    8 | int is_even(int n) { return n == 0 ? 1 : is_odd(n - 1); }
      |     ^
manifests/p09_recursion.cpp:9:5: note: example recursive call chain, starting from function 'is_odd'
    9 | int is_odd(int n) { return n == 0 ? 0 : is_even(n - 1); }
      |     ^
manifests/p09_recursion.cpp:9:41: note: Frame #1: function 'is_odd' calls function 'is_even' here:
    9 | int is_odd(int n) { return n == 0 ? 0 : is_even(n - 1); }
      |                                         ^
manifests/p09_recursion.cpp:8:42: note: Frame #2: function 'is_even' calls function 'is_odd' here:
    8 | int is_even(int n) { return n == 0 ? 1 : is_odd(n - 1); }
      |                                          ^
manifests/p09_recursion.cpp:8:42: note: ... which was the starting point of the recursive call chain; there may be other cycles
manifests/p09_recursion.cpp:9:5: warning: function 'is_odd' is within a recursive call chain [misc-no-recursion]
    9 | int is_odd(int n) { return n == 0 ? 0 : is_even(n - 1); }
      |     ^
manifests/p09_recursion.cpp:14:5: warning: function 'ping' is within a recursive call chain [misc-no-recursion]
   14 | int ping(int n) { return n == 0 ? 0 : pong(n - 1); }
      |     ^
manifests/p09_recursion.cpp:16:5: note: example recursive call chain, starting from function 'pang'
   16 | int pang(int n) { return ping(n); }
      |     ^
manifests/p09_recursion.cpp:16:26: note: Frame #1: function 'pang' calls function 'ping' here:
   16 | int pang(int n) { return ping(n); }
      |                          ^
manifests/p09_recursion.cpp:14:39: note: Frame #2: function 'ping' calls function 'pong' here:
   14 | int ping(int n) { return n == 0 ? 0 : pong(n - 1); }
      |                                       ^
manifests/p09_recursion.cpp:15:26: note: Frame #3: function 'pong' calls function 'pang' here:
   15 | int pong(int n) { return pang(n); }
      |                          ^
manifests/p09_recursion.cpp:15:26: note: ... which was the starting point of the recursive call chain; there may be other cycles
"""  # clang-tidy -checks='-*,misc-no-recursion' manifests/p09_recursion.cpp, paths made relative, cut after the ping chain
TIDY_FILTERED = """manifests/p09_recursion.cpp:4:5: warning: function 'fact' is within a recursive call chain [misc-no-recursion]
manifests/p09_recursion.cpp:4:43: note: Frame #1: function 'fact' calls function 'fact' here:
manifests/p09_recursion.cpp:8:5: warning: function 'is_even' is within a recursive call chain [misc-no-recursion]
manifests/p09_recursion.cpp:9:41: note: Frame #1: function 'is_odd' calls function 'is_even' here:
manifests/p09_recursion.cpp:8:42: note: Frame #2: function 'is_even' calls function 'is_odd' here:
manifests/p09_recursion.cpp:9:5: warning: function 'is_odd' is within a recursive call chain [misc-no-recursion]
manifests/p09_recursion.cpp:14:5: warning: function 'ping' is within a recursive call chain [misc-no-recursion]
manifests/p09_recursion.cpp:16:26: note: Frame #1: function 'pang' calls function 'ping' here:
manifests/p09_recursion.cpp:14:39: note: Frame #2: function 'ping' calls function 'pong' here:
manifests/p09_recursion.cpp:15:26: note: Frame #3: function 'pong' calls function 'pang' here:
manifests/p09_recursion.cpp:15:5: warning: function 'pong' is within a recursive call chain [misc-no-recursion]
manifests/p09_recursion.cpp:16:5: warning: function 'pang' is within a recursive call chain [misc-no-recursion]
manifests/p09_recursion.cpp:21:5: warning: function 'a' is within a recursive call chain [misc-no-recursion]
manifests/p09_recursion.cpp:22:36: note: Frame #1: function 'b' calls function 'a' here:
manifests/p09_recursion.cpp:21:36: note: Frame #2: function 'a' calls function 'b' here:
manifests/p09_recursion.cpp:22:5: warning: function 'b' is within a recursive call chain [misc-no-recursion]
manifests/p09_recursion.cpp:23:5: warning: function 'c' is within a recursive call chain [misc-no-recursion]
"""  # ... | grep -E 'warning|Frame' | sed -E 's|^.*/manifests/|manifests/|'
LIST_ORDER = """PreCall (forward_ptr) [SimpleFunctionCall]
PreCall (read_ptr) [SimpleFunctionCall]
PreCall (helper) [SimpleFunctionCall]
PostCall (helper) [SimpleFunctionCall]
PreCall (clang_analyzer_eval) [SimpleFunctionCall]
PostCall (clang_analyzer_eval) [SimpleFunctionCall]
PreCall (big) [SimpleFunctionCall]
PostCall (big) [SimpleFunctionCall]
"""
LIST_DUMPCALLS = """manifests/p10_analyzer.cpp:37:31: warning: Dereference of null pointer (loaded from variable 'p') [core.NullDereference]
   37 | int read_ptr(int *p) { return *p; }
      |                               ^~
1 warning generated.
forward_ptr(nullptr) read_ptr(q)helper(1)Returning 2 S32b
clang_analyzer_eval(helper(1) == 2)Returning void
big(0)Returning 0 S32b
clang_analyzer_eval(big(0) == 0)Returning void
"""
LIST_STATS = """manifests/p10_analyzer.cpp:22:27: warning: run -> Total CFGBlocks: 3 | Unreachable CFGBlocks: 0 | Exhausted Block: no | Empty WorkList: yes [debug.Stats]
   22 | struct Base { virtual int run(int x) { return x; } virtual ~Base() {} };
      |                           ^          ~~~~~~~~~~~~~
manifests/p10_analyzer.cpp:28:5: warning: main -> Total CFGBlocks: 3 | Unreachable CFGBlocks: 0 | Exhausted Block: no | Empty WorkList: yes [debug.Stats]
   28 | int main() {
      |     ^      ~
   29 |   clang_analyzer_eval(helper(1) == 2);
      |   ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
"""
LIST_PROGRESS = """ANALYZE (Syntax): manifests/p09_reach.cpp leaf(int)
ANALYZE (Syntax): manifests/p09_reach.cpp helper(int)
ANALYZE (Syntax): manifests/p09_reach.cpp helper_of_unused(int)
ANALYZE (Syntax): manifests/p09_reach.cpp unused(int)
ANALYZE (Syntax): manifests/p09_reach.cpp file_local(int)
ANALYZE (Syntax): manifests/p09_reach.cpp static_unused(int)
ANALYZE (Syntax): manifests/p09_reach.cpp Base::run(int)
ANALYZE (Syntax): manifests/p09_reach.cpp callback_target(int)
"""
LIST_EVAL = """manifests/p10_analyzer.cpp:29:3: warning: TRUE [debug.ExprInspection]
   29 |   clang_analyzer_eval(helper(1) == 2);
      |   ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
manifests/p10_analyzer.cpp:30:3: warning: TRUE [debug.ExprInspection]
   30 |   clang_analyzer_eval(big(0) == 0);
      |   ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
manifests/p10_analyzer.cpp:32:3: warning: TRUE [debug.ExprInspection]
   32 |   clang_analyzer_eval(dispatch(d) == 11);
"""
LIST_EXTDEF = """14:c:@F@from_b#I# p11_ctu_b.cpp
16:c:@F@deref_b#*I# p11_ctu_b.cpp
"""
METRICS_TOP_EDGES = """metric a in=2 out=1 sites=1 height=inf scc=3 recursive
metric b in=2 out=2 sites=2 height=inf scc=3 recursive
metric fact in=2 out=1 sites=1 height=inf scc=0 recursive
edge a -> b
edge b -> a
edge fact -> fact
totals: functions=14 edges=18 sites=18 sccs=9 cyclic=4 dead=3 leaves=2 roots=2 height=inf
"""  # p09_metrics manifests/p09_recursion.cpp --top=3 --by=in --edges

BASIC_CMD = "CHECKER=debug.DumpCallGraph scripts/dumpcfg.sh manifests/p08_basic.cpp"


# --------------------------------------------------------------------------
# helpers
# --------------------------------------------------------------------------
def to_dot(text: str, cmd: str) -> str | None:
    return outviz.to_dot(text, cmd)


def edges(dot: str) -> list[tuple[str, str, str]]:
    """(tail, head, attribute text) of every visible edge statement."""
    return [(a, b, attrs) for a, b, attrs in re.findall(r'"([^"]+)" -> "([^"]+)"(?: \[([^\]]*)\])?;', dot) if "invis" not in attrs]


def node(dot: str, name: str) -> str:
    """The attribute text of a node statement."""
    m = re.search(r'^\s*"' + re.escape(name) + r'" \[(.*)\];$', dot, re.M)
    assert m, f"no node {name!r} in\n{dot}"
    return m[1]


def classes(dot: str, name: str) -> list[str]:
    m = re.search(r'class="([^"]*)"', node(dot, name))
    return m[1].split() if m else []


def edge_classes(dot: str, a: str, b: str) -> list[str]:
    attrs = next(x for s, t, x in edges(dot) if (s, t) == (a, b))
    m = re.search(r'class="([^"]*)"', attrs)
    return m[1].split() if m else []


def edge_label(dot: str, a: str, b: str) -> str:
    attrs = next(x for s, t, x in edges(dot) if (s, t) == (a, b))
    m = re.search(r'label="([^"]*)"', attrs)
    return m[1] if m else ""


def pattern(parser: str) -> re.Pattern:
    """The command pattern registered for the parser of that name (`_nodes` -> "nodes")."""
    return next(p for p, fn in cg.PARSERS if fn.__name__ == parser)


def render(dot: str) -> tuple[int, str, float]:
    """(dot's exit status, its warnings, natural width in px) with the flags the site build uses."""
    p = subprocess.run(["dot", "-Tsvg", *build_site.DOT_FLAGS], input=dot, capture_output=True, text=True)
    vb = re.search(r'viewBox="[\d.\-]+ [\d.\-]+ ([\d.\-]+) ', p.stdout)
    return p.returncode, p.stderr.strip(), float(vb[1]) * 96 / 72 if vb else 0.0


class CgCase(unittest.TestCase):
    def assertDrawn(self, dot: str | None, max_px: int = 900) -> str:
        """A figure that dot draws without a warning, no wider than the page, with class= only (no colors or fonts)."""
        self.assertIsNotNone(dot)
        rc, err, px = render(dot)
        self.assertEqual((rc, err), (0, ""), dot)
        self.assertLessEqual(px, max_px, dot)
        self.assertNotRegex(dot, r"\b(?:color|fillcolor|fontcolor|pencolor|bgcolor|fontname|fontsize)\s*=")
        return dot


# --------------------------------------------------------------------------
# debug.DumpCallGraph
# --------------------------------------------------------------------------
class Dump(CgCase):
    def test_root_fan_out_recursion_and_repeated_callee(self):
        dot = self.assertDrawn(to_dot(BASIC_DUMP, BASIC_CMD))
        self.assertIn("root", classes(dot, "< root >"))
        self.assertEqual(edge_classes(dot, "< root >", "main"), ["weak"])
        self.assertEqual(len([e for e in edges(dot) if e[0] == "< root >"]), 5)  # F1: an edge to every node
        self.assertEqual(edge_label(dot, "f", "g"), "×2")  # `f calls: g g`
        self.assertEqual(edge_classes(dot, "fact", "fact"), ["back"])
        self.assertEqual(classes(dot, "fact"), ["recursive"])
        self.assertEqual(classes(dot, "main"), [])
        self.assertEqual(edge_classes(dot, "main", "f"), [])

    def test_part_1_6_block(self):
        dot = self.assertDrawn(to_dot(" --- Call graph Dump ---\n  Function: < root > calls: f g\n  Function: f calls: g\n  Function: g calls:\n", BASIC_CMD))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("< root >", "f"), ("< root >", "g"), ("f", "g")})

    def test_trailing_spaces_are_clang_s_and_do_not_matter(self):
        spaced = "".join(ln + " \n" for ln in BASIC_DUMP.splitlines())
        self.assertEqual(to_dot(spaced, BASIC_CMD), to_dot(BASIC_DUMP, BASIC_CMD))

    def test_names_with_a_space_and_names_that_print_alike(self):
        dot = self.assertDrawn(to_dot(INCLUDE_DUMP, BASIC_CMD))
        self.assertIn(("use", "operator new", ""), edges(dot))  # `operator new` is one callee, not `operator` and `new`
        self.assertIn(("< >", "helper", ""), edges(dot))
        self.assertEqual(edge_label(dot, "use", "twice"), "×2")  # two instantiations, one printed name
        self.assertIn("(2 nodes)", node(dot, "twice"))
        self.assertIn("(2 nodes)", node(dot, "use()::(lambda)::operator()"))
        self.assertEqual(len(edges(dot)), 13)

    def test_cg_nodes_dump_is_the_same_text(self):
        self.assertEqual(to_dot(BASIC_DUMP, "build/bin/p08_nodes manifests/p08_basic.cpp --dump"), to_dot(BASIC_DUMP, BASIC_CMD))

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot(" --- Call graph Dump ---\n  Function: < root > calls:\n", BASIC_CMD))  # an empty TU
        self.assertIsNone(to_dot("Writing '/tmp/CallGraph-abc.dot'...  done.\n", "clang --analyze -Xclang -analyzer-checker=debug.DumpCallGraph"))
        self.assertIsNone(to_dot(BASIC_DUMP + "t.cpp:3:1: warning: something\n", BASIC_CMD))  # a line that is not part of the dump


# --------------------------------------------------------------------------
# p08_nodes
# --------------------------------------------------------------------------
class Nodes(CgCase):
    CMD = "build/bin/p08_nodes manifests/p08_include.cpp --edges --sites --kinds -- -std=c++17 -fblocks"

    def test_kinds_flags_and_sites(self):
        dot = self.assertDrawn(to_dot(NODES, self.CMD))
        self.assertEqual(classes(dot, "fail"), ["dim", "sink"])  # a declaration that is [[noreturn]]
        self.assertIn("decl, noreturn", node(dot, "fail"))
        self.assertIn("tpl", node(dot, "twice<int>"))
        self.assertIn("static", node(dot, "file_local"))
        self.assertEqual(classes(dot, "operator new"), ["dim"])  # the quoted name is one node
        self.assertEqual(edge_label(dot, "with_new", "operator new"), "new @L49")  # a kind that is not `call` is spelled out
        self.assertEqual(edge_label(dot, "Derived::Derived", "Base::Base"), "ctor @L32")
        self.assertEqual(edge_label(dot, "die", "fail"), "@L17")
        self.assertEqual(edge_label(dot, "with_new", "twice<int>"), "@L50, @L51")  # two sites, one edge
        self.assertIn("2 call sites", dot)
        self.assertIn("CallExpr", dot)  # the expression class stays in a tooltip

    def test_long_names_wrap_and_keep_the_full_name_in_the_tooltip(self):
        dot = self.assertDrawn(to_dot(NODES, self.CMD))
        attrs = node(dot, "two_lambdas()::(lambda@L59)::operator()")
        self.assertIn("lambda", attrs)
        self.assertRegex(attrs, r'label="two_lambdas\(\)::\\n\(lambda@L59\)::operator\(\)\\nlambda"')
        self.assertIn('tooltip="two_lambdas()::(lambda@L59)::operator()\\nkind=lambda"', attrs)

    def test_without_kinds_or_sites(self):
        dot = self.assertDrawn(to_dot(NODES_PLAIN, "build/bin/p08_nodes manifests/p08_basic.cpp --edges"))
        self.assertEqual(edge_classes(dot, "fact", "fact"), ["back"])
        self.assertEqual(classes(dot, "fact"), ["recursive"])
        self.assertEqual(edge_label(dot, "main", "f"), "")
        self.assertIn("p08_basic.cpp", dot)  # the header's file is the title

    def test_with_root(self):
        dot = self.assertDrawn(to_dot(NODES_ROOT, "build/bin/p08_nodes manifests/p08_basic.cpp --edges --with-root"))
        self.assertEqual(classes(dot, "<root>"), ["root"])
        self.assertEqual(edge_classes(dot, "<root>", "f"), ["weak"])

    def test_function_slice_has_only_its_out_edges(self):
        dot = self.assertDrawn(to_dot("== p08_include.cpp: 1 nodes, 2 edges\nnode use_tpl kind=def\nedge use_tpl -> twice<double> @L21 CallExpr kind=call\nedge use_tpl -> twice<int> @L21 CallExpr kind=call\n", self.CMD + " --func=use_tpl"))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("use_tpl", "twice<double>"), ("use_tpl", "twice<int>")})

    def test_objc_and_blocks(self):
        text = ("== p08_objc.m: 4 nodes, 3 edges\nnode <block@L18> kind=block\nnode Counter::bump: kind=objc\nnode immediate kind=def\nnode send kind=def\n"
                "edge <block@L18> -> leaf kind=call\nedge immediate -> <block@L18> kind=block\nedge send -> Counter::bump: kind=objc\n")
        dot = self.assertDrawn(to_dot(text, "build/bin/p08_nodes manifests/p08_objc.m --edges --kinds -- -x objective-c -fblocks -w"))
        self.assertEqual(edge_label(dot, "send", "Counter::bump:"), "objc")
        self.assertEqual(edge_label(dot, "immediate", "<block@L18>"), "block")

    def test_a_usr_is_a_tooltip_and_usr_dash_is_none(self):
        dot = to_dot("== a.cpp: 2 nodes, 1 edges\nnode f kind=def usr=c:@F@f#\nnode g kind=def usr=-\nedge f -> g\n", "build/bin/p08_nodes a.cpp --edges --usr")
        self.assertIn("usr=c:@F@f#", node(dot, "f"))
        self.assertNotIn("usr=-", dot)

    def test_the_view_line_is_not_a_graph(self):
        self.assertIsNone(to_dot("view: out/view/CallGraph-0a1b2c.dot\n", "TMPDIR=out/view PATH=/var/empty build/bin/p08_nodes manifests/p08_include.cpp --view"))

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot(NODES_NO_EDGES, "build/bin/p08_nodes manifests/p08_basic.cpp"))  # no edge
        self.assertIsNone(to_dot(NODES + "a.cpp:1:1: warning: unused\n", self.CMD))  # an unknown line
        self.assertIsNone(to_dot('{"edges": [], "nodes": []}\n', "build/bin/p08_nodes a.cpp --emit=json"))
        self.assertIsNone(to_dot(NODES_PLAIN, "build/bin/p11_xtu_other manifests/p08_basic.cpp"))  # not a p08 tool

    def test_a_tool_name_inside_a_file_name_is_not_the_tool(self):
        self.assertIsNone(pattern("nodes").search("cat manifests/p08_nodes.h"))
        self.assertIsNone(pattern("xtu").search("cat manifests/p11_xtu.h manifests/p11_xtu_a.cpp"))
        self.assertTrue(pattern("xtu").search("scripts/run.sh p11_xtu manifests/p11_xtu_a.cpp"))
        self.assertTrue(pattern("xtu").search("build/bin/p11_xtu manifests/p11_xtu_a.cpp"))
        self.assertIsNone(pattern("xtu").search("build/bin/p11_xtu_other manifests/p11_xtu_a.cpp"))  # a longer name is another tool


class EmittedDot(CgCase):
    def test_a_complete_emit_dot_is_returned_as_it_is(self):
        dot = self.assertDrawn(to_dot(EMIT_DOT, "build/bin/p08_nodes manifests/p08_basic.cpp --emit=dot"))
        self.assertEqual(dot, EMIT_DOT)

    def test_a_cut_one_gets_its_closing_braces(self):
        cut = "\n".join(EMIT_DOT.splitlines()[:8]) + "\n"  # `| head -8` stops inside the edge list: digraph ... f -> g;
        dot = self.assertDrawn(to_dot(cut, "build/bin/p08_nodes manifests/p08_basic.cpp --emit=dot | head -8"))
        self.assertTrue(dot.rstrip().endswith("}"))
        self.assertEqual(re.findall(r"^  (\w+) -> (\w+)", dot, re.M), [("f", "g"), ("fact", "fact")])

    def test_a_cut_without_an_edge_is_nothing(self):
        cut = "\n".join(EMIT_DOT.splitlines()[:4]) + "\n"
        self.assertIsNone(to_dot(cut, "build/bin/p08_nodes manifests/p08_basic.cpp --emit=dot | head -4"))

    def test_the_site_never_sees_a_call_graph_as_a_cfg(self):
        # cfg.py draws any `digraph` it finds as a CFG; cg claims the output of its tools first
        self.assertEqual(to_dot(EMIT_DOT, "build/bin/p08_nodes manifests/p08_basic.cpp --emit=dot"), EMIT_DOT)
        self.assertIn('class="back"', to_dot(EMIT_DOT, "build/bin/p08_nodes manifests/p08_basic.cpp --emit=dot"))


# --------------------------------------------------------------------------
# p09_walk
# --------------------------------------------------------------------------
class Walk(CgCase):
    CMD = "build/bin/p09_walk manifests/p09_reach.cpp --callers=leaf"

    def test_callers_draw_the_reverse_star(self):
        dot = self.assertDrawn(to_dot(CALLERS, self.CMD))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {(c, "leaf") for c in ("Base::run", "file_local", "helper", "unused")})
        self.assertEqual(classes(dot, "leaf"), ["hl"])
        self.assertEqual(edge_classes(dot, "helper", "leaf"), [])

    def test_transitive_callers_are_weak_and_never_repeat_a_direct_one(self):
        dot = self.assertDrawn(to_dot(CALLERS_STAR, self.CMD + " --transitive"))
        self.assertEqual(len(edges(dot)), 8)
        self.assertEqual(edge_classes(dot, "helper", "leaf"), [])  # direct: a call edge
        self.assertEqual(edge_label(dot, "helper", "leaf"), "")  # not "x2"
        self.assertEqual(edge_classes(dot, "main", "leaf"), ["weak"])  # transitive: a relation
        self.assertIn("transitive caller", dot)

    def test_scc_order_reach_and_dead_alone_have_no_edge(self):
        for text, cmd in ((WALK_SCCS, "build/bin/p09_walk manifests/p09_recursion.cpp --sccs --cyclic"),
                          (WALK_REACH, "build/bin/p09_walk manifests/p09_reach.cpp --from=main --dead"),
                          (WALK_ORDER, "build/bin/p09_walk manifests/p09_reach.cpp --from=main --order=rpo")):
            self.assertIsNone(to_dot(text, cmd), cmd)

    def test_scc_clusters_and_orders_over_the_edges(self):
        dot = self.assertDrawn(to_dot(RECURSION, "build/bin/p09_walk manifests/p09_recursion.cpp --sccs --order=rpo --edges"))
        self.assertIn('subgraph cluster_scc3 {\n    label="scc 3 cyclic mutual"; labeljust=l;\n    class="scc";', dot)
        cluster = dot.split("subgraph cluster_scc3 {")[1].split("  }")[0]
        for member in ("a", "b", "c"):
            self.assertIn("recursive", classes(dot, member))
            self.assertIn(f'"{member}" [', cluster)  # inside the cluster
        self.assertNotIn('"main" [', cluster)
        self.assertNotIn("cluster_scc0", dot)  # a function that calls itself is a self edge, not a cluster
        self.assertEqual(classes(dot, "fact"), ["recursive"])
        self.assertEqual(edge_classes(dot, "fact", "fact"), ["back"])
        self.assertEqual(edge_classes(dot, "b", "a"), ["back"])  # DFS from main enters the SCC at a
        self.assertEqual(edge_classes(dot, "c", "b"), ["back"])
        self.assertEqual(edge_classes(dot, "a", "b"), [])
        self.assertIn('xlabel="rpo 0"', node(dot, "main"))  # the list position: <root> is not printed
        self.assertIn('xlabel="rpo 13"', node(dot, "fact"))
        self.assertNotIn("scc 4", dot)  # a trivial SCC is no cluster

    def test_dead_and_reach_over_the_edges(self):
        dot = self.assertDrawn(to_dot(REACH_EDGES, "build/bin/p09_walk manifests/p09_reach.cpp --from=main --dead --edges --sites"))
        self.assertEqual(classes(dot, "main"), ["entry"])
        for name in ("Base::run", "callback_target", "helper_of_unused", "static_unused", "unused"):
            self.assertEqual(classes(dot, name), ["dim"], name)
        self.assertEqual(classes(dot, "leaf"), [])
        self.assertEqual(edge_label(dot, "helper", "leaf"), "@L5")

    def test_an_empty_list_is_a_dash(self):
        self.assertIsNone(to_dot("callers main: -\n", "build/bin/p09_walk manifests/p09_reach.cpp --callers=main"))
        dot = to_dot("dead: -\nedge a -> b\nreach a: a b\n", "build/bin/p09_walk x.cpp --from=a --dead --edges")
        self.assertNotIn('"-"', dot)

    def test_reach_marks_the_start_and_dims_what_it_did_not_reach(self):
        text = "edge main -> helper\nedge helper -> leaf\nedge unused -> leaf\nreach main: helper leaf main\n"
        dot = self.assertDrawn(to_dot(text, "build/bin/p09_walk manifests/p09_reach.cpp --from=main --edges"))
        self.assertEqual(classes(dot, "main"), ["entry"])
        self.assertEqual(classes(dot, "unused"), ["dim"])
        self.assertEqual(classes(dot, "helper"), [])


# --------------------------------------------------------------------------
# p10_summary
# --------------------------------------------------------------------------
class Summary(CgCase):
    def test_sink_witness_chains_clusters_and_trace(self):
        dot = self.assertDrawn(to_dot(SUMMARY_SINK, "build/bin/p10_summary manifests/p10_sink.cpp --prop=sink --trace"))
        self.assertEqual(classes(dot, "fail"), ["sink"])  # always, and no `via`: the sink itself
        self.assertEqual(classes(dot, "die"), ["hl"])  # always, through fail
        self.assertEqual(classes(dot, "guarded"), [])  # may
        self.assertEqual(classes(dot, "fact"), ["recursive", "dim"])  # none, cyclic
        self.assertIn("sink=may", node(dot, "ring_a"))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("die", "fail"), ("guarded", "die"), ("ring_a", "ring_b"), ("ring_b", "ring_c"), ("ring_c", "fail")})
        self.assertIn('label="scc 8 cyclic iter=4"', dot)
        self.assertIn('label="scc 6 cyclic iter=1"', dot)
        self.assertIn("scc 8 iter 1: ring_a=none", node(dot, "ring_a"))  # --trace
        self.assertIn("scc 8 iter 3: ring_a=may", node(dot, "ring_a"))
        self.assertNotIn('"safe"', dot)  # none, no via, no cycle: nothing to hang it on

    def test_depth_chain_and_inf(self):
        dot = self.assertDrawn(to_dot(SUMMARY_DEPTH, "build/bin/p10_summary manifests/p10_sink.cpp --prop=depth"))
        self.assertIn("depth=1", node(dot, "level3"))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("die", "fail"), ("level3", "level4"), ("main", "die")})
        self.assertIn("recursive", classes(dot, "retry"))
        self.assertIn("longest chain", dot)

    def test_the_whole_graph_with_the_witness_chain_emphasised(self):
        dot = self.assertDrawn(to_dot(SUMMARY_SINK_EDGES, "build/bin/p10_summary manifests/p10_sink.cpp --prop=sink --edges"))
        self.assertEqual(edge_classes(dot, "die", "fail"), ["hl"])  # `die: sink=always via fail`
        self.assertEqual(edge_classes(dot, "always_dies", "fail"), [])  # a call, but not the witness
        self.assertEqual(edge_label(dot, "die", "fail"), "")  # not "x2": the line and the via are one edge
        self.assertEqual(edge_classes(dot, "ring_c", "ring_a"), ["back"])  # the cycle edge only the full graph has
        self.assertEqual(edge_classes(dot, "fact", "fact"), ["back"])
        self.assertIn('"top"', dot)  # it has an edge now

    def test_a_sink_named_on_the_command_line_is_the_sink(self):
        text = "scc 0: helper iter=1\nsummary helper: sink=always\nscc 1: top iter=1\nsummary top: sink=may via helper\n"
        dot = to_dot(text, "build/bin/p10_summary manifests/p10_sink.cpp --prop=sink --sink=helper")
        self.assertIn("sink", classes(dot, "helper"))

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("scc 0: safe iter=1\nsummary safe: sink=none\nscc 1: level4 iter=1\nsummary level4: depth=0\n", "build/bin/p10_summary a.cpp"))
        self.assertIsNone(to_dot(SUMMARY_DEPTH + "summary x: depth=banana\n", "build/bin/p10_summary a.cpp"))


# --------------------------------------------------------------------------
# p10_sites
# --------------------------------------------------------------------------
class Sites(CgCase):
    CMD = "build/bin/p10_sites manifests/p10_sites.cpp --unresolved --preset=analyzer"

    def test_a_cluster_per_function_and_an_edge_per_status(self):
        dot = self.assertDrawn(to_dot(SITES, self.CMD))
        for fn in ("main", "apply", "dispatch"):
            self.assertIn(f'label="{fn}"; labeljust=l;', dot)
        self.assertEqual(edge_classes(dot, "main:B1.2", "Holder::Holder"), ["call"])  # resolved
        self.assertEqual(edge_classes(dot, "main:B1.55", "Holder::~Holder"), ["weak"])
        self.assertEqual(edge_label(dot, "main:B1.55", "Holder::~Holder"), "missing implicit-dtor")
        self.assertEqual(edge_label(dot, "main:B1.43", "Holder::~Holder"), "missing delete")
        self.assertEqual(classes(dot, "external"), ["dim"])  # missing:decl: a callee with no body
        self.assertEqual(classes(dot, "operator new"), ["dim"])
        self.assertEqual(edge_classes(dot, "apply:B1.5", "?apply:B1.5"), ["indirect"])  # no callee: a `?`
        self.assertEqual(classes(dot, "?apply:B1.5"), ["note"])

    def test_a_virtual_site_goes_to_its_static_callee(self):
        dot = self.assertDrawn(to_dot(SITES, self.CMD))
        self.assertEqual(edge_classes(dot, "dispatch:B1.4", "Base::run"), ["virtual"])
        self.assertEqual(edge_label(dot, "dispatch:B1.4", "Base::run"), "virtual")
        self.assertNotIn('"?dispatch', dot)

    def test_succ_lines_draw_the_blocks_with_their_calls(self):
        dot = self.assertDrawn(to_dot(SITES_SUCCS, "build/bin/p10_sites manifests/p10_sites.cpp --func=branches --succs --preset=analyzer"))
        self.assertIn("B9 (ENTRY)", node(dot, "branches:B9"))
        self.assertEqual(classes(dot, "branches:B9"), ["entry"])
        self.assertEqual(classes(dot, "branches:B0"), ["exit"])
        self.assertEqual(classes(dot, "branches:B8"), ["cond"])  # a two-way branch
        self.assertEqual(edge_classes(dot, "branches:B8", "branches:B7"), ["t"])
        self.assertEqual(edge_label(dot, "branches:B8", "branches:B6"), "F")
        self.assertEqual(edge_classes(dot, "branches:B2", "branches:B4"), ["back"])  # the loop
        self.assertIn("6: helper", node(dot, "branches:B7"))  # the call site's element, in its block
        self.assertEqual(edge_classes(dot, "branches:B7", "helper"), ["call"])
        self.assertEqual(edge_label(dot, "branches:B7", "helper"), "B7.6")  # out of a block the edge names its element
        self.assertEqual(len([e for e in edges(dot) if e[1] == "leaf"]), 2)

    def test_succ_lines_of_a_virtual_site(self):
        text = "== dispatch: 3 blocks, 1 sites\nsite dispatch B1.4 Function ? virtual static=Base::run\nsucc dispatch B2 -> B1\nsucc dispatch B1 -> B0\n"
        dot = self.assertDrawn(to_dot(text, "build/bin/p10_sites manifests/p10_sites.cpp --func=dispatch --succs"))
        self.assertEqual(edge_classes(dot, "dispatch:B1", "Base::run"), ["virtual"])
        self.assertEqual(edge_label(dot, "dispatch:B1", "Base::run"), "B1.4 virtual")

    def test_clusters_are_stacked_without_ordering_out(self):
        dot = to_dot(SITES, self.CMD)
        self.assertIn("style=invis", dot)
        self.assertNotIn("ordering=out", dot)  # dot 15 asserts on both together

    def test_functions_without_a_site_are_no_figure(self):
        self.assertIsNone(to_dot(SITES_EMPTY, "build/bin/p10_sites manifests/p10_sites.cpp"))

    def test_walk_lines_are_the_descents(self):
        dot = self.assertDrawn(to_dot(SITES_WALK, "build/bin/p10_sites manifests/p10_sites.cpp --walk=main --depth=2 --preset=analyzer"))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("main", "Holder::Holder"), ("Holder::Holder", "leaf"), ("main", "branches"), ("branches", "leaf")})
        self.assertEqual(edge_label(dot, "branches", "leaf"), "B6, B3")  # the blocks that call it
        self.assertEqual(edge_classes(dot, "main", "branches"), ["call"])
        self.assertEqual(classes(dot, "main"), ["entry"])

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("walk 0 main:B2\nwalk 0 main:B1\n", "build/bin/p10_sites manifests/p10_sites.cpp --walk=main"))  # no descent
        self.assertIsNone(to_dot(SITES + SITES_WALK, self.CMD))  # sites and a walk in one block
        self.assertIsNone(to_dot(SITES + "site main B1.9 Function f surprising\n", self.CMD))  # a status this module does not know


# --------------------------------------------------------------------------
# p11_resolve
# --------------------------------------------------------------------------
class Resolve(CgCase):
    def test_added_edges_by_reason(self):
        dot = self.assertDrawn(to_dot(RESOLVE_VIRTUAL, "build/bin/p11_resolve manifests/p11_virtual.cpp --all"))
        self.assertEqual(edge_classes(dot, "dispatch", "Derived::run"), ["cha"])
        self.assertEqual(edge_label(dot, "dispatch", "Derived::run"), "cha @L26")
        self.assertIn("4 candidates", dot)
        self.assertEqual(edge_classes(dot, "call_final", "Final::run"), ["hl"])
        self.assertEqual(edge_label(dot, "call_final", "Final::run"), "devirt final @L28")
        self.assertEqual(edge_label(dot, "call_local", "Derived::run"), "devirt @L32")
        self.assertIn("skip @L48: no-overrider", node(dot, "use_orphan"))  # a skip is a tooltip on its caller

    def test_fnptr_edges_and_the_cycle_they_close(self):
        dot = self.assertDrawn(to_dot(RESOLVE_FNPTR, "build/bin/p11_resolve manifests/p11_fnptr.cpp --fnptr --sccs"))
        self.assertEqual(edge_classes(dot, "apply", "add_one"), ["indirect"])
        self.assertEqual(edge_label(dot, "apply", "add_one"), "fnptr @L11")
        self.assertIn('label="scc 4 cyclic mutual"', dot)
        self.assertEqual(classes(dot, "recurse_via_ptr"), ["recursive"])
        self.assertIn("skip @L18: unknown-type", node(dot, "fire"))

    def test_stats_are_the_title(self):
        text = RESOLVE_FNPTR + "stats: edges 5 -> 9, indirect sites 4, resolved 3\n"
        dot = to_dot(text, "build/bin/p11_resolve manifests/p11_fnptr.cpp --fnptr --counts --sccs")
        self.assertIn('label="edges 5 -> 9, indirect sites 4, resolved 3"; labelloc=t;', dot)

    def test_base_edges_come_first(self):
        text = "edge dispatch -> Base::run @L26 CXXMemberCallExpr kind=call\n" + RESOLVE_VIRTUAL
        dot = self.assertDrawn(to_dot(text, "build/bin/p11_resolve manifests/p11_virtual.cpp --all"))
        self.assertEqual(edge_classes(dot, "dispatch", "Base::run"), [])

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("stats: edges 5 -> 5, indirect sites 4, resolved 0\n", "build/bin/p11_resolve manifests/p11_fnptr.cpp --counts"))
        self.assertIsNone(to_dot("skip fire @L18 reason=unknown-type\n", "build/bin/p11_resolve manifests/p11_fnptr.cpp --fnptr"))
        self.assertIsNone(to_dot("scc 4 cyclic mutual: apply recurse_via_ptr\n", "build/bin/p11_resolve manifests/p11_fnptr.cpp --sccs"))
        self.assertIsNone(to_dot(RESOLVE_VIRTUAL + "add a -> b @L1 reason=new-rule\n", "build/bin/p11_resolve x.cpp"))


# --------------------------------------------------------------------------
# p11_xtu, p11_check
# --------------------------------------------------------------------------
class Xtu(CgCase):
    CMD = "build/bin/p11_xtu manifests/p11_xtu_a.cpp manifests/p11_xtu_b.cpp --edges"

    def test_one_cluster_per_translation_unit_and_the_shared_function_outside(self):
        dot = self.assertDrawn(to_dot(XTU, self.CMD))
        self.assertIn('label="p11_xtu_a.cpp"; labeljust=l;\n    class="tu";', dot)
        self.assertIn('label="p11_xtu_b.cpp"; labeljust=l;\n    class="tu";', dot)
        a_cluster = dot.split("subgraph cluster_tu1 {")[1].split("  }")[0]
        b_cluster = dot.split("subgraph cluster_tu2 {")[1].split("  }")[0]
        self.assertIn('"main"', a_cluster)
        self.assertIn('"local@p11_xtu_a.cpp"', a_cluster)
        self.assertIn('"cleanup"', b_cluster)
        self.assertNotIn('"shared"', a_cluster + b_cluster)  # defined in both: it belongs to neither
        self.assertEqual(classes(dot, "shared"), ["hl"])

    def test_cross_file_edges_and_the_cycle(self):
        dot = self.assertDrawn(to_dot(XTU, self.CMD))
        self.assertEqual(edge_classes(dot, "a_fn", "b_fn"), ["xtu"])
        self.assertEqual(sorted(edge_classes(dot, "b_fn", "a_fn")), ["back", "xtu"])  # closes the cycle a_fn -> b_fn -> a_fn
        self.assertEqual(edge_classes(dot, "a_fn", "local@p11_xtu_a.cpp"), [])
        self.assertEqual(classes(dot, "a_fn"), ["recursive"])
        self.assertIn("p11_xtu_b.cpp:7", dot)  # the call site, in the tooltip
        self.assertEqual(classes(dot, "fail"), ["dim", "sink", "external"])  # declaration only, [[noreturn]]
        self.assertEqual({n for n in re.findall(r'"(local@[^"]+)" \[label', dot)}, {"local@p11_xtu_a.cpp", "local@p11_xtu_b.cpp"})  # two statics, two nodes

    def test_a_declaration_with_no_definition_is_external(self):
        dot = self.assertDrawn(to_dot(XTU_SINGLE, "build/bin/p11_xtu manifests/p11_xtu_a.cpp --edges --unresolved 2>/dev/null"))
        self.assertEqual(classes(dot, "b_fn"), ["dim", "external"])
        self.assertIn("decl in p11_xtu.h", node(dot, "b_fn"))
        self.assertEqual(edge_classes(dot, "a_fn", "b_fn"), [])  # no xtu edge: nothing defines it

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("== merged: 1 nodes, 0 edges, 1 tus\nnode a_fn kind=def tus=a.cpp\n", self.CMD))
        self.assertIsNone(to_dot(XTU + "something else\n", self.CMD))


class Check(CgCase):
    CMD = "build/bin/p11_check manifests/p11_xtu_a.cpp manifests/p11_xtu_b.cpp"

    def test_plain_diag_output_is_a_list_of_diagnostics(self):
        self.assertIsNone(to_dot(CHECK_PLAIN, self.CMD))

    def test_trace_draws_the_cycle_and_the_sink_chain(self):
        dot = self.assertDrawn(to_dot(CHECK_TRACE, self.CMD + " --trace"))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("a_fn", "b_fn"), ("b_fn", "a_fn"), ("die", "fail"), ("cleanup", "die")})
        self.assertEqual(edge_classes(dot, "b_fn", "a_fn"), ["back"])
        self.assertEqual(classes(dot, "a_fn"), ["recursive"])
        self.assertEqual(classes(dot, "fail"), ["sink"])  # the end of the chain, not `die`
        self.assertEqual(classes(dot, "die"), [])
        self.assertIn("p11_xtu_b.cpp:7", dot)

    def test_a_trace_without_trace_lines_still_has_its_diag_paths(self):
        dot = to_dot("\n".join(ln for ln in CHECK_TRACE.splitlines() if not ln.startswith("trace")) + "\n", self.CMD + " --trace")
        self.assertEqual(edge_classes(dot, "b_fn", "a_fn"), ["back"])
        self.assertEqual(classes(dot, "fail"), ["sink"])

    def test_a_clean_run_has_nothing_to_draw(self):
        self.assertIsNone(to_dot("summary: 4 functions, 0 recursive, 0 reach a sink\n", self.CMD + " --trace"))


# --------------------------------------------------------------------------
# the call-graph track: p08_build, p08_anycall, p08_mine, p09_metrics
# --------------------------------------------------------------------------
class Build(CgCase):
    CMD = "build/bin/p08_build manifests/p08_include.cpp --implicit=0 --doors --edges -- -std=c++17 -fblocks"
    INCR = "build/bin/p08_build manifests/p09_recursion.cpp --incremental -- -std=c++17"

    def test_the_door_decides_external_and_the_flags_are_the_title(self):
        dot = self.assertDrawn(to_dot(BUILD_IMPLICIT, self.CMD))
        for name in ("fail", "declared_only", "Derived::Derived", "operator new"):
            self.assertEqual(classes(dot, name), ["external"], name)  # entered by the callee door only
        self.assertIn("door=callee", node(dot, "fail"))
        self.assertEqual(classes(dot, "Base::run"), [])  # a definition: the door includeInGraph
        self.assertEqual(classes(dot, "<block@L70>"), [])
        self.assertIn('label="implicit=0 instantiations=1 typelocs=0 lambda-body=1 (nodes 37 -> 35, edges 33 -> 31)"; labelloc=t;', dot)
        self.assertNotIn("Base::~Base", dot)  # the names `diff:` says are gone: the text gives no edge of theirs

    def test_instantiations_off_keeps_the_nodes_and_loses_edges(self):
        dot = self.assertDrawn(to_dot(BUILD_INSTANTIATIONS, self.CMD + " --func=use_tpl"))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("use_tpl", "twice<double>"), ("use_tpl", "twice<int>")})
        self.assertIn("instantiations=0", dot)
        self.assertIn("nodes 37 -> 37, edges 33 -> 29", dot)

    def test_incremental_is_a_row_per_step_in_the_order_they_ran(self):
        dot = self.assertDrawn(to_dot(BUILD_INCREMENTAL, self.INCR))
        self.assertIn("rankdir=LR", dot)  # a table of rows, not offered the other layout
        self.assertEqual(len(re.findall(r'class="api"', dot)), 14)  # one node per `after` line
        self.assertEqual(edge_classes(dot, "after is_even", "is_even"), ["weak"])
        self.assertEqual(edge_classes(dot, "after is_even", "is_odd"), ["weak"])
        self.assertEqual([b for a, b, _ in edges(dot) if a == "after is_odd"], [])  # `new: -`: the step adds nothing
        self.assertIn("size=4", node(dot, "after is_odd"))
        self.assertIn('"after is_even" -> "after is_odd" [style=invis]', dot)  # the order, not a relation
        self.assertRegex(dot, r'\{ rank=same; "after fact"; "after is_even"; "after is_odd"')
        self.assertNotIn("ordering=out", dot)  # dot 15 asserts on it together with invisible edges
        self.assertIn('label="implicit=1 instantiations=1 typelocs=0 lambda-body=1"', dot)

    def test_with_the_graph_the_steps_become_tooltips(self):
        dot = self.assertDrawn(to_dot(BUILD_INCREMENTAL + "edge is_even -> is_odd\nedge is_odd -> is_even\n", self.INCR + " --edges"))
        self.assertNotIn('"after is_even"', dot)
        self.assertIn("new after addToCallGraph(is_even), size=4", node(dot, "is_odd"))
        self.assertEqual(edge_classes(dot, "is_odd", "is_even"), ["back"])

    def test_nothing_to_draw(self):
        flags = "flags implicit=0 instantiations=1 typelocs=0 lambda-body=1\n"
        self.assertIsNone(to_dot(flags + "diff: nodes 37 -> 35 (-Base::Base) edges 33 -> 31\n", self.CMD))  # `flags` / `diff:` alone
        too_many = "".join(f"after f{i}: size={i + 2} new: f{i}\n" for i in range(cg.MAX_STEPS + 1))
        self.assertIsNone(to_dot(too_many, self.INCR))  # a table of 17 rows is not a figure
        self.assertIsNone(to_dot(BUILD_IMPLICIT + "t.cpp:1:1: warning: unused\n", self.CMD))  # an unknown line


class AnyCall(CgCase):
    CMD = "build/bin/p08_anycall manifests/p08_calls.cpp --func=main --kinds -- -std=c++17 -fblocks"

    def test_a_star_with_the_kind_and_the_line_on_each_edge(self):
        dot = self.assertDrawn(to_dot(ANYCALL_MAIN, self.CMD))
        self.assertTrue(all(a == "main" for a, _, _ in edges(dot)))
        self.assertEqual(len(edges(dot)), 12)  # 13 call lines, two of them the same edge (Holder::Holder)
        self.assertEqual(edge_label(dot, "main", "Counter::Counter"), "Constructor @L32")
        self.assertEqual(edge_label(dot, "main", "twice"), "Function @L33")
        self.assertEqual(edge_label(dot, "main", "operator new"), "Allocator @L38")
        self.assertEqual(edge_label(dot, "main", "<block@L44>"), "Block @L44")
        self.assertEqual(edge_label(dot, "main", "Holder::Holder"), "Constructor @L37, @L38")
        self.assertEqual(edge_classes(dot, "main", "twice"), [])  # a plain call
        self.assertIn("CXXMemberCallExpr, params=1 ret=int ident=bump", dot)  # the expression class and the signature: a tooltip

    def test_a_pointer_call_has_no_callee(self):
        dot = self.assertDrawn(to_dot(ANYCALL_MAIN, self.CMD))
        self.assertEqual(classes(dot, "?main@L36"), ["dim"])
        self.assertIn('label="?"', node(dot, "?main@L36"))
        self.assertEqual(edge_classes(dot, "main", "?main@L36"), ["indirect"])  # decl=?: getDecl() is null

    def test_the_deallocator_is_a_quiet_edge_and_a_type_keeps_its_spaces(self):
        dot = self.assertDrawn(to_dot(ANYCALL_MAIN, self.CMD))
        self.assertEqual(edge_classes(dot, "main", "operator delete"), ["weak"])  # nothing the source names
        self.assertIn("params=2", dot)  # the sized deallocation function
        self.assertIn("ret=void *", dot)  # a type is not quoted and has a space

    def test_the_title_is_the_kinds_that_occur(self):
        dot = to_dot(ANYCALL_MAIN, self.CMD)
        self.assertIn('label="kinds: Function=6 Block=1 Constructor=4 Allocator=1 Deallocator=1"; labelloc=t;', dot)  # the zeros are left out

    def test_decl_lines_add_to_the_tooltips(self):
        text = "decl twice kind=Function params=1 ret=int ident=twice\n" + ANYCALL_MAIN
        self.assertIn("decl: kind=Function params=1 ret=int", node(to_dot(text, self.CMD + " --decls"), "twice"))

    def test_nothing_to_draw(self):
        decls = "decl main kind=Function params=0 ret=int\ndecl twice kind=Function params=1 ret=int ident=twice\n"
        self.assertIsNone(to_dot(decls, self.CMD))  # --decls alone
        self.assertIsNone(to_dot("kinds: Function=6 ObjCMethod=0 Block=1 Destructor=0 Constructor=4 InheritedConstructor=0 Allocator=1 Deallocator=1\n", self.CMD))
        self.assertIsNone(to_dot(ANYCALL_MAIN + "something else\n", self.CMD))


class Mine(CgCase):
    CMD = "build/bin/p08_mine manifests/p08_include.cpp -- -std=c++17 -fblocks | grep -v both"

    def test_the_word_of_an_edge_is_its_class(self):
        dot = self.assertDrawn(to_dot(MINE_DIFF, self.CMD))
        for a, b in (("Member::m", "helper"), ("dflt", "leaf"), ("with_new", "operator delete"), ("with_new", "Holder::~Holder"), ("uses_inline_helper", "__inline_helper")):
            self.assertEqual(edge_classes(dot, a, b), ["hl"], (a, b))  # only-mine: what CGBuilder leaves out
        for a, b in (("Member::Member", "helper"), ("use_default", "leaf")):
            self.assertEqual(edge_classes(dot, a, b), ["weak"], (a, b))  # only-lib: the default-argument charge, at the use
        self.assertIn("only-mine, @L49", dot)  # the word is in the tooltip

    def test_a_pointer_call_goes_to_a_dim_pseudo_node(self):
        dot = self.assertDrawn(to_dot(MINE_DIFF, self.CMD))
        self.assertEqual(classes(dot, "?(int (*)(int))"), ["dim"])  # quoted in the text: it has spaces
        self.assertEqual(sorted(edge_classes(dot, "indirect", "?(int (*)(int))")), ["hl", "indirect"])
        self.assertEqual(classes(dot, "?(int (^)(int))"), ["dim"])  # a block variable: one node per callee type

    def test_an_edge_both_have_is_plain(self):
        text = "== a.cpp: lib 2 nodes 1 edges, mine 2 nodes 1 edges\nnode a in=both\nnode b in=both\nedge a -> b both @L3\n"
        dot = self.assertDrawn(to_dot(text, self.CMD))
        self.assertEqual(edge_classes(dot, "a", "b"), [])
        self.assertIn("in=both", node(dot, "a"))
        self.assertIn('label="a.cpp"', dot)

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("diff: only-lib=2 only-mine=9 both=31\n", self.CMD))
        self.assertIsNone(to_dot("== a.cpp: lib 1 nodes 0 edges, mine 1 nodes 0 edges\nnode a in=both\n", self.CMD))
        self.assertIsNone(to_dot(MINE_DIFF + "edge a -> b maybe @L1\n", self.CMD))  # a word this module does not know


class Metrics(CgCase):
    CMD = "build/bin/p09_metrics manifests/p09_reach.cpp --edges"

    def test_labels_classes_and_the_totals_title(self):
        dot = self.assertDrawn(to_dot(METRICS_REACH, self.CMD))
        self.assertIn("in 4 out 0 sites 0", node(dot, "leaf").replace("\\n", " "))  # the label may be wrapped by the layout that fits
        self.assertEqual(classes(dot, "leaf"), [])
        self.assertEqual(classes(dot, "main"), ["entry"])  # a root: nobody calls it
        self.assertEqual(classes(dot, "unused"), ["dim", "entry"])  # dead, and a root
        self.assertEqual(classes(dot, "helper_of_unused"), ["dim"])  # dead, but called
        self.assertEqual(len(edges(dot)), 10)
        self.assertIn("height=3", node(dot, "unused"))  # the metrics that do not fit the label are in the tooltip
        self.assertIn("scc=0", node(dot, "leaf"))
        self.assertIn('label="functions=9 edges=10 sites=10 sccs=9 cyclic=0 dead=5 leaves=1 roots=5 height=3"; labelloc=t;', dot)

    def test_recursion(self):
        dot = self.assertDrawn(to_dot(METRICS_TOP_EDGES, "build/bin/p09_metrics manifests/p09_recursion.cpp --top=3 --by=in --edges"))
        self.assertEqual(classes(dot, "a"), ["recursive"])
        self.assertIn("height=inf", node(dot, "fact"))
        self.assertEqual(edge_classes(dot, "fact", "fact"), ["back"])
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("a", "b"), ("b", "a"), ("fact", "fact")})  # only what --top kept

    def test_nothing_to_draw(self):
        top = "metric a in=2 out=1 sites=1 height=inf scc=3 recursive\ntotals: functions=14 edges=18 sites=18 sccs=9 cyclic=4 dead=3 leaves=2 roots=2 height=inf\n"
        self.assertIsNone(to_dot(top, "build/bin/p09_metrics manifests/p09_recursion.cpp --top=3 --by=in"))  # --top without --edges
        self.assertIsNone(to_dot("totals: functions=1 edges=0 sites=0 sccs=1 cyclic=0 dead=0 leaves=1 roots=1 height=0\n", "build/bin/p09_metrics a.cpp"))
        self.assertIsNone(to_dot(METRICS_REACH + "oops\n", self.CMD))


# --------------------------------------------------------------------------
# p09_walk: orders over the generic iterators, witness paths, cycles
# --------------------------------------------------------------------------
class WalkTrack(CgCase):
    CMD = "build/bin/p09_walk manifests/p09_reach.cpp --edges"
    SINK = "build/bin/p09_walk manifests/p10_sink.cpp"

    def test_bfs_says_the_level_and_marks_what_it_did_not_reach(self):
        dot = self.assertDrawn(to_dot(WALK_BFS, self.CMD + " --order=bfs --from=main"))
        self.assertIn('xlabel="bfs level 0"', node(dot, "main"))
        self.assertIn('xlabel="bfs level 1"', node(dot, "helper"))
        self.assertIn('xlabel="bfs level 2"', node(dot, "leaf"))
        self.assertEqual(classes(dot, "main"), ["entry"])
        self.assertEqual(classes(dot, "unused"), ["dim"])  # not reached from main
        self.assertEqual(classes(dot, "helper"), [])

    def test_dfs_says_the_index(self):
        dot = self.assertDrawn(to_dot(WALK_DFS, self.CMD + " --order=dfs --from=main"))
        self.assertIn('xlabel="dfs 1"', node(dot, "helper"))
        self.assertIn('xlabel="dfs 3"', node(dot, "file_local"))  # main helper leaf file_local

    def test_reverse_orders_say_so(self):
        dot = self.assertDrawn(to_dot(WALK_PO_REVERSE, self.CMD + " --order=po --reverse"))
        self.assertIn('xlabel="po reverse 0"', node(dot, "Base::run"))  # callers first
        self.assertIn('xlabel="po reverse 8"', node(dot, "leaf"))
        self.assertEqual(classes(dot, "leaf"), [])  # no start, nothing is `not reached`
        dot = self.assertDrawn(to_dot(WALK_BFS_REVERSE, self.CMD + " --order=bfs --reverse --from=leaf"))
        self.assertEqual(classes(dot, "leaf"), ["entry"])
        self.assertIn('xlabel="bfs reverse level 2"', node(dot, "main"))  # its transitive callers: everything, so nothing is dim
        self.assertEqual(classes(dot, "main"), [])

    def test_an_order_alone_has_no_edge(self):
        for text in (WALK_BFS, WALK_DFS, WALK_PO_REVERSE, WALK_BFS_REVERSE):
            first = text.splitlines()[0] + "\n"  # the `order` line, before the `edge` lines of --edges
            self.assertIsNone(to_dot(first, "build/bin/p09_walk manifests/p09_reach.cpp --order=bfs --from=main"), first)

    def test_the_witness_path(self):
        dot = self.assertDrawn(to_dot(PATH, self.SINK + " --path=main,fail"))
        self.assertEqual([(a, b) for a, b, _ in edges(dot)], [("main", "guarded"), ("guarded", "die"), ("die", "fail")])
        for a, b, _ in edges(dot):
            self.assertEqual(edge_classes(dot, a, b), ["hl"])
        self.assertEqual(classes(dot, "main"), ["entry"])
        self.assertEqual(classes(dot, "fail"), ["hl"])
        self.assertEqual(classes(dot, "guarded"), [])

    def test_all_paths_the_shortest_is_the_witness_and_the_rest_are_alternatives(self):
        dot = self.assertDrawn(to_dot(PATHS, self.SINK + " --path=main,fail --all-paths --max-paths=3"))
        self.assertEqual(edge_classes(dot, "die", "fail"), ["hl"])  # on the witness, and on the other path too: stays hl
        for a, b in (("main", "ping"), ("ping", "pong"), ("pong", "die")):
            self.assertEqual(edge_classes(dot, a, b), ["weak"])
        self.assertEqual(classes(dot, "ping"), ["dim"])  # only the alternative names it
        self.assertEqual(classes(dot, "die"), [])
        self.assertIn('label="paths: 2 shown, 2 found, limit 8"; labelloc=t;', dot)

    def test_a_path_over_the_whole_graph(self):
        dot = self.assertDrawn(to_dot(PATH_EDGES, self.SINK + " --path=main,fail --edges"), max_px=1200)
        self.assertEqual(edge_classes(dot, "guarded", "die"), ["hl"])
        self.assertEqual(edge_classes(dot, "always_dies", "die"), [])  # a call, but not the witness
        self.assertEqual(edge_label(dot, "main", "guarded"), "")  # the path line and the edge line are one edge, not `x2`
        self.assertEqual(edge_classes(dot, "fact", "fact"), ["back"])

    def test_a_cycle_closes_with_a_back_edge(self):
        dot = self.assertDrawn(to_dot(CYCLE, self.SINK + " --cycle=ring_a"))
        self.assertEqual([(a, b) for a, b, _ in edges(dot)], [("ring_a", "ring_b"), ("ring_b", "ring_c"), ("ring_c", "ring_a")])
        self.assertEqual(edge_classes(dot, "ring_c", "ring_a"), ["back"])
        self.assertEqual(edge_classes(dot, "ring_a", "ring_b"), [])
        for name in ("ring_a", "ring_b", "ring_c"):
            self.assertEqual(classes(dot, name), ["recursive"])

    def test_a_self_call_is_a_cycle_of_one_node(self):
        self.assertIsNone(to_dot("cycle fact -> fact\n", self.SINK + " --cycle=fact"))  # one node: no figure, as for any graph of one node
        dot = self.assertDrawn(to_dot("cycle fact -> fact\npath main -> fact (1 calls)\n", self.SINK + " --cycle=fact --path=main,fact"))
        self.assertEqual(edge_classes(dot, "fact", "fact"), ["back"])
        self.assertEqual(sorted(classes(dot, "fact")), ["hl", "recursive"])  # the end of the witness, and recursive

    def test_no_path_and_no_cycle_have_nothing_to_draw(self):
        self.assertIsNone(to_dot(PATH_NONE, self.SINK + " --path=fail,main"))
        self.assertIsNone(to_dot(CYCLE_NONE, self.SINK + " --cycle=safe"))


# --------------------------------------------------------------------------
# p10_callstrings, p10_farm
# --------------------------------------------------------------------------
class CallStrings(CgCase):
    CMD = "build/bin/p10_callstrings manifests/p10_callstrings.cpp --k=1"
    DIV = "divide[…,risky@L18]"

    def test_one_node_per_context_with_the_trace_as_edges(self):
        dot = self.assertDrawn(to_dot(CS_K1_TRACE, self.CMD + " --trace"))
        self.assertEqual(classes(dot, self.DIV), ["hl"])  # the warn target
        self.assertIn('label="divide\\n[…,risky@L18]\\na=top b=zero"', node(dot, self.DIV))  # the call string and the values, in the label
        self.assertIn("k=1", node(dot, self.DIV))
        self.assertIn("divides by b=zero at p10_callstrings.cpp:14", node(dot, self.DIV))
        self.assertEqual(classes(dot, "divide[…,safe@L16]"), [])
        self.assertIn(("risky[main@L29]", self.DIV), [(a, b) for a, b, _ in edges(dot)])  # `<-` is reversed: the caller's context points at the callee's
        self.assertEqual(edge_label(dot, "risky[main@L29]", self.DIV), "a=nonzero b=zero")  # what the call passes, not the site
        self.assertEqual(edge_label(dot, "risky[…,loop@L24]", self.DIV), "a=top b=zero")  # the other caller: where `top` comes from
        self.assertEqual(edge_label(dot, "main[]", "safe[main@L30]"), "")  # `-`: no parameter
        self.assertIn("@L18", re.search(r'"risky\[main@L29\]" -> "divide\[…,risky@L18\]" \[[^\]]*tooltip="([^"]*)"', dot)[1])  # the site stays in the tooltip

    def test_the_truncated_recursion_is_a_cycle_of_contexts(self):
        dot = self.assertDrawn(to_dot(CS_K1_TRACE, self.CMD + " --trace"))
        self.assertEqual(edge_classes(dot, "loop[…,loop@L25]", "loop[…,loop@L25]"), ["back"])  # the limit makes the string its own caller
        self.assertEqual(classes(dot, "loop[…,loop@L25]"), ["recursive"])
        self.assertEqual(classes(dot, "loop[main@L32]"), [])

    def test_the_summary_is_the_title(self):
        self.assertIn('label="k=1 contexts=11 functions=7 warnings=1"; labelloc=t;', to_dot(CS_K1_TRACE, self.CMD + " --trace"))

    def test_with_edges_and_no_trace_it_is_the_function_graph(self):
        dot = self.assertDrawn(to_dot(CS_K1_EDGES, self.CMD + " --edges"))
        self.assertEqual(classes(dot, "divide"), ["hl"])
        self.assertIn("k=1 […,risky@L18]: a=top b=zero", node(dot, "divide"))  # a function's contexts are its tooltip
        self.assertIn("k=1 […,safe@L16]: a=nonzero b=nonzero", node(dot, "divide"))
        self.assertEqual(edge_classes(dot, "loop", "loop"), ["back"])
        self.assertEqual(classes(dot, "loop"), ["recursive"])

    def test_a_possible_zero_warns_too(self):
        text = CS_K0 + "trace divide[] <- risky[] @L18: a=top b=zero\ntrace risky[] <- main[] @L29: x=nonzero\n"
        dot = self.assertDrawn(to_dot(text, self.CMD.replace("--k=1", "--k=0") + " --trace"))
        self.assertEqual(classes(dot, "divide[]"), ["hl"])  # `divides by b=top`: top may be zero
        self.assertIn("divides by b=top", node(dot, "divide[]"))

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot(CS_K0, self.CMD.replace("--k=1", "--k=0")))  # neither --trace nor --edges
        self.assertIsNone(to_dot("summary: k=0 contexts=7 functions=7 warnings=1\n", self.CMD + " --trace"))
        self.assertIsNone(to_dot(CS_K1_TRACE + "oops\n", self.CMD + " --trace"))


class Farm(CgCase):
    def test_the_cfg_of_the_farmed_body(self):
        dot = self.assertDrawn(to_dot(FARM_C, "build/bin/p10_farm manifests/p10_farm.c -- -fblocks"))
        self.assertTrue(dot.startswith("digraph farm_dispatch_once {"))
        self.assertIn('label="dispatch_once"; labelloc=t;', dot)
        self.assertEqual(classes(dot, "B3"), ["entry"])
        self.assertEqual(classes(dot, "B2"), ["cond"])
        self.assertEqual(classes(dot, "B0"), ["exit"])
        self.assertEqual(edge_label(dot, "B2", "B1"), "T")
        self.assertIn("block()", node(dot, "B1"))
        self.assertNotIn("if (*predicate != ~0L) {", dot)  # the pretty-printed body is cut out: only the CFG is drawn
        self.assertNotIn("farm clang_analyzer_eval", dot)  # `synthesized=no` sections have none

    def test_a_template_with_a_state_field(self):
        dot = self.assertDrawn(to_dot(FARM_CPP, "build/bin/p10_farm manifests/p10_farm.cpp"))
        self.assertTrue(dot.startswith("digraph farm_std_call_once"), dot[:60])
        self.assertIn("flag.__state_ = 1", node(dot, "B1"))
        self.assertIn("std::call_once", dot)

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("farm main: synthesized=no\nfarm helper: synthesized=no\n", "build/bin/p10_farm manifests/p10_farm.c"))
        self.assertIsNone(to_dot("farm f: synthesized=yes kind=other\nif (x) {\n    y();\n}\n", "build/bin/p10_farm a.c --cfg=false"))  # a body, no CFG
        dot = self.assertDrawn(to_dot("p10_farm.c:3:1: warning: a compiler remark\n" + FARM_C, "build/bin/p10_farm manifests/p10_farm.c 2>&1"))
        self.assertIn('label="dispatch_once"', dot)  # a line outside a `farm` section is not part of its CFG: left alone


# --------------------------------------------------------------------------
# p11_index, the p11_resolve --rta and p11_xtu --load additions, clang-tidy
# --------------------------------------------------------------------------
class Index(CgCase):
    def test_pointer_calls_references_and_calls(self):
        dot = self.assertDrawn(to_dot(INDEX_REFS, "build/bin/p11_index manifests/p11_fnptr.cpp --refs"))
        self.assertEqual(classes(dot, "f@param"), ["dim"])  # the variable the pointer call names, not a function
        self.assertEqual(edge_classes(dot, "apply", "f@param"), ["indirect"])
        self.assertEqual(edge_classes(dot, "table", "add_one"), ["weak"])  # an address taken
        self.assertEqual(edge_label(dot, "table", "add_one"), "ref @L13")
        self.assertEqual(edge_classes(dot, "main", "apply"), [])  # a plain call
        self.assertIn("roles=Call", dot)
        self.assertIn('label="p11_fnptr.cpp"', dot)

    def test_a_reference_to_itself_is_not_recursion(self):
        dot = to_dot(INDEX_REFS, "build/bin/p11_index manifests/p11_fnptr.cpp --refs")
        self.assertEqual(edge_classes(dot, "recurse_via_ptr", "recurse_via_ptr"), ["weak"])
        self.assertEqual(classes(dot, "recurse_via_ptr"), [])  # a reference is not a call: no cycle
        self.assertEqual(edge_classes(dot, "recurse_via_ptr", "apply"), [])

    def test_a_dynamic_call_is_a_virtual_edge(self):
        text = "== a.cpp: 1 call occurrences, 0 references\nedge dispatch -> Base::run @L26 roles=Call,Dyn\n"
        dot = self.assertDrawn(to_dot(text, "build/bin/p11_index a.cpp"))
        self.assertEqual(edge_classes(dot, "dispatch", "Base::run"), ["virtual"])
        self.assertEqual(edge_label(dot, "dispatch", "Base::run"), "virtual @L26")

    def test_the_diff_words(self):
        dot = self.assertDrawn(to_dot(INDEX_DIFF, "build/bin/p11_index manifests/p08_include.cpp --diff -- -std=c++17 -fblocks | grep -v ' both$'"), max_px=1100)
        self.assertEqual(edge_classes(dot, "Member::m", "helper"), ["hl"])  # only-index: charged to the field, no RelCall
        self.assertEqual(edge_classes(dot, "use_default", "leaf"), ["weak"])  # only-graph
        self.assertEqual(sorted(edge_classes(dot, "blk_var", "b@var")), ["hl", "indirect"])
        self.assertEqual(classes(dot, "b@var"), ["dim"])
        self.assertIn("roles=Call,NoRelCall", dot)

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("diff: only-index=13 only-graph=17 both=16\n", "build/bin/p11_index manifests/p08_include.cpp --diff"))
        self.assertIsNone(to_dot("== a.cpp: 0 call occurrences, 0 references\n", "build/bin/p11_index a.cpp"))
        self.assertIsNone(to_dot(INDEX_REFS + "edge a -> b @L1 roles=Call maybe\n", "build/bin/p11_index a.cpp"))


class TrackAdditions(CgCase):
    def test_rta_edges_are_cha_edges_and_the_instantiated_classes_are_the_title(self):
        dot = self.assertDrawn(to_dot(RESOLVE_RTA, "build/bin/p11_resolve manifests/p11_virtual.cpp --rta --counts"))
        self.assertEqual(edge_classes(dot, "dispatch", "Derived::run"), ["cha"])
        self.assertEqual(edge_label(dot, "dispatch", "Derived::run"), "rta @L26")
        self.assertIn("rta, 3 candidates", dot)
        self.assertIn('label="instantiated: Derived, Final, Square; edges 21 -> 28, indirect sites 5, resolved 4"; labelloc=t;', dot)
        self.assertIn("skip @L48: no-overrider", node(dot, "use_orphan"))
        self.assertNotIn("Unused", dot)  # a class nothing instantiates has no edge

    def test_not_instantiated_is_a_skip(self):
        text = "instantiated: Heap Local\nadd poll -> Heap::read @L19 reason=rta candidates=2\nadd poll -> Local::read @L19 reason=rta candidates=2\nskip read_gauge @L36 reason=not-instantiated\n"
        dot = self.assertDrawn(to_dot(text, "build/bin/p11_resolve manifests/p11_rta.cpp --rta"))
        self.assertIn("skip @L36: not-instantiated", node(dot, "read_gauge"))
        self.assertIn('label="instantiated: Heap, Local"', dot)
        self.assertIsNone(to_dot("instantiated: -\nskip f @L1 reason=not-instantiated\n", "build/bin/p11_resolve a.cpp --rta"))

    def test_load_says_where_it_merged_from(self):
        dot = self.assertDrawn(to_dot(XTU_LOAD, "build/bin/p11_xtu --load out/a.json out/b.json --edges"))
        self.assertIn('label="p11_xtu_a.cpp"; labeljust=l;\n    class="tu";', dot)
        self.assertEqual(edge_classes(dot, "a_fn", "b_fn"), ["xtu"])
        self.assertEqual(classes(dot, "shared"), ["hl"])


class Tidy(CgCase):
    CMD = "clang-tidy -checks='-*,misc-no-recursion' manifests/p09_recursion.cpp -- -std=c++17 2>/dev/null"

    def test_each_chain_is_a_cycle(self):
        dot = self.assertDrawn(to_dot(TIDY_RAW, self.CMD))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("fact", "fact"), ("is_odd", "is_even"), ("is_even", "is_odd"), ("pang", "ping"), ("ping", "pong"), ("pong", "pang")})
        for name in ("fact", "is_odd", "is_even", "pang", "ping", "pong"):
            self.assertEqual(classes(dot, name), ["recursive"], name)
        self.assertEqual(edge_classes(dot, "fact", "fact"), ["back"])  # the last frame calls the function the chain started at
        self.assertEqual(edge_classes(dot, "is_even", "is_odd"), ["back"])  # Frame #2
        self.assertEqual(edge_classes(dot, "is_odd", "is_even"), [])  # Frame #1
        self.assertEqual(edge_classes(dot, "pong", "pang"), ["back"])  # Frame #3
        self.assertEqual(edge_classes(dot, "pang", "ping"), [])
        self.assertEqual(edge_label(dot, "ping", "pong"), "@L14")  # the line of the call, from the note

    def test_the_filtered_output_draws_the_same_cycles(self):
        dot = self.assertDrawn(to_dot(TIDY_FILTERED, self.CMD + " | grep -E 'warning|Frame' | sed -E 's|^.*/manifests/|manifests/|'"))
        self.assertEqual(edge_classes(dot, "b", "a"), [])  # chain `b a`: Frame #1 b -> a, Frame #2 a -> b
        self.assertEqual(edge_classes(dot, "a", "b"), ["back"])
        self.assertEqual(edge_classes(dot, "pong", "pang"), ["back"])
        self.assertNotIn('"c"', dot)  # a warning with no chain of its own draws nothing
        self.assertEqual(len(edges(dot)), 8)

    def test_no_frame_no_figure(self):
        warnings = "".join(ln + "\n" for ln in TIDY_FILTERED.splitlines() if "warning" in ln)
        self.assertIsNone(to_dot(warnings, self.CMD + " | grep warning"))
        self.assertIsNone(to_dot("", self.CMD))

    def test_the_command_pattern(self):
        self.assertTrue(pattern("tidy").search(self.CMD))
        self.assertTrue(pattern("tidy").search("clang-tidy manifests/p09_recursion.cpp \\\n  -checks='-*,misc-no-recursion' -- -std=c++17"))
        self.assertIsNone(pattern("tidy").search("clang-tidy -checks='-*,readability-braces-around-statements' a.cpp"))


# --------------------------------------------------------------------------
# lists, not graphs
# --------------------------------------------------------------------------
class NotGraphs(CgCase):
    CTU = ("+ clang-extdef-mapping manifests/p11_ctu_b.cpp -- -std=c++17\n+ clang++ -emit-ast -o out/ctu/p11_ctu_b.cpp.ast manifests/p11_ctu_b.cpp\n"
           "CTU loaded AST file: p11_ctu_b.cpp.ast\nmanifests/p11_ctu_a.cpp:12:3: warning: TRUE [debug.ExprInspection]\n")
    GEN = "int f0(int);\nint f1(int);\nint f0(int x) { if (x <= 0) return 0; return f1(x - 1); }\n"

    def test_the_analyzer_and_script_outputs_have_no_figure(self):
        for text, cmd in (
            (LIST_ORDER, "CHECKER=debug.AnalysisOrder scripts/dumpcfg.sh manifests/p10_analyzer.cpp -Xclang -analyzer-config -Xclang debug.AnalysisOrder:PreCall=true"),
            (LIST_DUMPCALLS, "CHECKER=debug.DumpCalls scripts/dumpcfg.sh manifests/p10_analyzer.cpp | sed -E 's/0x[0-9a-f]+/0xADDR/g'"),
            (LIST_STATS, "CHECKER=debug.Stats scripts/dumpcfg.sh manifests/p10_analyzer.cpp"),
            (LIST_PROGRESS, "CHECKER=core scripts/dumpcfg.sh manifests/p09_reach.cpp -Xclang -analyzer-display-progress"),
            (LIST_EVAL, "CHECKER=debug.ExprInspection scripts/dumpcfg.sh manifests/p10_analyzer.cpp"),
            (LIST_EXTDEF, "clang-extdef-mapping manifests/p11_ctu_b.cpp -- -std=c++17"),
            (self.CTU, "scripts/ctu.sh manifests/p11_ctu_a.cpp manifests/p11_ctu_b.cpp 2>&1 | grep -E 'CTU|warning|note'"),
            (self.GEN, "python3 scripts/gen_calls.py --functions 3000 --fanout 3 --seed 1 > out/big.cpp; head -3 out/big.cpp"),
        ):
            self.assertIsNone(to_dot(text, cmd), cmd)

    def test_a_tool_name_is_a_command_not_a_file_name(self):
        for parser, tool in (("build", "p08_build"), ("anycall", "p08_anycall"), ("mine", "p08_mine"), ("metrics", "p09_metrics"), ("callstrings", "p10_callstrings"),
                             ("farm", "p10_farm"), ("index", "p11_index")):
            pat = pattern(parser)
            self.assertTrue(pat.search(f"build/bin/{tool} manifests/p08_include.cpp"), tool)
            self.assertTrue(pat.search(f"scripts/run.sh {tool} manifests/p08_include.cpp"), tool)
            self.assertIsNone(pat.search(f"cat manifests/{tool}.cpp tools/{tool}/main.cpp"), tool)
            self.assertIsNone(pat.search(f"build/bin/{tool}_extra a.cpp"), tool)
        self.assertIsNone(pattern("metrics").search("cat manifests/p09_metrics.h"))


# --------------------------------------------------------------------------
# the module as a whole
# --------------------------------------------------------------------------
class Module(unittest.TestCase):
    def test_registered_before_the_generic_cfg_parser(self):
        self.assertLess(outviz.MODULES.index("cg"), outviz.MODULES.index("cfg"))

    def test_the_site_and_the_module_give_dot_the_same_flags(self):
        self.assertEqual(cg.DOT_FLAGS, build_site.DOT_FLAGS)

    def test_wrap_breaks_at_scopes_then_punctuation_then_underscores(self):
        self.assertEqual(cg._wrap("main", 24), ["main"])
        self.assertEqual(cg._wrap("use()::(lambda@L8)::operator()", 18), ["use()::", "(lambda@L8)::", "operator()"])
        self.assertEqual(cg._wrap("ns::Foo<int, double>::bar(int)", 18), ["ns::", "Foo<int, double>::", "bar(int)"])
        self.assertEqual(cg._wrap("uses_inline_helper", 11), ["uses_", "inline_", "helper"])
        for width in (24, 18, 14, 11):
            for name in ("operator new[]", "two_lambdas()::(lambda@L59)::operator()", "a_name_without_any_break_point_at_all"):
                self.assertTrue(all(len(x) <= width for x in cg._wrap(name, width)), (width, name))
                self.assertEqual("".join(cg._wrap(name, width)), name)

    def test_a_wide_graph_gets_a_narrower_layout(self):
        # a root with a dozen leaves of long names does not fit one rank: it is laid out left to right
        leaves = [f"a_rather_long_function_name_{i}" for i in range(12)]
        text = "== w.cpp: 13 nodes, 12 edges\nnode <root>\n" + "".join(f"node {n}\n" for n in leaves) + "".join(f"edge <root> -> {n}\n" for n in leaves)
        dot = to_dot(text, "build/bin/p08_nodes w.cpp --edges --with-root")
        self.assertIn("rankdir=LR", dot)
        rc, err, px = render(dot)
        self.assertEqual((rc, err), (0, ""))
        self.assertLessEqual(px, 900)

    def test_figures_are_deterministic(self):
        self.assertEqual(to_dot(NODES, Nodes.CMD), to_dot(NODES, Nodes.CMD))
        self.assertEqual(to_dot(INCLUDE_DUMP, BASIC_CMD), to_dot(INCLUDE_DUMP, BASIC_CMD))
        for text, cmd in ((BUILD_INCREMENTAL, Build.INCR), (CS_K1_TRACE, CallStrings.CMD + " --trace"), (ANYCALL_MAIN, AnyCall.CMD), (FARM_C, "build/bin/p10_farm a.c")):
            self.assertEqual(to_dot(text, cmd), to_dot(text, cmd), cmd)

    def test_every_tool_of_the_track_has_a_parser(self):
        names = {fn.__name__ for _, fn in cg.PARSERS}
        self.assertLessEqual({"dump", "nodes", "build", "anycall", "mine", "metrics", "summary", "sites", "callstrings", "farm", "resolve", "index", "xtu", "check", "tidy"}, names)

    def test_key_value_words(self):
        self.assertEqual(cg._kvs('kind=Allocator decl="operator new" params=1 ret=void *'), {"kind": "Allocator", "decl": "operator new", "params": "1", "ret": "void *"})
        self.assertEqual(cg._kvs("kind=Function decl=twice params=1 ret=int ident=twice")["ident"], "twice")  # `ident=` ends the type
        self.assertEqual(cg._kvs("in=0 out=1 sites=1 height=inf scc=6 dead root"), {"in": "0", "out": "1", "sites": "1", "height": "inf", "scc": "6"})

    def test_levels_after_a_quoted_name(self):
        self.assertEqual(cg._levels('main@0 helper@1 "operator new"@2 "a b@3" local@x.cpp'), [("main", 0), ("helper", 1), ("operator new", 2), ("a b", 3), ("local@x.cpp", None)])
        self.assertEqual(cg._levels("-"), [])

    def test_a_long_title_is_set_in_lines_a_short_one_is_not(self):
        self.assertEqual(cg._title("edges 5 -> 9"), '"edges 5 -> 9"')
        long = " ".join(f"word{i}" for i in range(40))
        lines = cg._title(long).strip('"').split("\\l")
        self.assertGreater(len(lines), 1)
        self.assertTrue(all(len(x) <= cg.TITLE_WRAP for x in lines))
        self.assertEqual(" ".join(x for x in lines if x), long)

    def test_invisible_edges_and_ordering_out_never_meet(self):
        for text, cmd in ((BUILD_INCREMENTAL, Build.INCR), (SITES, Sites.CMD)):
            dot = to_dot(text, cmd)
            self.assertIn("style=invis", dot)
            self.assertNotIn("ordering=out", dot)  # dot 15 asserts on invisible edges together with it


if __name__ == "__main__":
    unittest.main()
