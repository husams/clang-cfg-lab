"""Tests for outviz.p08: `python3 -m unittest discover -s scripts/outviz` (or run this file).

The samples are real outputs of this lab's Part 8 tools (build/bin/p08_*) and of debug.DumpCallGraph, trimmed.
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
from outviz import p08  # noqa: E402

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

# p08_walk manifests/p08_recursion.cpp --sccs --order=rpo --edges
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

XTU = """[1/2] Processing file manifests/p08_xtu_a.cpp.
[2/2] Processing file manifests/p08_xtu_b.cpp.
== merged: 9 nodes, 9 edges, 2 tus
node a_fn kind=def tus=p08_xtu_a.cpp
node b_fn kind=def tus=p08_xtu_b.cpp
node cleanup kind=def tus=p08_xtu_b.cpp
node die kind=def tus=p08_xtu_b.cpp
node fail kind=decl noreturn tus=p08_xtu_b.cpp
node local@p08_xtu_a.cpp kind=def static tus=p08_xtu_a.cpp
node local@p08_xtu_b.cpp kind=def static tus=p08_xtu_b.cpp
node main kind=def tus=p08_xtu_a.cpp
node shared kind=def tus=p08_xtu_a.cpp,p08_xtu_b.cpp
edge a_fn -> b_fn @p08_xtu_a.cpp:5 xtu
edge a_fn -> local@p08_xtu_a.cpp @p08_xtu_a.cpp:5
edge b_fn -> a_fn @p08_xtu_b.cpp:7 xtu
edge b_fn -> local@p08_xtu_b.cpp @p08_xtu_b.cpp:7
edge cleanup -> die @p08_xtu_b.cpp:9
edge die -> fail @p08_xtu_b.cpp:8
edge local@p08_xtu_a.cpp -> shared @p08_xtu_a.cpp:4
edge local@p08_xtu_b.cpp -> shared @p08_xtu_b.cpp:4
edge main -> a_fn @p08_xtu_a.cpp:6
"""

XTU_SINGLE = """== merged: 5 nodes, 4 edges, 1 tus
node a_fn kind=def tus=p08_xtu_a.cpp
node b_fn kind=decl tus=p08_xtu_a.cpp
node local kind=def static tus=p08_xtu_a.cpp
node main kind=def tus=p08_xtu_a.cpp
node shared kind=def tus=p08_xtu_a.cpp
edge a_fn -> b_fn @p08_xtu_a.cpp:5
edge a_fn -> local @p08_xtu_a.cpp:5
edge local -> shared @p08_xtu_a.cpp:4
edge main -> a_fn @p08_xtu_a.cpp:6
unresolved b_fn (decl in p08_xtu.h)
"""

CHECK_PLAIN = """diag manifests/p08_xtu_a.cpp:5: recursion: a_fn -> b_fn -> a_fn
diag manifests/p08_xtu_b.cpp:8: reaches-sink: die -> fail
diag manifests/p08_xtu_b.cpp:9: reaches-sink: cleanup -> die -> fail
summary: 8 functions, 2 recursive, 2 reach a sink
"""

CHECK_TRACE = """[1/2] Processing file manifests/p08_xtu_a.cpp.
[2/2] Processing file manifests/p08_xtu_b.cpp.
diag manifests/p08_xtu_a.cpp:5: recursion: a_fn -> b_fn -> a_fn
trace recursion: a_fn -> b_fn @manifests/p08_xtu_a.cpp:5
trace recursion: b_fn -> a_fn @manifests/p08_xtu_b.cpp:7
diag manifests/p08_xtu_b.cpp:8: reaches-sink: die -> fail
trace reaches-sink: die -> fail @manifests/p08_xtu_b.cpp:8
diag manifests/p08_xtu_b.cpp:9: reaches-sink: cleanup -> die -> fail
trace reaches-sink: cleanup -> die @manifests/p08_xtu_b.cpp:9
trace reaches-sink: die -> fail @manifests/p08_xtu_b.cpp:8
summary: 8 functions, 2 recursive, 2 reach a sink
"""

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


def render(dot: str) -> tuple[int, str, float]:
    """(dot's exit status, its warnings, natural width in px) with the flags the site build uses."""
    p = subprocess.run(["dot", "-Tsvg", *build_site.DOT_FLAGS], input=dot, capture_output=True, text=True)
    vb = re.search(r'viewBox="[\d.\-]+ [\d.\-]+ ([\d.\-]+) ', p.stdout)
    return p.returncode, p.stderr.strip(), float(vb[1]) * 96 / 72 if vb else 0.0


class P08Case(unittest.TestCase):
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
class Dump(P08Case):
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

    def test_p08_nodes_dump_is_the_same_text(self):
        self.assertEqual(to_dot(BASIC_DUMP, "build/bin/p08_nodes manifests/p08_basic.cpp --dump"), to_dot(BASIC_DUMP, BASIC_CMD))

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot(" --- Call graph Dump ---\n  Function: < root > calls:\n", BASIC_CMD))  # an empty TU
        self.assertIsNone(to_dot("Writing '/tmp/CallGraph-abc.dot'...  done.\n", "clang --analyze -Xclang -analyzer-checker=debug.DumpCallGraph"))
        self.assertIsNone(to_dot(BASIC_DUMP + "t.cpp:3:1: warning: something\n", BASIC_CMD))  # a line that is not part of the dump


# --------------------------------------------------------------------------
# p08_nodes
# --------------------------------------------------------------------------
class Nodes(P08Case):
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

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot(NODES_NO_EDGES, "build/bin/p08_nodes manifests/p08_basic.cpp"))  # no edge
        self.assertIsNone(to_dot(NODES + "a.cpp:1:1: warning: unused\n", self.CMD))  # an unknown line
        self.assertIsNone(to_dot('{"edges": [], "nodes": []}\n', "build/bin/p08_nodes a.cpp --emit=json"))
        self.assertIsNone(to_dot(NODES_PLAIN, "build/bin/p08_xtu_other manifests/p08_basic.cpp"))  # not a p08 tool

    def test_a_tool_name_inside_a_file_name_is_not_the_tool(self):
        self.assertIsNone(p08.PARSERS[1][0].search("cat manifests/p08_nodes.h"))
        self.assertIsNone(p08.PARSERS[5][0].search("cat manifests/p08_xtu.h manifests/p08_xtu_a.cpp"))
        self.assertTrue(p08.PARSERS[5][0].search("scripts/run.sh p08_xtu manifests/p08_xtu_a.cpp"))


class EmittedDot(P08Case):
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
        # cfg.py draws any `digraph` it finds as a CFG; p08 claims the output of its tools first
        self.assertEqual(to_dot(EMIT_DOT, "build/bin/p08_nodes manifests/p08_basic.cpp --emit=dot"), EMIT_DOT)
        self.assertIn('class="back"', to_dot(EMIT_DOT, "build/bin/p08_nodes manifests/p08_basic.cpp --emit=dot"))


# --------------------------------------------------------------------------
# p08_walk
# --------------------------------------------------------------------------
class Walk(P08Case):
    CMD = "build/bin/p08_walk manifests/p08_reach.cpp --callers=leaf"

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
        for text, cmd in ((WALK_SCCS, "build/bin/p08_walk manifests/p08_recursion.cpp --sccs --cyclic"),
                          (WALK_REACH, "build/bin/p08_walk manifests/p08_reach.cpp --from=main --dead"),
                          (WALK_ORDER, "build/bin/p08_walk manifests/p08_reach.cpp --from=main --order=rpo")):
            self.assertIsNone(to_dot(text, cmd), cmd)

    def test_scc_clusters_and_orders_over_the_edges(self):
        dot = self.assertDrawn(to_dot(RECURSION, "build/bin/p08_walk manifests/p08_recursion.cpp --sccs --order=rpo --edges"))
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
        dot = self.assertDrawn(to_dot(REACH_EDGES, "build/bin/p08_walk manifests/p08_reach.cpp --from=main --dead --edges --sites"))
        self.assertEqual(classes(dot, "main"), ["entry"])
        for name in ("Base::run", "callback_target", "helper_of_unused", "static_unused", "unused"):
            self.assertEqual(classes(dot, name), ["dim"], name)
        self.assertEqual(classes(dot, "leaf"), [])
        self.assertEqual(edge_label(dot, "helper", "leaf"), "@L5")

    def test_an_empty_list_is_a_dash(self):
        self.assertIsNone(to_dot("callers main: -\n", "build/bin/p08_walk manifests/p08_reach.cpp --callers=main"))
        dot = to_dot("dead: -\nedge a -> b\nreach a: a b\n", "build/bin/p08_walk x.cpp --from=a --dead --edges")
        self.assertNotIn('"-"', dot)

    def test_reach_marks_the_start_and_dims_what_it_did_not_reach(self):
        text = "edge main -> helper\nedge helper -> leaf\nedge unused -> leaf\nreach main: helper leaf main\n"
        dot = self.assertDrawn(to_dot(text, "build/bin/p08_walk manifests/p08_reach.cpp --from=main --edges"))
        self.assertEqual(classes(dot, "main"), ["entry"])
        self.assertEqual(classes(dot, "unused"), ["dim"])
        self.assertEqual(classes(dot, "helper"), [])


# --------------------------------------------------------------------------
# p08_summary
# --------------------------------------------------------------------------
class Summary(P08Case):
    def test_sink_witness_chains_clusters_and_trace(self):
        dot = self.assertDrawn(to_dot(SUMMARY_SINK, "build/bin/p08_summary manifests/p08_sink.cpp --prop=sink --trace"))
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
        dot = self.assertDrawn(to_dot(SUMMARY_DEPTH, "build/bin/p08_summary manifests/p08_sink.cpp --prop=depth"))
        self.assertIn("depth=1", node(dot, "level3"))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("die", "fail"), ("level3", "level4"), ("main", "die")})
        self.assertIn("recursive", classes(dot, "retry"))
        self.assertIn("longest chain", dot)

    def test_the_whole_graph_with_the_witness_chain_emphasised(self):
        dot = self.assertDrawn(to_dot(SUMMARY_SINK_EDGES, "build/bin/p08_summary manifests/p08_sink.cpp --prop=sink --edges"))
        self.assertEqual(edge_classes(dot, "die", "fail"), ["hl"])  # `die: sink=always via fail`
        self.assertEqual(edge_classes(dot, "always_dies", "fail"), [])  # a call, but not the witness
        self.assertEqual(edge_label(dot, "die", "fail"), "")  # not "x2": the line and the via are one edge
        self.assertEqual(edge_classes(dot, "ring_c", "ring_a"), ["back"])  # the cycle edge only the full graph has
        self.assertEqual(edge_classes(dot, "fact", "fact"), ["back"])
        self.assertIn('"top"', dot)  # it has an edge now

    def test_a_sink_named_on_the_command_line_is_the_sink(self):
        text = "scc 0: helper iter=1\nsummary helper: sink=always\nscc 1: top iter=1\nsummary top: sink=may via helper\n"
        dot = to_dot(text, "build/bin/p08_summary manifests/p08_sink.cpp --prop=sink --sink=helper")
        self.assertIn("sink", classes(dot, "helper"))

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("scc 0: safe iter=1\nsummary safe: sink=none\nscc 1: level4 iter=1\nsummary level4: depth=0\n", "build/bin/p08_summary a.cpp"))
        self.assertIsNone(to_dot(SUMMARY_DEPTH + "summary x: depth=banana\n", "build/bin/p08_summary a.cpp"))


# --------------------------------------------------------------------------
# p08_sites
# --------------------------------------------------------------------------
class Sites(P08Case):
    CMD = "build/bin/p08_sites manifests/p08_sites.cpp --unresolved --preset=analyzer"

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
        dot = self.assertDrawn(to_dot(SITES_SUCCS, "build/bin/p08_sites manifests/p08_sites.cpp --func=branches --succs --preset=analyzer"))
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
        dot = self.assertDrawn(to_dot(text, "build/bin/p08_sites manifests/p08_sites.cpp --func=dispatch --succs"))
        self.assertEqual(edge_classes(dot, "dispatch:B1", "Base::run"), ["virtual"])
        self.assertEqual(edge_label(dot, "dispatch:B1", "Base::run"), "B1.4 virtual")

    def test_clusters_are_stacked_without_ordering_out(self):
        dot = to_dot(SITES, self.CMD)
        self.assertIn("style=invis", dot)
        self.assertNotIn("ordering=out", dot)  # dot 15 asserts on both together

    def test_functions_without_a_site_are_no_figure(self):
        self.assertIsNone(to_dot(SITES_EMPTY, "build/bin/p08_sites manifests/p08_sites.cpp"))

    def test_walk_lines_are_the_descents(self):
        dot = self.assertDrawn(to_dot(SITES_WALK, "build/bin/p08_sites manifests/p08_sites.cpp --walk=main --depth=2 --preset=analyzer"))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("main", "Holder::Holder"), ("Holder::Holder", "leaf"), ("main", "branches"), ("branches", "leaf")})
        self.assertEqual(edge_label(dot, "branches", "leaf"), "B6, B3")  # the blocks that call it
        self.assertEqual(edge_classes(dot, "main", "branches"), ["call"])
        self.assertEqual(classes(dot, "main"), ["entry"])

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("walk 0 main:B2\nwalk 0 main:B1\n", "build/bin/p08_sites manifests/p08_sites.cpp --walk=main"))  # no descent
        self.assertIsNone(to_dot(SITES + SITES_WALK, self.CMD))  # sites and a walk in one block
        self.assertIsNone(to_dot(SITES + "site main B1.9 Function f surprising\n", self.CMD))  # a status this module does not know


# --------------------------------------------------------------------------
# p08_resolve
# --------------------------------------------------------------------------
class Resolve(P08Case):
    def test_added_edges_by_reason(self):
        dot = self.assertDrawn(to_dot(RESOLVE_VIRTUAL, "build/bin/p08_resolve manifests/p08_virtual.cpp --all"))
        self.assertEqual(edge_classes(dot, "dispatch", "Derived::run"), ["cha"])
        self.assertEqual(edge_label(dot, "dispatch", "Derived::run"), "cha @L26")
        self.assertIn("4 candidates", dot)
        self.assertEqual(edge_classes(dot, "call_final", "Final::run"), ["hl"])
        self.assertEqual(edge_label(dot, "call_final", "Final::run"), "devirt final @L28")
        self.assertEqual(edge_label(dot, "call_local", "Derived::run"), "devirt @L32")
        self.assertIn("skip @L48: no-overrider", node(dot, "use_orphan"))  # a skip is a tooltip on its caller

    def test_fnptr_edges_and_the_cycle_they_close(self):
        dot = self.assertDrawn(to_dot(RESOLVE_FNPTR, "build/bin/p08_resolve manifests/p08_fnptr.cpp --fnptr --sccs"))
        self.assertEqual(edge_classes(dot, "apply", "add_one"), ["indirect"])
        self.assertEqual(edge_label(dot, "apply", "add_one"), "fnptr @L11")
        self.assertIn('label="scc 4 cyclic mutual"', dot)
        self.assertEqual(classes(dot, "recurse_via_ptr"), ["recursive"])
        self.assertIn("skip @L18: unknown-type", node(dot, "fire"))

    def test_stats_are_the_title(self):
        text = RESOLVE_FNPTR + "stats: edges 5 -> 9, indirect sites 4, resolved 3\n"
        dot = to_dot(text, "build/bin/p08_resolve manifests/p08_fnptr.cpp --fnptr --counts --sccs")
        self.assertIn('label="edges 5 -> 9, indirect sites 4, resolved 3"; labelloc=t;', dot)

    def test_base_edges_come_first(self):
        text = "edge dispatch -> Base::run @L26 CXXMemberCallExpr kind=call\n" + RESOLVE_VIRTUAL
        dot = self.assertDrawn(to_dot(text, "build/bin/p08_resolve manifests/p08_virtual.cpp --all"))
        self.assertEqual(edge_classes(dot, "dispatch", "Base::run"), [])

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("stats: edges 5 -> 5, indirect sites 4, resolved 0\n", "build/bin/p08_resolve manifests/p08_fnptr.cpp --counts"))
        self.assertIsNone(to_dot("skip fire @L18 reason=unknown-type\n", "build/bin/p08_resolve manifests/p08_fnptr.cpp --fnptr"))
        self.assertIsNone(to_dot("scc 4 cyclic mutual: apply recurse_via_ptr\n", "build/bin/p08_resolve manifests/p08_fnptr.cpp --sccs"))
        self.assertIsNone(to_dot(RESOLVE_VIRTUAL + "add a -> b @L1 reason=new-rule\n", "build/bin/p08_resolve x.cpp"))


# --------------------------------------------------------------------------
# p08_xtu, p08_check
# --------------------------------------------------------------------------
class Xtu(P08Case):
    CMD = "build/bin/p08_xtu manifests/p08_xtu_a.cpp manifests/p08_xtu_b.cpp --edges"

    def test_one_cluster_per_translation_unit_and_the_shared_function_outside(self):
        dot = self.assertDrawn(to_dot(XTU, self.CMD))
        self.assertIn('label="p08_xtu_a.cpp"; labeljust=l;\n    class="tu";', dot)
        self.assertIn('label="p08_xtu_b.cpp"; labeljust=l;\n    class="tu";', dot)
        a_cluster = dot.split("subgraph cluster_tu1 {")[1].split("  }")[0]
        b_cluster = dot.split("subgraph cluster_tu2 {")[1].split("  }")[0]
        self.assertIn('"main"', a_cluster)
        self.assertIn('"local@p08_xtu_a.cpp"', a_cluster)
        self.assertIn('"cleanup"', b_cluster)
        self.assertNotIn('"shared"', a_cluster + b_cluster)  # defined in both: it belongs to neither
        self.assertEqual(classes(dot, "shared"), ["hl"])

    def test_cross_file_edges_and_the_cycle(self):
        dot = self.assertDrawn(to_dot(XTU, self.CMD))
        self.assertEqual(edge_classes(dot, "a_fn", "b_fn"), ["xtu"])
        self.assertEqual(sorted(edge_classes(dot, "b_fn", "a_fn")), ["back", "xtu"])  # closes the cycle a_fn -> b_fn -> a_fn
        self.assertEqual(edge_classes(dot, "a_fn", "local@p08_xtu_a.cpp"), [])
        self.assertEqual(classes(dot, "a_fn"), ["recursive"])
        self.assertIn("p08_xtu_b.cpp:7", dot)  # the call site, in the tooltip
        self.assertEqual(classes(dot, "fail"), ["dim", "sink", "external"])  # declaration only, [[noreturn]]
        self.assertEqual({n for n in re.findall(r'"(local@[^"]+)" \[label', dot)}, {"local@p08_xtu_a.cpp", "local@p08_xtu_b.cpp"})  # two statics, two nodes

    def test_a_declaration_with_no_definition_is_external(self):
        dot = self.assertDrawn(to_dot(XTU_SINGLE, "build/bin/p08_xtu manifests/p08_xtu_a.cpp --edges --unresolved 2>/dev/null"))
        self.assertEqual(classes(dot, "b_fn"), ["dim", "external"])
        self.assertIn("decl in p08_xtu.h", node(dot, "b_fn"))
        self.assertEqual(edge_classes(dot, "a_fn", "b_fn"), [])  # no xtu edge: nothing defines it

    def test_nothing_to_draw(self):
        self.assertIsNone(to_dot("== merged: 1 nodes, 0 edges, 1 tus\nnode a_fn kind=def tus=a.cpp\n", self.CMD))
        self.assertIsNone(to_dot(XTU + "something else\n", self.CMD))


class Check(P08Case):
    CMD = "build/bin/p08_check manifests/p08_xtu_a.cpp manifests/p08_xtu_b.cpp"

    def test_plain_diag_output_is_a_list_of_diagnostics(self):
        self.assertIsNone(to_dot(CHECK_PLAIN, self.CMD))

    def test_trace_draws_the_cycle_and_the_sink_chain(self):
        dot = self.assertDrawn(to_dot(CHECK_TRACE, self.CMD + " --trace"))
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("a_fn", "b_fn"), ("b_fn", "a_fn"), ("die", "fail"), ("cleanup", "die")})
        self.assertEqual(edge_classes(dot, "b_fn", "a_fn"), ["back"])
        self.assertEqual(classes(dot, "a_fn"), ["recursive"])
        self.assertEqual(classes(dot, "fail"), ["sink"])  # the end of the chain, not `die`
        self.assertEqual(classes(dot, "die"), [])
        self.assertIn("p08_xtu_b.cpp:7", dot)

    def test_a_trace_without_trace_lines_still_has_its_diag_paths(self):
        dot = to_dot("\n".join(ln for ln in CHECK_TRACE.splitlines() if not ln.startswith("trace")) + "\n", self.CMD + " --trace")
        self.assertEqual(edge_classes(dot, "b_fn", "a_fn"), ["back"])
        self.assertEqual(classes(dot, "fail"), ["sink"])

    def test_a_clean_run_has_nothing_to_draw(self):
        self.assertIsNone(to_dot("summary: 4 functions, 0 recursive, 0 reach a sink\n", self.CMD + " --trace"))


# --------------------------------------------------------------------------
# the module as a whole
# --------------------------------------------------------------------------
class Module(unittest.TestCase):
    def test_registered_before_the_generic_cfg_parser(self):
        self.assertLess(outviz.MODULES.index("p08"), outviz.MODULES.index("cfg"))

    def test_the_site_and_the_module_give_dot_the_same_flags(self):
        self.assertEqual(p08.DOT_FLAGS, build_site.DOT_FLAGS)

    def test_wrap_breaks_at_scopes_then_punctuation_then_underscores(self):
        self.assertEqual(p08._wrap("main", 24), ["main"])
        self.assertEqual(p08._wrap("use()::(lambda@L8)::operator()", 18), ["use()::", "(lambda@L8)::", "operator()"])
        self.assertEqual(p08._wrap("ns::Foo<int, double>::bar(int)", 18), ["ns::", "Foo<int, double>::", "bar(int)"])
        self.assertEqual(p08._wrap("uses_inline_helper", 11), ["uses_", "inline_", "helper"])
        for width in (24, 18, 14, 11):
            for name in ("operator new[]", "two_lambdas()::(lambda@L59)::operator()", "a_name_without_any_break_point_at_all"):
                self.assertTrue(all(len(x) <= width for x in p08._wrap(name, width)), (width, name))
                self.assertEqual("".join(p08._wrap(name, width)), name)

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


if __name__ == "__main__":
    unittest.main()
