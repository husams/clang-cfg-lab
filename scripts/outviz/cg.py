"""Graphs from call-graph text: the `debug.DumpCallGraph` dump, the call-graph tools of Parts 8-11 (p08_* ... p11_*) and clang-tidy.

  debug.DumpCallGraph  `Function: X calls: Y Z` lines (also `p08_nodes --dump`): `< root >` (class root) with weak
                       edges to everything it lists, repeated callees one edge labelled `x2`, cycle-closing edges
                       back, functions on a cycle recursive. Names that print the same (template instantiations,
                       lambdas, blocks `< >`) are one node here: it says "N nodes" in its label.
  p08_nodes, p09_walk  `node` / `edge` lines: decl nodes dim, [[noreturn]] nodes sink, `--sites` lines as edge labels,
                       kind / static in the node label, usr / expression class in tooltips; with `p09_walk --edges` the
                       `scc` lines (cyclic SCCs become scc clusters), `order po|rpo|dfs|bfs` (xlabel `rpo i`, `dfs i`,
                       `bfs level l`; a dfs / bfs start is an entry), `reach` (the start is an entry), `dead` (dim);
                       `callers` lines draw by themselves: the reverse star around the name; `path` / `cycle` lines draw
                       by themselves too: the first `path` is the witness (edges hl, start entry, end hl), further
                       `--all-paths` paths are alternatives (dim nodes, weak edges), a `cycle` is a chain with its
                       closing edge back, and `paths:` is the title
  p08_build            the `node` / `edge` graph (a `--doors` node entered by the callee door is external), the `flags` line
                       and the node / edge counts of `diff:` as the title; `--incremental` without edges: a step node
                       `after <fn>` (api) per line, a weak edge to each name in its `new:`, the steps stacked in order
  p08_anycall          a star per function: `fn -> callee` labelled with the AnyCall kind and the line (the expression
                       class and the signature in the tooltip); `decl=?` is a dim `?` node reached by an indirect edge;
                       Destructor / Deallocator edges weak; `kinds:` is the title; `decl` lines add to tooltips
  p08_mine             `edge` lines by their `both` / `only-lib` / `only-mine` word (plain / weak / hl), a `"?(type)"` callee
                       a dim node reached by an indirect edge
  p09_metrics          the nodes of the `metric` lines labelled `in / out / sites` (recursive, dead = dim, root = entry in
                       the classes, height and scc in the tooltip) with the `edge` lines of `--edges`
  p10_summary          `summary` values in the node labels (sink=none dim, always hl, the sink itself sink; depth=inf
                       recursive), cyclic `scc` clusters with their iteration count, `trace` lines in tooltips; the edges
                       are the `via <callee>` of the summaries, or with `--edges` the whole graph with the witness chain hl
  p10_callstrings      one node per `fn[string]` (the call string in the label, its `ctx` values too) with the edges of the
                       `trace` lines (caller -> callee, reversed from the `<-`), the `warn` targets hl; with `--edges`
                       and no `trace` the function-level graph, a function's `ctx` rows in its tooltip, `warn` targets hl
  p10_farm             each `farm <name>: synthesized=yes` section: its CFG, drawn by cfg.py (the header and the
                       pretty-printed body are cut out first)
  p10_sites            one cluster per function; with `--succs` its CFG blocks (entry, exit, T / F, back edges) holding
                       their call sites, else a node per call-site element `B3.2`; an edge to each callee by status
                       (resolved call, virtual, missing:indirect to a `?` node, other missing:* dashed); `walk` lines
                       become the functions the walk descended into, an edge per descent labelled with its block
  p11_resolve          the `add` edges (fnptr-sig indirect, cha / rta cha, devirt-* hl) with their reason as label, `skip`
                       in tooltips, `instantiated:` and `stats:` as the title, `scc` lines as in p09_walk
  p11_index            `edge` lines (a `Dyn` role is a virtual edge, a `<name>@param` / `<name>@var` callee a dim node
                       reached by an indirect edge), `ref` lines weak, with `--diff` the `both` / `only-index` /
                       `only-graph` word as in p08_mine
  p11_xtu              one tu cluster per translation unit (`tus=` with one file; functions defined in several stand
                       outside, highlighted), decl-only nodes external, xtu edges (call sites in the tooltips)
  p11_check            only with `--trace`: the `trace` edges of the recursion cycles and sink chains as one graph
  clang-tidy           `misc-no-recursion`: the `Frame #n: function 'a' calls function 'b'` notes of each warning are one
                       cycle (recursive nodes, the last frame is the edge back to the start, the line of each call site);
                       a chain a pipeline printed, `tidy:  a -> b -> a` (and `lab:  ...` next to it), is drawn the same way,
                       each edge labelled with the tags of the lines that have it

A tool's own `--emit=dot` output is returned as it is (it already follows the diagram contract); one cut by head / sed
gets its missing closing braces. The text of `scc` / `order` / `reach` / `dead` alone (no `--edges`), `summary` lines with no `via`
(a summary with no edge of its own is left out of the figure: the Output tab lists it), `--counts`, `--emit=json`,
plain `diag` output of p11_check, and any unknown line in a tool format (a clang warning merged into the output, the
`lookup` lines of p08_nodes, a bash block that mixes tools in a way the shared grammar does not know) give None, like an
output with no edge. A tool's own error line (`p09_walk: --path needs ...`) and an `exit=N` echo are not graph content and are skipped.
An empty list is printed `-` by the tools and is the empty list here. A listing cut by
head / sed / grep is drawn as far as its lines go: every line is a fact of its own.

The None cases of the track tools: p08_build with only `flags` / `diff:` lines, or an `--incremental` table of more than
MAX_STEPS rows; p08_anycall with only `decl` / `kinds:` lines (`--decls`, `--kinds`); p08_mine and p11_index with only the
`diff:` line; p09_walk with `path A -> B: none`, `cycle F: none` or an order list without `--edges`; p09_metrics with
`totals:` alone or `--top` without `--edges`; p10_callstrings with `summary:` alone, or `ctx` / `warn` lines but neither
`--trace` nor `--edges`; p10_farm with `synthesized=no` only (no body, no CFG); clang-tidy output with no `Frame` line (a
warning for a function whose chain was printed under another warning has none). Like any graph, a figure needs two nodes
and an edge: a lone self call (`cycle fact -> fact`, one `Frame`) is nothing to draw. Lists, not graphs, so nothing at all
for `debug.AnalysisOrder`, `debug.DumpCalls`, `debug.Stats`, `-analyzer-display-progress`, `clang_analyzer_eval`
warnings, `clang-extdef-mapping`, `scripts/ctu.sh` and `scripts/gen_calls.py`: no pattern here matches their commands.

Two choices the tools' own words do not make for us. Where a tool says "dim" for an edge (an edge the library has and a
hand-written graph lacks, the alternatives of a witness) this module writes `weak`: `dim` is a node class, and a dashed
muted edge is the vocabulary's quiet edge. An address taken (`ref`) or a transitive caller is a relation, not a call: it
never makes a cycle. With both `trace` and `edge` lines p10_callstrings draws the contexts of the `trace` lines (the
function-level `edge` lines are a different node namespace); p10_farm, whose sections each hold a complete CFG, leaves a
line outside a section (a compiler remark merged into the output) alone instead of giving up.

Width: a figure is laid out top to bottom with names wrapped at `::`; when dot's own drawing of it is wider than the page
(900 px, measured by running dot with the build's flags) the wrap is tightened, and a graph that is still too wide goes
left to right, where a wide rank stacks vertically. Clusters of separate sections (functions, translation units) are
stacked with invisible edges, top to bottom only (`ordering=out` is then left out: dot 15 asserts on invisible edges
between clusters together with it; the same holds for any figure with an invisible edge).
"""

from __future__ import annotations

import re
import subprocess
import textwrap
from typing import Callable

from . import cfg as _cfg

LAYOUTS = [("TB", 24), ("TB", 18), ("LR", 24), ("LR", 18), ("TB", 14), ("TB", 11), ("LR", 14), ("LR", 11)]  # (rankdir, name wrap width) tried in turn
BUDGET_PX = 900  # a drawing wider than this (the page holds 896 px) is laid out again with the next entry of LAYOUTS
MAX_SECTIONS = 4  # more translation units / functions than this in one output: not one figure
MAX_STEPS = 16  # more `after` lines than this (p08_build --incremental) in one output: not one figure
ORDER_BASE = 0  # `order po|rpo` lists are numbered from here (the index shown in the xlabel)
PLAIN_DIAG_DRAWS = False  # p11_check: plain `diag` output is a list of diagnostics; only `--trace` draws the paths
# The flags the site build gives dot (DOT_FLAGS in scripts/build_site.py; test_cg checks that they agree): the natural width depends on them.
DOT_FLAGS = [
    "-Gbgcolor=transparent", "-Gpad=0.15", "-Gnodesep=0.35", "-Granksep=0.45", "-Gfontname=Helvetica",
    "-Nfontname=Menlo", "-Efontname=Helvetica", "-Gfontsize=12", "-Nfontsize=12", "-Efontsize=11",
    "-Nshape=box", "-Nstyle=rounded", "-Nmargin=0.12,0.06", "-Earrowsize=0.7",
]
TITLE_WRAP = 90  # a figure title longer than this (characters) is set in several lines: a long one would make the drawing wider than the page
TIMES = "\u00d7"


class _Skip(Exception):
    """The output is not (all) in a format this module draws."""


# --------------------------------------------------------------------------
# a small dot writer
# --------------------------------------------------------------------------
_ENTITY = re.compile(r"&(?=#?\w+;)")  # dot reads "&amp;" inside labels; keep a literal "&name;" literal


def _esc(text: str) -> str:
    text = text.replace("\\", "\\\\").replace('"', '\\"').replace("\t", " ").replace("\n", " ")
    return _ENTITY.sub("&amp;", text)


def _q(text: str) -> str:
    return '"' + _esc(text) + '"'


def _title(text: str) -> str:
    """A graph title as a label: one line, or several left-justified lines when it is longer than TITLE_WRAP."""
    lines = textwrap.wrap(text, TITLE_WRAP, break_long_words=False, break_on_hyphens=False) or [text]
    if len(lines) == 1:
        return _q(text)
    return '"' + "".join(_esc(x) + "\\l" for x in lines) + '"'


def _tip(text: str) -> str:
    """A tooltip: one line per line of `text` (dot's \\n escape)."""
    return '"' + "\\n".join(_esc(x) for x in text.split("\n")) + '"'


def _gname(*parts: str) -> str:
    s = re.sub(r"[^A-Za-z0-9_]+", "_", "_".join(parts)).strip("_")[:48] or "g"
    return s if not s[0].isdigit() else "g_" + s


def _wrap(name: str, width: int) -> list[str]:
    """A name as label lines of at most `width` characters: broken after `::`, then after `,` `<` `(` and spaces, then after `_`, then hard."""
    if len(name) <= width:
        return [name]
    pieces = [name]
    for sep in (r"(?<=::)", r"(?<=,)|(?<=<)|(?<=\()(?!\))|(?<= )", r"(?<=_)"):
        pieces = [p for piece in pieces for p in (re.split(sep, piece) if len(piece) > width else [piece])]
    lines: list[str] = []
    for p in pieces:
        while len(p) > width:  # no break point left: cut
            lines.append(p[:width])
            p = p[width:]
        if lines and len(lines[-1]) + len(p) <= width:
            lines[-1] += p
        else:
            lines.append(p)
    return lines


class _G:
    """Nodes, edges and clusters in creation order; node() and edge() are upserts keyed by id / (a, b, label, classes)."""

    def __init__(self, name: str, title: str = ""):
        self.name = _gname(name)
        self.title = title
        self.nodes: dict[str, dict] = {}
        self.edges: list[dict] = []
        self.clusters: dict[str, dict] = {}
        self.stack: list[str] = []  # cluster ids to stack top to bottom with invisible edges
        self.same: list[list[str]] = []  # node ids that share a rank (a column, left to right)
        self.rankdir = "TB"
        self.fixed_rankdir = ""  # a graph that only reads one way ("LR": a table of rows) is not offered the other layout

    def cluster(self, cid: str, label: str, cls: str) -> None:
        self.clusters[cid] = {"label": label, "cls": cls}

    def node(self, nid: str, lines: list[str], cls: str = "", tip: str | None = None, xlabel: str = "", cluster: str | None = None,
             left: bool = False) -> None:
        """`left`: the first line is a centered header, the others are left-justified (the lines of a CFG block)."""
        n = self.nodes.setdefault(nid, {"cluster": None})
        n.update(lines=lines, cls=cls, tip=tip, xlabel=xlabel, left=left)
        if cluster is not None:
            n["cluster"] = cluster

    def edge(self, a: str, b: str, label: str = "", cls: str = "", tip: str | None = None, invis: bool = False, minlen: int = 1) -> None:
        """`minlen`: ranks between the ends (an invisible edge with 2 puts b below everything that hangs on a)."""
        for e in self.edges:
            if (e["a"], e["b"], e["label"], e["cls"], e["invis"]) == (a, b, label, cls, invis):
                return
        self.edges.append({"a": a, "b": b, "label": label, "cls": cls, "tip": tip, "invis": invis, "minlen": minlen})

    def drawable(self) -> bool:
        return len(self.nodes) >= 2 and any(not e["invis"] for e in self.edges)

    def _node_stmt(self, nid: str, n: dict, indent: str) -> str:
        if n.get("left") and len(n["lines"]) > 1:
            label = _esc(n["lines"][0]) + "\\n" + "".join(_esc(x) + "\\l" for x in n["lines"][1:])
        else:
            label = "\\n".join(_esc(x) for x in n["lines"])
        attrs = [f'label="{label}"']
        if n["xlabel"]:
            attrs.append(f"xlabel={_q(n['xlabel'])}")
        if n["cls"]:
            attrs.append(f"class={_q(n['cls'])}")
        if n["tip"]:
            attrs.append(f"tooltip={_tip(n['tip'])}")
        return f"{indent}{_q(nid)} [{', '.join(attrs)}];"

    def _edge_stmt(self, e: dict) -> str:
        attrs = []
        if e["invis"]:
            attrs.append("style=invis")
            if e.get("minlen", 1) > 1:
                attrs.append(f"minlen={e['minlen']}")
        else:
            if e["label"]:
                attrs.append(f"label={_q(e['label'])}")
            if e["cls"]:
                attrs.append(f"class={_q(e['cls'])}")
            if e["tip"]:
                attrs.append(f"tooltip={_tip(e['tip'])}")
        return f"  {_q(e['a'])} -> {_q(e['b'])}" + (f" [{', '.join(attrs)}]" if attrs else "") + ";"

    def dot(self) -> str:
        for e in self.edges:  # an edge never names a node that was not declared
            for end in (e["a"], e["b"]):
                self.nodes.setdefault(end, {"cluster": None, "lines": [end], "cls": "", "tip": None, "xlabel": ""})
        edges = list(self.edges)
        by_cluster: dict[str, list[str]] = {c: [] for c in self.clusters}
        for nid, n in self.nodes.items():
            if n["cluster"] in by_cluster:
                by_cluster[n["cluster"]].append(nid)
        for a, b in zip(self.stack, self.stack[1:]):  # one invisible edge between neighbouring clusters
            if by_cluster.get(a) and by_cluster.get(b):
                edges.append({"a": by_cluster[a][-1], "b": by_cluster[b][0], "label": "", "cls": "", "tip": None, "invis": True})
        out = [f"digraph {self.name} {{", f"  rankdir={self.rankdir};"]
        if not any(e["invis"] for e in edges):
            out.append("  ordering=out;")
        if self.title:
            out.append(f"  label={_title(self.title)}; labelloc=t; labeljust=l;")
        for cid, c in self.clusters.items():
            out.append(f"  subgraph cluster_{_gname(cid)} {{")
            out.append(f"    label={_q(c['label'])}; labeljust=l;")
            if c["cls"]:
                out.append(f"    class={_q(c['cls'])};")
            out += [self._node_stmt(nid, self.nodes[nid], "    ") for nid in by_cluster[cid]]
            out.append("  }")
        out += [self._node_stmt(nid, n, "  ") for nid, n in self.nodes.items() if n["cluster"] not in self.clusters]
        out += [f"  {{ rank=same; {'; '.join(_q(n) for n in ids)}; }}" for ids in self.same]
        out += [self._edge_stmt(e) for e in edges]
        out.append("}")
        return "\n".join(out) + "\n"


def _natural_px(dot: str) -> float | None:
    """Natural width in px (96 dpi) of the drawing dot makes of this source; None when dot is not there or fails."""
    try:
        p = subprocess.run(["dot", "-Tsvg", *DOT_FLAGS], input=dot, capture_output=True, text=True, timeout=30)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None
    m = re.search(r'viewBox="[\d.\-]+ [\d.\-]+ ([\d.\-]+) ', p.stdout)
    return float(m[1]) * 96 / 72 if p.returncode == 0 and m else None


def _fit(build: Callable[[int], _G]) -> str | None:
    """The graph in the first layout whose drawing is at most BUDGET_PX wide; None when it has nothing to draw.

    Top to bottom with the names wrapped at 24, then 18 characters; then left to right, where a wide rank stacks
    vertically (not for several stacked clusters); then tighter wraps. When nothing fits, the narrowest drawing.
    """
    cands: list[tuple[float, str]] = []
    for rankdir, width in LAYOUTS:
        g = build(width)
        if not g.drawable():
            return None
        if rankdir == "LR" and len(g.stack) > 1:
            continue
        g.rankdir = g.fixed_rankdir or rankdir
        dot = g.dot()
        px = _natural_px(dot)
        if px is None or px <= BUDGET_PX:
            return dot
        cands.append((px, dot))
    return min(cands, key=lambda c: c[0])[1] if cands else None


# --------------------------------------------------------------------------
# helpers on the command text and on tool lines
# --------------------------------------------------------------------------
def _tool(*names: str) -> re.Pattern:
    """A command that runs one of the tools: `build/bin/<tool>` or `scripts/run.sh <tool>` (not `manifests/p11_xtu.h`)."""
    return re.compile(r"(?:build/bin/|run\.sh\s+)(?:" + "|".join(map(re.escape, names)) + r")(?![\w.-])")


def _options(command: str, tool: str) -> list[str]:
    """The `--option[=value]` words of the first `<tool> ...` call in the command."""
    m = _tool(tool).search(command)
    if not m:
        return []
    rest = re.split(r"[;|&>]|\n", command[m.end():], maxsplit=1)[0]
    return [w for w in rest.split() if w.startswith("--")]


_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|(\S+)')
_NOISE = re.compile(r"^\[\d+/\d+\] Processing file ")  # ClangTool's progress line on stderr (p11_xtu, p11_check)
_NAME = r'"(?:[^"\\]|\\.)*"|\S+'  # a name as the tools print it: quoted when it has a space


def _tokens(s: str) -> list[str]:
    """Words of a tool line; a name with spaces is printed in double quotes and comes back as one word."""
    return [m[1].replace('\\"', '"').replace("\\\\", "\\") if m[1] is not None else m[2] for m in _TOKEN.finditer(s)]


def _name(s: str) -> str:
    t = _tokens(s)
    return t[0] if len(t) == 1 else s.strip()


def _lines(text: str) -> list[str]:
    out = [ln.rstrip() for ln in text.splitlines() if ln.strip() and not _NOISE.match(ln)]
    if not out:
        raise _Skip
    return out


def _short_site(site: str) -> str:
    """`@L12` and a block (`B3`) stay; `@dir/file.cpp:12` becomes `L12` (the file stays in the tooltip)."""
    return site if site.startswith("@L") or not site.startswith("@") else "L" + site.rsplit(":", 1)[-1]


# --------------------------------------------------------------------------
# the call graph a text shows
# --------------------------------------------------------------------------
class _Cg:
    """Nodes in first-seen order, edges with their call sites, plus what the text says about the nodes."""

    def __init__(self) -> None:
        self.nodes: dict[str, dict] = {}  # name -> {"kind", "flags": [], "usr", "tus": [], "tip": [], "dups"}
        self.edges: dict[tuple[str, str], dict] = {}  # (a, b) -> {"n", "sites": [], "kinds": [], "exprs": [], "extra": []}
        self.root = ""  # the name of the synthetic root node when the text has one

    def node(self, name: str, **attrs) -> dict:
        if name in ("<root>", "< root >"):
            self.root = name
        n = self.nodes.setdefault(name, {"kind": "", "flags": [], "usr": "", "tus": [], "tip": [], "dups": 1})
        for k, v in attrs.items():
            if v:
                n[k] = v
        return n

    def edge(self, a: str, b: str, site: str = "", kind: str = "", expr: str = "", extra: str = "", relation: bool = False) -> None:
        """`relation`: not a call (an address taken, a transitive caller): it never makes a cycle, unless a call edge joins it."""
        self.node(a)
        self.node(b)
        e = self.edges.setdefault((a, b), {"n": 0, "sites": [], "kinds": [], "exprs": [], "extra": [], "relation": True})
        e["relation"] = e["relation"] and relation
        e["n"] += 1
        for key, val in (("sites", site), ("kinds", kind), ("exprs", expr), ("extra", extra)):
            if val and val not in e[key]:
                e[key].append(val)

    def cycles(self) -> tuple[list[list[str]], set[tuple[str, str]]]:
        """(cyclic SCCs as lists of names, DFS back edges) over the drawn edges.

        The DFS starts at the root, then at the other nodes in the order the text lists them; it follows the
        successors in the order the text lists them. A back edge is an edge into a node still on the DFS stack.
        Edges that are relations, not calls, are not followed.
        """
        succ: dict[str, list[str]] = {n: [] for n in self.nodes}
        for (a, b), e in self.edges.items():
            if not e["relation"]:
                succ[a].append(b)
        index: dict[str, int] = {}
        low: dict[str, int] = {}
        on: set[str] = set()
        st: list[str] = []
        sccs: list[list[str]] = []
        counter = [0]

        def visit(v: str) -> None:
            index[v] = low[v] = counter[0]
            counter[0] += 1
            st.append(v)
            on.add(v)
            for w in succ[v]:
                if w not in index:
                    visit(w)
                    low[v] = min(low[v], low[w])
                elif w in on:
                    low[v] = min(low[v], index[w])
            if low[v] == index[v]:
                comp = []
                while True:
                    w = st.pop()
                    on.discard(w)
                    comp.append(w)
                    if w == v:
                        break
                if len(comp) > 1 or v in succ[v]:
                    sccs.append(sorted(comp, key=list(self.nodes).index))

        for v in self.nodes:
            if v not in index:
                visit(v)
        state: dict[str, int] = {}
        back: set[tuple[str, str]] = set()
        for s in ([self.root] if self.root in self.nodes else []) + [n for n in self.nodes if n != self.root]:
            if s in state:
                continue
            state[s] = 1
            stack = [(s, iter(succ[s]))]
            while stack:
                n, it = stack[-1]
                for w in it:
                    if state.get(w) == 1:
                        back.add((n, w))
                    elif w not in state:
                        state[w] = 1
                        stack.append((w, iter(succ[w])))
                        break
                else:
                    state[n] = 2
                    stack.pop()
        return sccs, back


def _edge_label(e: dict, sites: bool = True) -> str:
    parts = [k for k in e["kinds"] if k != "call"]
    if sites and e["sites"]:
        parts.append(", ".join(_short_site(s) for s in e["sites"]))
    elif e["n"] > 1 and not e["sites"]:
        parts.append(f"{TIMES}{e['n']}")
    return " ".join(parts)


def _edge_tip(e: dict) -> str | None:
    bits = [*e["exprs"], *e["extra"], *([f"{e['n']} call sites"] if e["n"] > 1 and e["sites"] else []), *([", ".join(e["sites"])] if e["sites"] else [])]
    return ", ".join(bits) if bits else None


def _plain_draw(cg: _Cg, name: str, title: str = "", *, clusters: list[tuple[str, str, list[str]]] = (), stack: bool = False,
                xlabels: dict[str, str] | None = None, node_cls: dict[str, list[str]] | None = None,
                node_tip: dict[str, list[str]] | None = None, node_lines: dict[str, list[str] | Callable[[int], list[str]]] | None = None,
                edge_cls: dict[tuple[str, str], list[str]] | None = None, cluster_cls: str = "scc",
                label_edge: Callable[[dict], bool] = lambda e: True, display: dict[str, str] | None = None) -> str | None:
    """The generic call-graph figure: classes from the text (root, kinds, flags), recursion from the drawn edges, extras from the caller.

    `clusters` are (id, label, member names) of `cluster_cls`; `stack` lays them out top to bottom with invisible edges;
    `label_edge` says which edges show their call sites (the others keep them in the tooltip); `display` is the name a node
    shows when it is not its id (`?` for the callee a pointer call does not have); a `node_lines` entry is a list of lines or
    a function of the wrap width that makes them.
    """
    cyc, back = cg.cycles()
    recursive = {n for comp in cyc for n in comp}
    xl, ncls, ntip, nlines, ecls, shown = xlabels or {}, node_cls or {}, node_tip or {}, node_lines or {}, edge_cls or {}, display or {}
    where = {n: cid for cid, _, members in clusters for n in members}

    def build(width: int) -> _G:
        g = _G(name, title)
        for cid, label, _ in clusters:
            g.cluster(cid, label, cluster_cls)
        for n, a in cg.nodes.items():
            cls: list[str] = []
            if n == cg.root:
                cls.append("root")
            if a["kind"] == "decl":
                cls.append("dim")
            if "noreturn" in a["flags"]:
                cls.append("sink")
            if n in recursive:
                cls.append("recursive")
            cls += ncls.get(n, [])
            tags = ([a["kind"]] if a["kind"] not in ("", "def") else []) + a["flags"]
            name_lines = _wrap(shown.get(n, n), width)
            extra = nlines.get(n, [])
            lines = [*name_lines, *(extra(width) if callable(extra) else extra), *([", ".join(tags)] if tags else []),
                     *([f"({a['dups']} nodes)"] if a["dups"] > 1 else [])]
            tip = [n, *([f"kind={a['kind']}"] if a["kind"] else []), *a["flags"], *([f"usr={a['usr']}"] if a["usr"] else []),
                   *([f"tus={','.join(a['tus'])}"] if a["tus"] else []), *a["tip"], *ntip.get(n, [])]
            wrapped = len(name_lines) > 1  # the full name goes in the tooltip
            g.node(n, lines, " ".join(dict.fromkeys(cls)), "\n".join(tip) if len(tip) > 1 or wrapped else None, xl.get(n, ""), where.get(n))
        for (a, b), e in cg.edges.items():
            cls = []
            if a == cg.root:
                cls.append("weak")
            if (a, b) in back:
                cls.append("back")
            cls += ecls.get((a, b), [])
            g.edge(a, b, _edge_label(e, label_edge(e)), " ".join(dict.fromkeys(cls)), _edge_tip(e))
        if stack:
            g.stack = [cid for cid, _, _ in clusters]
        return g

    return _fit(build)


# --------------------------------------------------------------------------
# debug.DumpCallGraph (and p08_nodes --dump)
# --------------------------------------------------------------------------
_D_HDR = re.compile(r"^\s*--- Call graph Dump ---\s*$")
_D_FN = re.compile(r"^\s*Function: (?P<name>.*?) calls:(?P<rest>.*)$")


def _split_callees(rest: str, known: set[str]) -> list[str]:
    """`operator new Holder::Holder < > fail` -> names: the nodes of the dump are known, so the longest known run of words is a name."""
    words = rest.split()
    out: list[str] = []
    i = 0
    while i < len(words):
        for j in range(min(len(words), i + 4), i, -1):
            cand = " ".join(words[i:j])
            if cand in known:
                out.append(cand)
                i = j
                break
        else:  # a callee the output does not list as a node (cut by grep): the two shapes that contain a space
            if words[i] == "<" and i + 1 < len(words) and words[i + 1] == ">":
                out.append("< >")
                i += 2
            elif words[i] == "operator" and i + 1 < len(words):
                out.append(f"operator {words[i + 1]}")
                i += 2
            else:
                out.append(words[i])
                i += 1
    return out


def _dump(text: str, cmd: str = "") -> str | None:
    """`debug.DumpCallGraph`: None without a `Function: ... calls:` line, with a line that is not part of the dump, or with no edge."""
    rows: list[tuple[str, str]] = []
    for line in _lines(text):
        if _D_HDR.match(line):
            continue
        m = _D_FN.match(line)
        if not m:
            raise _Skip
        rows.append((m["name"], m["rest"]))
    if not rows:
        raise _Skip
    known = {n for n, _ in rows}
    cg = _Cg()
    seen: set[str] = set()
    for name, rest in rows:
        a = cg.node(name)
        if name in seen:
            a["dups"] += 1  # two nodes that print the same
        seen.add(name)
        for callee in _split_callees(rest, known):
            cg.edge(name, callee)
    return _plain_draw(cg, "call_graph")


# --------------------------------------------------------------------------
# the tool grammar shared by p08_nodes, p09_walk, p10_summary, p11_resolve, p11_xtu
# --------------------------------------------------------------------------
_HDR_NODES = re.compile(r"^== (?P<file>.+?): (?P<n>\d+) nodes, (?P<m>\d+) edges$")
_SITE_AT = re.compile(r"^@L\d+$")
_FILE_AT = re.compile(r"^@[^:\s]+:\d+$")
_SCC = re.compile(r"^scc (?P<id>\d+)(?: (?P<cyc>cyclic)(?: (?P<kind>self|mutual))?)?: ?(?P<rest>.*)$")
_ORDER = re.compile(r"^order (?P<kind>po|rpo|scc)(?P<rev> reverse)?: ?(?P<rest>.*)$")  # `reverse`: --reverse, the order over the reverse graph
_REACH = re.compile(rf"^reach (?P<name>{_NAME}): ?(?P<rest>.*)$")
_DEAD = re.compile(r"^dead: ?(?P<rest>.*)$")
_CALLERS = re.compile(rf"^callers(?P<star>\*)? (?P<name>{_NAME}): ?(?P<rest>.*)$")
_TRACE_SCC = re.compile(r"^trace scc (?P<id>\d+) iter (?P<k>\d+): ?(?P<rest>.*)$")
_TRAV = re.compile(rf"^order (?P<kind>dfs|bfs)(?P<rev> reverse)? (?P<start>{_NAME}): ?(?P<rest>.*)$")  # order bfs main: main@0 helper@1 ...
_PATH_NONE = re.compile(rf"^path (?:{_NAME}) -> (?:{_NAME}): none$")
_PATH = re.compile(r"^path (?P<rest>.+?)(?: \((?P<n>\d+) calls?\))?$")  # path main -> die -> fail (2 calls)
_CYCLE_NONE = re.compile(rf"^cycle (?:{_NAME}): none$")
_CYCLE = re.compile(r"^cycle (?P<rest>.+)$")
_PATHS_NOTE = re.compile(r"^(?P<rest>paths: \d+ shown, \d+ found, limit \d+)$")


def _names(rest: str) -> list[str]:
    """A list of names as the tools print it: `-` is the empty list."""
    t = _tokens(rest)
    return [] if t == ["-"] else t


def _levels(rest: str) -> list[tuple[str, int | None]]:
    """`main@0 helper@1 "operator new"@2` -> [(name, level)]; names with no `@level` (a dfs list) have level None."""
    out: list[tuple[str, int | None]] = []
    for t in _names(rest):
        if re.fullmatch(r"@\d+", t) and out:  # the level after a quoted name is a word of its own
            out[-1] = (out[-1][0], int(t[1:]))
        elif m := re.fullmatch(r"(.+)@(\d+)", t):
            out.append((m[1], int(m[2])))
        else:
            out.append((t, None))
    return out


def _chain(rest: str) -> list[str]:
    """`a -> b -> c` -> names; fewer than two is not a chain."""
    names = [_name(x) for x in re.split(r"\s+->\s+", rest.strip())]
    if len(names) < 2:
        raise _Skip
    return names


def _node_line(cg: _Cg, toks: list[str]) -> None:
    """`node <name> [kind=def|decl|...] [noreturn] [static] [usr=...|usr=-] [tus=a,b] [scc=3 po=1 rpo=4 recursive dead]`"""
    if len(toks) < 2:
        raise _Skip
    attrs: dict[str, object] = {}
    flags: list[str] = []
    extra: list[str] = []
    for t in toks[2:]:
        k, eq, v = t.partition("=")
        if eq and k in ("kind", "usr", "tus"):
            attrs[k] = [x for x in v.split(",") if x] if k == "tus" else ("" if v == "-" else v)  # `usr=-`: the node has none
        elif t in ("noreturn", "static"):
            flags.append(t)
        else:
            extra.append(t)  # scc=3 po=1 rpo=4 recursive dead: whatever else the line says
    n = cg.node(toks[1], **attrs)
    n["flags"] += [f for f in flags if f not in n["flags"]]
    n["tip"] += extra


def _edge_line(cg: _Cg, toks: list[str]) -> None:
    """`edge <caller> -> <callee> [@L<line> <ExprClass>] [@<file>:<line>] [kind=call|ctor|new|objc|block|op] [xtu]`"""
    if len(toks) < 4 or toks[2] != "->":
        raise _Skip
    site, kind, expr, extra = "", "", "", ""
    for t in toks[4:]:
        if _SITE_AT.match(t) or _FILE_AT.match(t):
            site = t
        elif t.startswith("kind="):
            kind = t[5:]
        elif t == "xtu":
            extra = "xtu"
        elif re.fullmatch(r"[A-Z]\w+", t):
            expr = t
        else:
            raise _Skip
    cg.edge(toks[1], toks[3], site, kind, expr, extra)


_TOOL_NOTE = re.compile(r"^(?:p\d\d_\w+: .+|exit=\d+)$")  # a tool's own error line (`p09_walk: --path needs two different functions`), or a doc's `echo exit=$?`


class _Text:
    """The records of a tool output: `take` fills the call graph from node / edge lines and keeps the rest in lists."""

    def __init__(self) -> None:
        self.cg = _Cg()
        self.headers: list[re.Match] = []
        self.scc: list[dict] = []
        self.order: dict[str, list[str]] = {}
        self.reach: dict[str, list[str]] = {}
        self.dead: list[str] = []
        self.callers: list[tuple[str, list[str], bool]] = []
        self.trace: list[tuple[int, int, list[str]]] = []
        self.trav: list[tuple[str, str, list[tuple[str, int | None]]]] = []  # (dfs | bfs, start, [(name, level)])
        self.paths: list[list[str]] = []  # the `path` chains, shortest first
        self.cycles: list[list[str]] = []
        self.paths_note = ""  # `paths: 3 shown, 5 found, limit 8`
        self.has_dump = False

    def walk_only(self) -> bool:
        """Lines only the `p09_walk` views print: a tool that does not take them gives None."""
        return bool(self.order or self.reach or self.dead or self.callers or self.trav or self.paths or self.cycles or self.paths_note)

    def take(self, line: str) -> bool:
        """Record one line; False when it is not one of the shared forms."""
        if m := _HDR_NODES.match(line):
            self.headers.append(m)
        elif _D_HDR.match(line) or _D_FN.match(line):
            self.has_dump = True
        elif line.startswith("node "):
            _node_line(self.cg, _tokens(line))
        elif line.startswith("edge "):
            _edge_line(self.cg, _tokens(line))
        elif m := _SCC.match(line):
            rest, iters = m["rest"], None
            if im := re.search(r"\s+iter=(\d+)$", rest):
                rest, iters = rest[: im.start()], int(im[1])
            self.scc.append({"id": int(m["id"]), "cyclic": bool(m["cyc"]), "kind": m["kind"] or "", "members": _names(rest), "iter": iters})
        elif m := _ORDER.match(line):
            self.order[m["kind"] + (m["rev"] or "")] = _names(m["rest"])
        elif m := _REACH.match(line):
            self.reach[_name(m["name"])] = _names(m["rest"])
        elif m := _DEAD.match(line):
            self.dead += _names(m["rest"])
        elif m := _CALLERS.match(line):
            self.callers.append((_name(m["name"]), _names(m["rest"]), bool(m["star"])))
        elif m := _TRACE_SCC.match(line):
            self.trace.append((int(m["id"]), int(m["k"]), _tokens(m["rest"])))
        elif m := _TRAV.match(line):
            self.trav.append((m["kind"] + (m["rev"] or ""), _name(m["start"]), _levels(m["rest"])))
        elif _PATH_NONE.match(line) or _CYCLE_NONE.match(line):
            pass  # no chain: nothing to draw
        elif m := _PATH.match(line):
            self.paths.append(_chain(m["rest"]))
        elif m := _CYCLE.match(line):
            self.cycles.append(_chain(m["rest"]))
        elif m := _PATHS_NOTE.match(line):
            self.paths_note = m["rest"]
        elif _TOOL_NOTE.match(line):
            pass  # not part of any graph: the Output tab shows it
        else:
            return False
        return True


def _extras(t: _Text) -> dict:
    """The drawing hints of the scc / order / reach / dead / callers lines (shared by p08_nodes, p09_walk, p10_summary, p11_resolve)."""
    cg = t.cg
    out: dict = {"clusters": [], "xlabels": {}, "node_cls": {}, "node_tip": {}, "edge_cls": {}}

    def mark(name: str, cls: str) -> None:
        cg.node(name)
        out["node_cls"].setdefault(name, []).append(cls)

    for s in t.scc:
        if not s["cyclic"]:
            continue
        label = f"scc {s['id']} cyclic" + (f" {s['kind']}" if s["kind"] else "") + (f" iter={s['iter']}" if s["iter"] is not None else "")
        for m in s["members"]:
            mark(m, "recursive")
        if len(s["members"]) > 1 or s["kind"] != "self":
            out["clusters"].append((f"scc{s['id']}", label, s["members"]))
        else:  # one function that calls itself: a self edge, not a cluster
            out["node_tip"].setdefault(s["members"][0], []).append(label)
    kinds = ("po", "rpo", "po reverse", "rpo reverse")  # the xlabel says `reverse` for an order over the reverse graph
    pos = {kind: {name: i for i, name in enumerate(t.order.get(kind, []), ORDER_BASE)} for kind in kinds}
    trav: dict[str, list[str]] = {}  # `order dfs|bfs`: the index in the walk, or the level of a breadth-first one
    for kind, start, items in t.trav:
        for i, (name, level) in enumerate(items, ORDER_BASE):
            trav.setdefault(name, []).append(f"{kind} level {level}" if kind.startswith("bfs") and level is not None else f"{kind} {i}")
    for name in cg.nodes:
        bits = [f"{k} {pos[k][name]}" for k in kinds if name in pos[k]] + trav.get(name, [])
        if bits:
            out["xlabels"][name] = " / ".join(bits)
    for kind, start, items in t.trav:  # a walk from `start` reaches the names it lists; the others are not reached
        cg.node(start)
        mark(start, "entry")
        reached = {n for n, _ in items}
        if not t.dead:
            for name in cg.nodes:
                if name not in (cg.root, start) and name not in reached:
                    mark(name, "dim")
    for start, reached in t.reach.items():
        cg.node(start)
        if start != cg.root:
            mark(start, "entry")
        if not t.dead:  # what the traversal did not reach
            for name in cg.nodes:
                if name not in (cg.root, start) and name not in reached:
                    mark(name, "dim")
    for name in t.dead:
        mark(name, "dim")
    _chains(t, out, mark)
    for name, callers, star in sorted(t.callers, key=lambda x: x[2]):  # the direct callers first: a transitive caller that is one adds nothing
        mark(name, "hl")
        for c in callers:
            if not star:
                cg.edge(c, name)
            elif (c, name) not in cg.edges:  # a relation, not a call edge
                cg.edge(c, name, extra="transitive caller", relation=True)
                out["edge_cls"][(c, name)] = ["weak"]
    return out


def _chains(t: _Text, out: dict, mark: Callable[[str, str], None]) -> None:
    """`path` and `cycle` lines: their edges join the graph (once: `--edges` may have printed them too).

    The first path is the witness: its edges are hl, its start an entry, its end hl. Another path (`--all-paths`) is an
    alternative: the edges it adds are weak and the nodes only it names dim. A cycle needs no marks: its nodes are
    recursive and its closing edge is a DFS back edge of the drawn graph, like any other.
    """
    cg = t.cg
    witness = set(zip(t.paths[0], t.paths[0][1:])) if t.paths else set()
    on_witness = set(t.paths[0]) if t.paths else set()
    for names in [*t.paths, *t.cycles]:
        for a, b in zip(names, names[1:]):
            if (a, b) not in cg.edges:
                cg.edge(a, b)
    for key in witness:
        out["edge_cls"].setdefault(key, []).append("hl")
    for names in t.paths[1:]:
        for key in zip(names, names[1:]):
            if key not in witness:
                out["edge_cls"].setdefault(key, []).append("weak")
        for n in names:
            if n not in on_witness:
                mark(n, "dim")
    if t.paths:
        mark(t.paths[0][0], "entry")
        mark(t.paths[0][-1], "hl")


def _emitted_dot(text: str) -> str | None:
    """A tool's own `--emit=dot`: already a diagram source with class= only; one cut by head / sed gets its closing braces back.

    Laid out left to right when that makes it narrower than the page and the tool did not pick a direction.
    """
    src = text.strip()
    opens = src.count("{") - src.count("}")
    if opens < 0 or " -> " not in src:
        return None
    src += "\n" + "}\n" * opens
    if "rankdir" not in src and (px := _natural_px(src)) is not None and px > BUDGET_PX:
        lr = src.replace("{\n", "{\n  rankdir=LR;\n", 1)
        if (px_lr := _natural_px(lr)) is not None and px_lr < px:
            return lr
    return src


def _parser(fn: Callable[[str, str], str | None]) -> Callable[[str, str], str | None]:
    """A parser that gives None for anything it cannot draw faithfully, and passes a tool's `--emit=dot` text through."""

    def parse(text: str, cmd: str = "") -> str | None:
        try:
            if re.match(r"\s*digraph\b", text):
                return _emitted_dot(text)
            return fn(text, cmd)
        except _Skip:
            return None

    parse.__doc__ = fn.__doc__
    parse.__name__ = fn.__name__.lstrip("_")
    return parse


# --------------------------------------------------------------------------
# p08_nodes, p09_walk
# --------------------------------------------------------------------------
@_parser
def _nodes(text: str, cmd: str) -> str | None:
    """`node` / `edge` / `scc` / `order` / `reach` / `dead` / `callers` / `path` / `cycle` lines; a `--dump` goes to the dump parser; None without an edge."""
    lines = _lines(text)
    if any(_D_FN.match(x) for x in lines):
        return _dump(text)
    t = _Text()
    for line in lines:
        if not t.take(line):
            raise _Skip
    if len(t.headers) > 1 or t.trace:
        raise _Skip
    cg = t.cg
    extras = _extras(t)
    if not cg.edges:
        return None
    kinds = [k for k, v in (("sccs", t.scc), ("order", t.order or t.trav), ("reach", t.reach), ("dead", t.dead), ("callers", t.callers),
                            ("paths", t.paths), ("cycles", t.cycles)) if v]
    title = " ".join(x for x in (t.headers[0]["file"] if t.headers else "", t.paths_note) if x)
    return _plain_draw(cg, "_".join(["walk", *kinds]) if kinds else "nodes", title,
                       clusters=extras["clusters"], xlabels=extras["xlabels"], node_cls=extras["node_cls"], node_tip=extras["node_tip"],
                       edge_cls=extras["edge_cls"])


# --------------------------------------------------------------------------
# p10_summary
# --------------------------------------------------------------------------
_SUMMARY = re.compile(rf"^summary (?P<name>{_NAME}): (?P<rest>.*)$")
_VALUE = re.compile(r"(?:sink=(?:none|may|always)|depth=(?:\d+|inf))")


@_parser
def _summary(text: str, cmd: str) -> str | None:
    """`summary` values in the labels, the cyclic `scc` clusters with `iter=`, `trace` in tooltips; edges from `via` and `edge` lines."""
    t = _Text()
    values: dict[str, str] = {}
    vias: list[tuple[str, str, str]] = []  # (function, the callee its answer comes from, what the answer is)
    for line in _lines(text):
        if m := _SUMMARY.match(line):
            name = _name(m["name"])
            rest = m["rest"].strip()
            if via := re.search(rf"\s+via (?P<c>{_NAME})$", rest):
                vias.append((name, _name(via["c"]), "witness for sink" if rest.startswith("sink=") else "longest chain"))
                rest = rest[: via.start()]
            if not _VALUE.fullmatch(rest):
                raise _Skip
            t.cg.node(name)
            values[name] = rest
        elif not t.take(line):
            raise _Skip
    if t.has_dump or t.walk_only() or len(t.headers) > 1:
        raise _Skip
    cg = t.cg
    witness: set[tuple[str, str]] = set()
    for a, b, what in vias:
        if (a, b) in cg.edges:  # the graph is there too (--edges): the witness chain is its emphasis
            witness.add((a, b))
            cg.edges[(a, b)]["extra"].append(what)
        else:
            cg.edge(a, b, extra=what)
    has_via = {a for a, _, _ in vias}
    extras = _extras(t)
    for key in witness:
        extras["edge_cls"].setdefault(key, []).append("hl")
    if not cg.edges or not values:
        return None
    lines: dict[str, list[str]] = {}
    for name, v in values.items():
        lines[name] = [v]
        if v == "sink=none":
            extras["node_cls"].setdefault(name, []).append("dim")
        elif v == "sink=always":  # with no `via` it is the sink itself, else every path ends in it
            extras["node_cls"].setdefault(name, []).append("hl" if name in has_via else "sink")
        elif v == "depth=inf":
            extras["node_cls"].setdefault(name, []).append("recursive")
    for opt in _options(cmd, "p10_summary"):
        for name in opt.split("=", 1)[1].split(",") if opt.startswith("--sink=") else []:
            if name in cg.nodes:
                extras["node_cls"].setdefault(name, []).append("sink")
    # a summary that has no `via` and sits in no cyclic component has no edge to hang on: the Output tab lists it
    drawn = {n for e in cg.edges for n in e} | {m for _, _, members in extras["clusters"] for m in members}
    for name in [n for n in cg.nodes if n not in drawn]:
        del cg.nodes[name]
    for sid, k, items in t.trace:
        for item in items:
            nm, _, val = item.partition("=")
            if nm in cg.nodes:
                extras["node_tip"].setdefault(nm, []).append(f"scc {sid} iter {k}: {nm}={val}")
    prop = "sink" if any(v.startswith("sink=") for v in values.values()) else "depth"
    return _plain_draw(cg, "summary_" + prop, "", clusters=extras["clusters"], node_cls=extras["node_cls"], node_tip=extras["node_tip"],
                       node_lines=lines, edge_cls=extras["edge_cls"])


# --------------------------------------------------------------------------
# p10_sites
# --------------------------------------------------------------------------
_HDR_SITES = re.compile(r"^== (?P<fn>.+?): (?P<n>\d+) blocks, (?P<s>\d+) sites$")
_SITE_ID = re.compile(r"^B(?P<b>\d+)\.(?P<i>\d+)$")
_BLOCK = re.compile(r"^B(?P<b>\d+)$")
_WALK = re.compile(rf"^walk (?P<d>\d+) (?P<at>\S+?:B\d+)(?: -> (?P<callee>{_NAME}))?$")
_BLOCK_ID = re.compile(r"^(?P<fn>.+):B(?P<b>\d+)$")


@_parser
def _sites(text: str, cmd: str) -> str | None:
    """`site` lines in one cluster per `== fn` header (the blocks and their `succ` edges too, with `--succs`), or the functions a `--walk` descended into."""
    secs: list[dict] = []  # {"fn", "sites": [site tokens], "succs": [(from, to, T|F|"")]}
    walks: list[tuple[int, str, str]] = []

    def section(fn: str) -> dict:
        if not secs or secs[-1]["fn"] != fn:  # a listing that starts after the `==` line, or a line of another function
            secs.append({"fn": fn, "sites": [], "succs": []})
        return secs[-1]

    for line in _lines(text):
        if m := _HDR_SITES.match(line):
            secs.append({"fn": m["fn"], "sites": [], "succs": []})
        elif line.startswith("site "):
            toks = _tokens(line)
            if len(toks) < 6 or not _SITE_ID.match(toks[2]):
                raise _Skip
            section(toks[1])["sites"].append(toks[1:])
        elif line.startswith("succ "):
            toks = _tokens(line)  # succ <fn> B9 -> B8 [T|F]
            if len(toks) not in (5, 6) or toks[3] != "->" or not _BLOCK.match(toks[2]) or not _BLOCK.match(toks[4]) or (len(toks) == 6 and toks[5] not in ("T", "F")):
                raise _Skip
            section(toks[1])["succs"].append((toks[2], toks[4], toks[5] if len(toks) == 6 else ""))
        elif m := _WALK.match(line):
            walks.append((int(m["d"]), m["at"], _name(m["callee"]) if m["callee"] else ""))
        else:
            raise _Skip
    secs = [sec for sec in secs if sec["sites"]]  # a function with no site is a header line and nothing to draw
    if walks and secs or not (walks or secs) or len(secs) > MAX_SECTIONS:
        return None
    return _walk_graph(walks) if walks else _fit(lambda w: _sites_graph(secs, w))


def _status(rest: list[str]) -> tuple[str, str]:
    """('resolved' | 'virtual' | 'missing:...', the text after it: `static=X`)."""
    if rest[0] in ("resolved", "virtual") or rest[0].startswith("missing:"):
        return rest[0], " ".join(rest[1:])
    raise _Skip


def _call_edge(g: _G, src: str, sid: str, kind: str, callee: str, status: list[str], width: int) -> None:
    """The edge from a call site (or the block that holds it) to its callee, by status; `sid` is the site's `B3.2` label of the edge."""
    word, extra = _status(status)
    label = f"{sid} " if src.rsplit(":", 1)[-1] != sid else ""  # an edge out of a block says which element; out of the element itself it need not
    if word == "virtual":  # the graph knows only the static callee, `static=X`; the callee column may be `?`
        static = extra[len("static="):] if extra.startswith("static=") else callee
        g.node(static, _wrap(static, width), "")
        g.edge(src, static, label + "virtual", "virtual", f"{sid}: the graph records only the static callee {static}")
    elif word == "missing:indirect" or callee == "?":
        unknown = f"?{src}" if src.endswith(f":{sid}") else f"?{src}:{sid}"
        g.node(unknown, ["?"], "note", "no callee the graph can name")
        g.edge(src, unknown, label + "missing indirect", "indirect")
    else:
        g.node(callee, _wrap(callee, width), "dim" if word == "missing:decl" else "")
        if word == "resolved":
            g.edge(src, callee, label.strip(), "call")
        else:
            g.edge(src, callee, label + word.replace("missing:", "missing "), "weak")


def _sites_graph(secs: list[dict], width: int) -> _G:
    """A cluster per function. Without `succ` lines a node per call-site element (stacked in listing order); with them the CFG blocks."""
    g = _G("sites", "")
    for k, sec in enumerate(secs, 1):
        fn, cid = sec["fn"], f"fn{k}"
        g.cluster(cid, fn, "group")
        g.stack.append(cid)
        if sec["succs"]:
            _cfg_cluster(g, cid, sec, width)
            continue
        prev = ""
        for _, sid, kind, callee, *status in sec["sites"]:
            site = f"{fn}:{sid}"
            g.node(site, [sid, kind], "", f"{fn} {sid}\n{kind} {callee} {' '.join(status)}", cluster=cid)
            if prev:
                g.edge(prev, site, invis=True)
            prev = site
            _call_edge(g, site, sid, kind, callee, status, width)
    return g


def _cfg_cluster(g: _G, cid: str, sec: dict, width: int) -> None:
    """The blocks of one function (entry on top, B0 the exit), T / F branch edges, DFS back edges; each call site an edge out of its block."""
    fn = sec["fn"]
    succ: dict[int, list[tuple[int, str]]] = {}
    for a, b, tf in sec["succs"]:
        succ.setdefault(int(a[1:]), []).append((int(b[1:]), tf))
    sites: dict[int, list[list[str]]] = {}
    for toks in sec["sites"]:
        sites.setdefault(int(_SITE_ID.match(toks[1])["b"]), []).append(toks)
    blocks = sorted({*succ, *(b for ss in succ.values() for b, _ in ss), *sites}, reverse=True)
    targets = {b for ss in succ.values() for b, _ in ss}
    entry = next((b for b in blocks if b not in targets), None)
    exit_ = 0 if 0 in blocks and not succ.get(0) else None
    state: dict[int, int] = {}
    back: set[tuple[int, int]] = set()
    if entry is not None:
        state[entry] = 1
        stack = [(entry, iter(succ.get(entry, [])))]
        while stack:
            b, it = stack[-1]
            for nxt, _ in it:
                if state.get(nxt) == 1:
                    back.add((b, nxt))
                elif nxt not in state:
                    state[nxt] = 1
                    stack.append((nxt, iter(succ.get(nxt, []))))
                    break
            else:
                state[b] = 2
                stack.pop()
    for b in blocks:
        head = f"B{b}" + (" (ENTRY)" if b == entry else " (EXIT)" if b == exit_ else "")
        elems = [f"{_SITE_ID.match(t[1])['i']}: {t[3]}" for t in sites.get(b, [])]
        cls = ["entry" if b == entry else "exit" if b == exit_ else ""]
        if any(tf for _, tf in succ.get(b, [])):
            cls.append("cond")
        g.node(f"{fn}:B{b}", [head, *[x if len(x) <= 30 else x[:29] + "\u2026" for x in elems]], " ".join(c for c in cls if c),
               "\n".join([head, *elems]) if any(len(x) > 30 for x in elems) else None, cluster=cid, left=True)
    for b in blocks:
        for nxt, tf in succ.get(b, []):
            cls = [{"T": "t", "F": "f"}.get(tf, ""), "back" if (b, nxt) in back else ""]
            g.edge(f"{fn}:B{b}", f"{fn}:B{nxt}", tf, " ".join(c for c in cls if c))
    for b in blocks:
        for _, sid, kind, callee, *status in sites.get(b, []):
            _call_edge(g, f"{fn}:B{b}", sid, kind, callee, status, width)


def _walk_graph(walks: list[tuple[int, str, str]]) -> str | None:
    """The functions a `--walk` descended into: `walk d fn:Bk -> callee` is an edge fn -> callee labelled with its block."""
    cg = _Cg()
    start = ""
    for _, at, callee in walks:
        m = _BLOCK_ID.match(at)
        if not m:
            raise _Skip
        start = start or m["fn"]
        if callee:
            cg.edge(m["fn"], callee, site=f"B{m['b']}")
    if not cg.edges:
        return None
    return _plain_draw(cg, "walk", "", node_cls={start: ["entry"]}, edge_cls={k: ["call"] for k in cg.edges})


# --------------------------------------------------------------------------
# p11_resolve
# --------------------------------------------------------------------------
_ADD = re.compile(rf"^add (?P<a>{_NAME}) -> (?P<b>{_NAME}) @L(?P<line>\d+) reason=(?P<reason>[\w-]+)(?: candidates=(?P<cand>\d+))?$")
_SKIP = re.compile(rf"^skip (?P<a>{_NAME}) @L(?P<line>\d+) reason=(?P<reason>[\w-]+)$")
_STATS = re.compile(r"^stats: edges (?P<before>\d+) -> (?P<after>\d+), indirect sites (?P<sites>\d+), resolved (?P<res>\d+)$")
_REASON_CLASS = {"fnptr-sig": "indirect", "devirt-static": "hl", "devirt-final": "hl", "cha": "cha", "rta": "cha"}  # as in p11_resolve --emit=dot
_REASON_WORD = {"fnptr-sig": "fnptr", "devirt-static": "devirt", "devirt-final": "devirt final", "cha": "cha", "rta": "rta"}
_INSTANTIATED = re.compile(r"^instantiated: ?(?P<rest>.*)$")


@_parser
def _resolve(text: str, cmd: str) -> str | None:
    """The `add` edges (class and label by reason, rta is cha) over any base `edge` lines; `skip` in tooltips, `instantiated:` and `stats:` as the title; None with no edge."""
    t = _Text()
    adds: list[tuple[str, str, str, str, str]] = []
    skips: dict[str, list[str]] = {}
    stats = ""
    instantiated = ""
    for line in _lines(text):
        if m := _INSTANTIATED.match(line):
            instantiated = "instantiated: " + ", ".join(_names(m["rest"])) if _names(m["rest"]) else ""
        elif m := _ADD.match(line):
            if m["reason"] not in _REASON_CLASS:
                raise _Skip
            adds.append((_name(m["a"]), _name(m["b"]), m["line"], m["reason"], m["cand"] or ""))
        elif m := _SKIP.match(line):
            skips.setdefault(_name(m["a"]), []).append(f"skip @L{m['line']}: {m['reason']}")
        elif m := _STATS.match(line):
            stats = f"edges {m['before']} -> {m['after']}, indirect sites {m['sites']}, resolved {m['res']}"
        elif not t.take(line):
            raise _Skip
    if t.has_dump or t.walk_only() or t.trace or len(t.headers) > 1:
        raise _Skip
    cg = t.cg
    extras = _extras(t)
    for a, b, ln, reason, cand in adds:
        cg.edge(a, b, site=f"@L{ln}", kind=_REASON_WORD[reason], extra=f"{reason}" + (f", {cand} candidates" if cand else ""))
        extras["edge_cls"].setdefault((a, b), []).append(_REASON_CLASS[reason])
    for a, notes in skips.items():
        cg.node(a)
        extras["node_tip"].setdefault(a, []).extend(notes)
    if not cg.edges:
        return None
    return _plain_draw(cg, "resolve", "; ".join(x for x in (instantiated, stats) if x), clusters=extras["clusters"], xlabels=extras["xlabels"], node_cls=extras["node_cls"],
                       node_tip=extras["node_tip"], edge_cls=extras["edge_cls"])


# --------------------------------------------------------------------------
# p11_xtu
# --------------------------------------------------------------------------
_HDR_MERGED = re.compile(r"^== merged(?: from \d+ json files?)?: (?P<n>\d+) nodes, (?P<m>\d+) edges, (?P<t>\d+) tus(?: from \d+ json files?)?$")  # --load says where it merged from
_UNRESOLVED = re.compile(rf"^unresolved (?P<name>{_NAME}) \(decl in (?P<file>[^)]+)\)$")


@_parser
def _xtu(text: str, cmd: str) -> str | None:
    """`node ... tus=` clusters (one tu per file; a function defined in several stands outside), `xtu` edges, decl-only nodes external."""
    cg = _Cg()
    unresolved: dict[str, str] = {}
    for line in _lines(text):
        if _HDR_MERGED.match(line):
            continue
        if m := _UNRESOLVED.match(line):
            unresolved[_name(m["name"])] = m["file"]
        elif line.startswith("node "):
            _node_line(cg, _tokens(line))
        elif line.startswith("edge "):
            _edge_line(cg, _tokens(line))
        else:
            raise _Skip
    if not cg.edges:
        return None
    files = list(dict.fromkeys([f for a in cg.nodes.values() for f in a["tus"]] + list(unresolved.values())))
    if len(files) > MAX_SECTIONS:
        return None
    members: dict[str, list[str]] = {f: [] for f in files}
    node_cls: dict[str, list[str]] = {}
    for n, a in cg.nodes.items():
        if n in unresolved:
            a["tip"].append(f"decl in {unresolved[n]}")
        home = unresolved[n] if n in unresolved and not a["tus"] else a["tus"][0] if len(a["tus"]) == 1 else ""
        if home:
            members[home].append(n)
        if a["kind"] == "decl" or n in unresolved:
            node_cls[n] = ["external", "dim"]
        elif len(a["tus"]) > 1:  # one function, defined in several translation units
            node_cls[n] = ["hl"]
    clusters = [(f"tu{k}", f, members[f]) for k, f in enumerate(files, 1) if members[f]]
    edge_cls = {k: ["xtu"] for k, e in cg.edges.items() if "xtu" in e["extra"]}
    return _plain_draw(cg, "xtu", "", clusters=clusters, stack=len(clusters) > 2, node_cls=node_cls, edge_cls=edge_cls, cluster_cls="tu",
                       label_edge=lambda e: False)  # the two edges of a cross-file cycle share their midpoint: the call sites stay in the tooltips


# --------------------------------------------------------------------------
# p11_check
# --------------------------------------------------------------------------
_DIAG = re.compile(r"^diag (?P<file>[^:\s]+):(?P<line>\d+): (?P<kind>recursion|reaches-sink): (?P<path>.+)$")
_CHECK_TRACE = re.compile(rf"^trace (?P<kind>recursion|reaches-sink): (?P<a>{_NAME}) -> (?P<b>{_NAME}) (?P<site>@[^:\s]+:\d+)$")
_CHECK_SUMMARY = re.compile(r"^summary: \d+ functions, \d+ recursive, \d+ reach a sink$")


@_parser
def _check(text: str, cmd: str) -> str | None:
    """With `--trace`: the `trace` edges (else the `diag` paths) as one graph, the end of each sink chain a sink; plain `diag` output: None."""
    if "--trace" not in _options(cmd, "p11_check") and not PLAIN_DIAG_DRAWS:
        return None
    cg = _Cg()
    paths: list[tuple[str, str, str]] = []  # (kind, path, file:line) of the diag lines
    traced = False
    ends: set[str] = set()
    for line in _lines(text):
        if _CHECK_SUMMARY.match(line):
            continue
        if m := _CHECK_TRACE.match(line):
            traced = True
            cg.edge(_name(m["a"]), _name(m["b"]), site=m["site"])
            if m["kind"] == "reaches-sink":
                ends.add(_name(m["b"]))
        elif m := _DIAG.match(line):
            paths.append((m["kind"], m["path"], f"@{m['file']}:{m['line']}"))
        else:
            raise _Skip
    for kind, path, site in paths:
        names = [_name(x) for x in re.split(r"\s+->\s+", path)]
        if len(names) < 2:
            raise _Skip
        if kind == "reaches-sink":
            ends.add(names[-1])
        if not traced:
            for k, (a, b) in enumerate(zip(names, names[1:])):
                cg.edge(a, b, site=site if k == 0 else "")
    if not cg.edges:
        return None
    # the end of a chain is the sink; a function the chain passes (die in cleanup -> die -> fail) is not
    return _plain_draw(cg, "check", "", node_cls={n: ["sink"] for n in ends if not any(a == n for a, _ in cg.edges)})


# --------------------------------------------------------------------------
# the track tools: key=value words, then p08_build, p08_anycall, p08_mine, p09_metrics
# --------------------------------------------------------------------------
_KV = re.compile(r'([\w-]+)=("(?:[^"\\]|\\.)*"|\S*)')


def _kvs(rest: str) -> dict[str, str]:
    """`kind=Function decl="operator delete" params=2 ret=void *` -> {"kind": ..., "decl": "operator delete", "params": "2", "ret": "void *"}.

    A name with a space is quoted; `ret=` is the one value that is not (it is a type): it runs to the end of the line, or to ` ident=`.
    """
    head, _, tail = rest.partition(" ret=")
    out: dict[str, str] = {}
    for m in _KV.finditer(head):
        v = m[2]
        out[m[1]] = v[1:-1].replace('\\"', '"').replace("\\\\", "\\") if v.startswith('"') else v
    if tail:
        m = _RET.match(tail)
        out["ret"] = m["ret"]
        if m["ident"]:
            out["ident"] = m["ident"]
    return out


def _edge_head(toks: list[str]) -> tuple[str, str]:
    """`edge <caller> -> <callee> ...` -> (caller, callee)."""
    if len(toks) < 4 or toks[0] != "edge" or toks[2] != "->":
        raise _Skip
    return toks[1], toks[3]


def _cmp_edge(toks: list[str], words: tuple[str, ...]) -> tuple[str, str, str]:
    """What follows the callee of a comparison edge -> (site, roles, word): `@L12`, `roles=Call,Dyn` and one of `words` in any order."""
    site = roles = word = ""
    for t in toks[4:]:
        if _SITE_AT.match(t):
            site = t
        elif t.startswith("roles="):
            roles = t[6:]
        elif t in words:
            word = t
        else:
            raise _Skip
    return site, roles, word


_BUILD_FLAGS = re.compile(r"^flags (?P<rest>implicit=\d.*)$")
_BUILD_DIFF = re.compile(r"^diff: nodes (?P<a>\d+) -> (?P<b>\d+)(?: \(.*\))? edges (?P<c>\d+) -> (?P<d>\d+)$")
_AFTER = re.compile(rf"^after (?P<fn>{_NAME}): size=(?P<n>\d+) new: ?(?P<rest>.*)$")


def _steps_graph(steps: list[tuple[str, int, list[str]]], title: str, width: int) -> _G:
    """`--incremental`: a row per `after` line, left to right: the step node, then a weak edge to each name it added; the rows in the order they ran."""
    g = _G("build_steps", title)
    g.fixed_rankdir = "LR"
    prev = ""
    for fn, size, new in steps:
        sid = f"after {fn}"
        g.node(sid, [*_wrap(sid, width), f"size={size}"], "api", f"addToCallGraph({fn})\nsize={size}\nnew: {' '.join(new) or '-'}")
        if prev:
            g.edge(prev, sid, invis=True)
        prev = sid
        for n in new:
            g.node(n, _wrap(n, width), "", f"new after addToCallGraph({fn})")
            g.edge(sid, n, "", "weak")
    g.same.append([f"after {fn}" for fn, _, _ in steps])
    return g


@_parser
def _build(text: str, cmd: str) -> str | None:
    """p08_build: the `node` / `edge` graph (a callee-door node is external), `flags` and `diff:` as the title; `--incremental`
    with no edge: a step node per `after` line; the `flags` / `diff:` lines alone: None."""
    t = _Text()
    flags = diff = ""
    steps: list[tuple[str, int, list[str]]] = []
    for line in _lines(text):
        if m := _BUILD_FLAGS.match(line):
            flags = m["rest"]
        elif m := _BUILD_DIFF.match(line):
            diff = f"nodes {m['a']} -> {m['b']}, edges {m['c']} -> {m['d']}"
        elif m := _AFTER.match(line):
            steps.append((_name(m["fn"]), int(m["n"]), _names(m["rest"])))
        elif not t.take(line):
            raise _Skip
    if t.has_dump or t.walk_only() or t.scc or t.trace or len(t.headers) > 1:
        raise _Skip
    cg = t.cg
    title = " ".join(x for x in (flags, f"({diff})" if diff else "") if x)
    if not cg.edges:
        return _fit(lambda w: _steps_graph(steps, title, w)) if 0 < len(steps) <= MAX_STEPS else None
    node_cls = {n: ["external"] for n, a in cg.nodes.items() if "door=callee" in a["tip"]}
    node_tip: dict[str, list[str]] = {}
    for fn, size, new in steps:  # the nodes each addToCallGraph call added, when the graph is there too
        for n in new:
            node_tip.setdefault(n, []).append(f"new after addToCallGraph({fn}), size={size}")
    return _plain_draw(cg, "build", title, node_cls=node_cls, node_tip=node_tip)


_CALL = re.compile(rf"^call (?P<fn>{_NAME}) @L(?P<line>\d+) (?P<expr>\w+) (?P<rest>kind=.*)$")
_RET = re.compile(r"^(?P<ret>.*?)(?: ident=(?P<ident>\S+))?$")  # what follows ` ret=`: a type, which has spaces and no quotes (`void *`)
_ANY_DECL = re.compile(rf"^decl (?P<name>{_NAME}) (?P<rest>kind=.*)$")
_KINDS = re.compile(r"^kinds: (?P<rest>\w+=\d+(?: \w+=\d+)*)$")


@_parser
def _anycall(text: str, cmd: str) -> str | None:
    """p08_anycall: `call` lines as `fn -> callee` edges labelled with the kind and the line; `decl=?` is a dim `?` node;
    `decl` lines add to the tooltips and `kinds:` is the title; with no `call` line: None."""
    cg = _Cg()
    node_cls: dict[str, list[str]] = {}
    node_tip: dict[str, list[str]] = {}
    edge_cls: dict[tuple[str, str], list[str]] = {}
    display: dict[str, str] = {}
    decls: dict[str, str] = {}
    title = ""
    for line in _lines(text):
        if m := _CALL.match(line):
            fn, site = _name(m["fn"]), f"@L{m['line']}"
            kv = _kvs(m["rest"])
            kind, decl = kv.get("kind", ""), kv.get("decl", "?")
            callee = decl if decl != "?" else f"?{fn}{site}"  # one `?` per pointer call
            sig = f"params={kv.get('params', '?')} ret={kv.get('ret', '?')}" + (f" ident={kv['ident']}" if "ident" in kv else "")
            cg.edge(fn, callee, site, kind, m["expr"], sig)
            if decl == "?":
                display[callee] = "?"
                node_cls[callee] = ["dim"]
                node_tip[callee] = ["no declaration: a call through a pointer"]
                edge_cls[(fn, callee)] = ["indirect"]
            elif kind in ("Destructor", "Deallocator"):  # the callee a `delete` implies, not one the source names
                edge_cls[(fn, callee)] = ["weak"]
        elif m := _ANY_DECL.match(line):
            kv = _kvs(m["rest"])
            decls[_name(m["name"])] = f"decl: kind={kv.get('kind', '?')} params={kv.get('params', '?')} ret={kv.get('ret', '?')}"
        elif m := _KINDS.match(line):
            title = "kinds: " + " ".join(w for w in m["rest"].split() if not w.endswith("=0"))  # the tool prints all eight; the zeros say nothing
        else:
            raise _Skip
    if not cg.edges:
        return None
    for name, tip in decls.items():
        if name in cg.nodes:
            node_tip.setdefault(name, []).append(tip)
    return _plain_draw(cg, "anycall", title, node_cls=node_cls, node_tip=node_tip, edge_cls=edge_cls, display=display)


_MINE_HDR = re.compile(r"^== (?P<file>.+?): lib \d+ nodes \d+ edges, mine \d+ nodes \d+ edges$")
_MINE_DIFF = re.compile(r"^diff: only-lib=\d+ only-mine=\d+ both=\d+$")
_MINE_WORDS = ("both", "only-lib", "only-mine")


@_parser
def _mine(text: str, cmd: str) -> str | None:
    """p08_mine: `edge` lines by their word (both plain, only-mine hl, only-lib weak; `in=` of a node stays in its tooltip), a
    `?(type)` callee a dim node behind an indirect edge; the `diff:` line alone: None."""
    cg = _Cg()
    words: dict[tuple[str, str], set[str]] = {}
    pointers: set[str] = set()
    title = ""
    for line in _lines(text):
        if m := _MINE_HDR.match(line):
            title = m["file"]
        elif _MINE_DIFF.match(line):
            continue
        elif line.startswith("node "):
            _node_line(cg, _tokens(line))
        elif line.startswith("edge "):
            toks = _tokens(line)
            a, b = _edge_head(toks)
            site, _, word = _cmp_edge(toks, _MINE_WORDS)
            cg.edge(a, b, site, extra=word)
            words.setdefault((a, b), set()).add(word)
            if b.startswith("?("):
                pointers.add(b)
        else:
            raise _Skip
    if not cg.edges:
        return None
    return _plain_draw(cg, "mine", title, node_cls={n: ["dim"] for n in pointers}, edge_cls=_cmp_edge_cls(words, "only-mine", "only-lib", pointers))


def _cmp_edge_cls(words: dict[tuple[str, str], set[str]], hl: str, quiet: str, pointers: set[str]) -> dict[tuple[str, str], list[str]]:
    """The classes of the edges of a comparison: only in the new structure hl, only in the old one weak (the vocabulary's quiet edge), a pointer call indirect."""
    out: dict[tuple[str, str], list[str]] = {}
    for key, ws in words.items():
        cls = ["hl"] if hl in ws else ["weak"] if quiet in ws else []
        if key[1] in pointers:
            cls.append("indirect")
        if cls:
            out[key] = cls
    return out


_METRIC = re.compile(rf"^metric (?P<name>{_NAME}) (?P<rest>in=.*)$")
_TOTALS = re.compile(r"^totals: (?P<rest>functions=.*)$")


@_parser
def _metrics(text: str, cmd: str) -> str | None:
    """p09_metrics: `in / out / sites` in the labels of the `metric` nodes (recursive, dead = dim, root = entry), the `edge` lines of
    `--edges` as the graph, `totals:` as the title; `totals:` alone or `--top` without `--edges`: None."""
    t = _Text()
    rows: dict[str, tuple[dict[str, str], list[str]]] = {}
    totals = ""
    for line in _lines(text):
        if m := _METRIC.match(line):
            name = _name(m["name"])
            rows[name] = (_kvs(m["rest"]), [w for w in m["rest"].split() if "=" not in w])
            t.cg.node(name)
        elif m := _TOTALS.match(line):
            totals = m["rest"]
        elif not t.take(line):
            raise _Skip
    if t.has_dump or t.walk_only() or t.scc or t.trace or len(t.headers) > 1:
        raise _Skip
    cg = t.cg
    if not cg.edges:
        return None
    lines: dict[str, list[str]] = {}
    node_cls: dict[str, list[str]] = {}
    node_tip: dict[str, list[str]] = {}
    for name, (kv, flags) in rows.items():
        lines[name] = lambda w, t=f"in {kv.get('in', '?')} out {kv.get('out', '?')} sites {kv.get('sites', '?')}": textwrap.wrap(t, max(w, 10))
        node_tip[name] = [f"{k}={kv[k]}" for k in ("height", "scc") if k in kv] + [f for f in flags if f == "leaf"]
        node_cls[name] = [c for f, c in (("recursive", "recursive"), ("dead", "dim"), ("root", "entry")) if f in flags]
    return _plain_draw(cg, "metrics", totals or (t.headers[0]["file"] if t.headers else ""), node_cls=node_cls, node_tip=node_tip, node_lines=lines)


# --------------------------------------------------------------------------
# p10_callstrings, p10_farm
# --------------------------------------------------------------------------
_CS_NODE = r'(?:"(?:[^"\\]|\\.)*"|[^\s\[\]]+)\[[^\]]*\]'  # divide[safe@L12]
_CTX = re.compile(rf"^ctx k=(?P<k>\d+) (?P<node>{_CS_NODE}): ?(?P<vals>.*)$")
_WARN = re.compile(rf"^warn (?P<at>\S+?:\d+): (?P<node>{_CS_NODE}) divides by (?P<p>\w+)=(?P<v>zero|top)$")  # zero, or top: it may be zero
_CS_TRACE = re.compile(rf"^trace (?P<node>{_CS_NODE}) <- (?P<caller>{_CS_NODE}) @L(?P<line>\d+): ?(?P<vals>.*)$")
_CS_SUMMARY = re.compile(r"^summary: (?P<rest>k=\d+ contexts=\d+ functions=\d+ warnings=\d+)$")
_CS_SPLIT = re.compile(r"^(?P<fn>.*)\[(?P<string>[^\]]*)\]$")


def _values(rest: str) -> list[str]:
    """`a=top b=zero` -> ["a=top", "b=zero"]; `-` (no parameter) is the empty list."""
    return _names(rest)


def _wrap_string(string: str, width: int) -> list[str]:
    """`[…,loop@L25,loop@L24]` as lines of whole sites (broken after the commas, never inside a site)."""
    lines: list[str] = []
    for piece in re.split(r"(?<=,)", f"[{string}]"):
        if lines and len(lines[-1]) + len(piece) <= width:
            lines[-1] += piece
        else:
            lines.append(piece)
    return lines


def _context_lines(string: str, values: list[str]) -> Callable[[int], list[str]]:
    """The label lines under a context's function name: its call string, then its values, both wrapped to the layout's width."""
    return lambda w: [*_wrap_string(string, w), *(_wrap(" ".join(values), w) if values else [])]


def _cs_fn(node: str) -> str:
    """`divide[safe@L12]` -> `divide`; the name of a quoted function comes back without its quotes."""
    return _name(_CS_SPLIT.match(node)["fn"])


@_parser
def _callstrings(text: str, cmd: str) -> str | None:
    """p10_callstrings: with `trace` lines one node per `fn[string]` and an edge caller -> callee per line (the `<-` reversed), the
    `ctx` values in the labels, the `warn` targets hl; with `--edges` and no `trace` the function-level graph, a function's
    `ctx` rows in its tooltip and the `warn` targets hl; `summary:` as the title; neither `--trace` nor `--edges`: None."""
    t = _Text()
    ctx: dict[str, tuple[int, list[str]]] = {}
    traces: list[tuple[str, str, str, str]] = []  # (callee node, caller node, line, what the call passes: `a=zero b=top`)
    warns: list[tuple[str, str, str, str]] = []  # (node, param, value, file:line)
    title = ""
    for line in _lines(text):
        if m := _CTX.match(line):
            ctx[m["node"]] = (int(m["k"]), _values(m["vals"]))
        elif m := _WARN.match(line):
            warns.append((m["node"], m["p"], m["v"], m["at"]))
        elif m := _CS_TRACE.match(line):
            traces.append((m["node"], m["caller"], m["line"], " ".join(_values(m["vals"]))))
        elif m := _CS_SUMMARY.match(line):
            title = m["rest"]
        elif not t.take(line):
            raise _Skip
    if t.has_dump or t.walk_only() or t.scc or t.trace or len(t.headers) > 1:
        raise _Skip
    if traces:  # contexts: the edges are the trace lines, whatever else the text says
        cg = _Cg()
        for node, caller, ln, passed in traces:
            cg.edge(caller, node, f"@L{ln}", passed)
        display, lines, node_tip, node_cls = {}, {}, {}, {}
        for n in cg.nodes:
            display[n] = _cs_fn(n)
            lines[n] = _context_lines(_CS_SPLIT.match(n)["string"], ctx[n][1] if n in ctx else [])
            node_tip[n] = [f"k={ctx[n][0]}"] if n in ctx else []
        for node, p, v, at in warns:
            if node in cg.nodes:
                node_cls.setdefault(node, []).append("hl")
                node_tip.setdefault(node, []).append(f"divides by {p}={v} at {at}")
        # the call site is the last element of the callee's call string: the edge says what the call passes, the site stays in the tooltip
        return _plain_draw(cg, "callstrings", title, node_cls=node_cls, node_tip=node_tip, node_lines=lines, display=display,
                           label_edge=lambda e: False)
    cg = t.cg
    if not cg.edges:
        return None
    node_tip, node_cls = {}, {}
    for n, (k, vals) in ctx.items():
        fn, string = _cs_fn(n), _CS_SPLIT.match(n)["string"]
        if fn in cg.nodes:
            node_tip.setdefault(fn, []).append(f"k={k} [{string}]: {' '.join(vals)}")
    for node, p, v, at in warns:
        fn = _cs_fn(node)
        if fn in cg.nodes:
            node_cls.setdefault(fn, []).append("hl")
            node_tip.setdefault(fn, []).append(f"{node} divides by {p}={v} at {at}")
    return _plain_draw(cg, "callstrings_edges", title, node_cls=node_cls, node_tip=node_tip)


_FARM = re.compile(rf"^farm (?P<name>{_NAME}): synthesized=(?P<yes>yes|no)(?: .*)?$")


@_parser
def _farm(text: str, cmd: str) -> str | None:
    """p10_farm: the CFG of each `synthesized=yes` section (everything from its first block header on), drawn by cfg.py; the
    header and the pretty-printed body are cut out first; `synthesized=no` only, or a body with no CFG: None. A line outside a
    section (a compiler remark merged into the output) is not part of any CFG: it is left alone."""
    sections: list[tuple[str, list[str]]] = []
    cur: list[str] | None = None
    for line in text.splitlines():
        if m := _FARM.match(line):
            cur = [] if m["yes"] == "yes" else None
            if cur is not None:
                sections.append((_name(m["name"]), cur))
        elif cur is not None:
            cur.append(line.rstrip())
    parts: list[str] = []
    for name, body in sections:
        first = next((i for i, ln in enumerate(body) if _cfg.BLOCK_HDR.match(ln)), None)
        if first is not None:
            parts += [name, *body[first:]]
    dot = _cfg.parse_dumpcfg("\n".join(parts)) if parts else None
    # cfg.py names the graph after the first `name (` in the title: `void` for a call_once<void (&)(int &)>; the download is named for the farm
    return re.sub(r"^digraph \w+", "digraph " + _gname("farm", sections[0][0]), dot, count=1) if dot else None


# --------------------------------------------------------------------------
# p11_index
# --------------------------------------------------------------------------
_IDX_HDR = re.compile(r"^== (?P<file>.+?): \d+ call occurrences, \d+ references$")
_IDX_DIFF = re.compile(r"^diff: only-index=\d+ only-graph=\d+ both=\d+$")
_IDX_WORDS = ("both", "only-index", "only-graph")
_POINTER = re.compile(r"@(?:param|var)$")  # the variable a call through a pointer names: f@param


@_parser
def _index(text: str, cmd: str) -> str | None:
    """p11_index: `edge` lines (a `Dyn` role is a virtual edge, a `<name>@param` / `<name>@var` callee a dim node behind an
    indirect edge), `ref` lines weak, with `--diff` the word as in p08_mine (only-index hl, only-graph weak); `diff:` alone: None."""
    cg = _Cg()
    words: dict[tuple[str, str], set[str]] = {}
    pointers: set[str] = set()
    virtual: set[tuple[str, str]] = set()
    refs: set[tuple[str, str]] = set()
    title = ""
    for line in _lines(text):
        if m := _IDX_HDR.match(line):
            title = m["file"]
        elif _IDX_DIFF.match(line):
            continue
        elif line.startswith("edge "):
            toks = _tokens(line)
            a, b = _edge_head(toks)
            site, roles, word = _cmp_edge(toks, _IDX_WORDS)
            dyn = "Dyn" in roles.split(",")
            cg.edge(a, b, site, "virtual" if dyn else "", extra=f"roles={roles}" if roles else "")
            words.setdefault((a, b), set()).add(word)
            if dyn:
                virtual.add((a, b))
            if _POINTER.search(b):
                pointers.add(b)
        elif line.startswith("ref "):
            toks = _tokens(line)  # ref <container> -> <fn> @L<line>
            if len(toks) != 5 or toks[2] != "->" or not _SITE_AT.match(toks[4]):
                raise _Skip
            cg.edge(toks[1], toks[3], toks[4], "ref", relation=True)  # an address taken: not a call, so not recursion
            refs.add((toks[1], toks[3]))
        else:
            raise _Skip
    if not cg.edges:
        return None
    edge_cls = _cmp_edge_cls(words, "only-index", "only-graph", pointers)
    for key in refs:
        edge_cls.setdefault(key, []).append("weak")
    for key in virtual:
        edge_cls.setdefault(key, []).insert(0, "virtual")
    return _plain_draw(cg, "index", title, node_cls={n: ["dim"] for n in pointers}, edge_cls=edge_cls)


# --------------------------------------------------------------------------
# clang-tidy misc-no-recursion
# --------------------------------------------------------------------------
_TIDY_FRAME = re.compile(r"^(?P<loc>\S+?):(?P<line>\d+):\d+: note: Frame #(?P<n>\d+): function '(?P<a>.+?)' calls function '(?P<b>.+?)' here:?$")
_TIDY_CHAIN = re.compile(r"^(?P<tag>\w+): +(?P<chain>\S+(?: -> \S+)+)$")  # `tidy:  pang -> ping -> pong -> pang`, `lab:   ...`: a chain a doc's pipeline printed


@_parser
def _tidy(text: str, cmd: str) -> str | None:
    """clang-tidy `misc-no-recursion`: the `Frame #n` notes of each warning are one cycle (the last frame calls the first function:
    the edge back), each edge labelled with the line of its call; a chain a pipeline printed (`tidy:  a -> b -> a`) is drawn too,
    its edges labelled with the tags of the chains that have them (`tidy, lab`); every other line is clang-tidy's and is left
    alone; None without a Frame or a chain."""
    cg = _Cg()
    back: set[tuple[str, str]] = set()
    chain: tuple[str, str] | None = None
    tags: dict[tuple[str, str], list[str]] = {}
    for line in text.splitlines():
        if m := _TIDY_FRAME.match(line.strip()):
            if int(m["n"]) == 1:  # a new chain: the closing edge of the one before is settled
                if chain:
                    back.add(chain)
                chain = None
            cg.edge(m["a"], m["b"], f"@L{m['line']}")
            chain = (m["a"], m["b"])
        elif m := _TIDY_CHAIN.match(line.strip()):
            names = m["chain"].split(" -> ")
            for key in zip(names, names[1:]):
                if m["tag"] not in tags.setdefault(key, []):
                    tags[key].append(m["tag"])
    if chain:
        back.add(chain)
    for (a, b), tg in tags.items():
        cg.edge(a, b, kind=", ".join(tg))
    if not cg.edges:
        return None
    return _plain_draw(cg, "no_recursion", "", edge_cls={k: ["back"] for k in back})


PARSERS: list[tuple[re.Pattern, Callable[[str, str], str | None]]] = [
    (re.compile(r"debug\.DumpCallGraph"), _parser(_dump)),
    (_tool("p08_nodes", "p09_walk"), _nodes),
    (_tool("p08_build"), _build),
    (_tool("p08_anycall"), _anycall),
    (_tool("p08_mine"), _mine),
    (_tool("p09_metrics"), _metrics),
    (_tool("p10_summary"), _summary),
    (_tool("p10_sites"), _sites),
    (_tool("p10_callstrings"), _callstrings),
    (_tool("p10_farm"), _farm),
    (_tool("p11_resolve"), _resolve),
    (_tool("p11_index"), _index),
    (_tool("p11_xtu"), _xtu),
    (_tool("p11_check"), _check),
    (re.compile(r"clang-tidy\b.*misc-no-recursion", re.S), _tidy),
]
