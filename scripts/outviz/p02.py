"""Graphviz diagrams for the output of the Part 2 tools.

PARSERS is a list of (regex, parser).  The site build `.search`es the regex in
the bash command that produced an output block and calls
`parser(output_text, command_text)`, which returns a complete `digraph` source
or None ("nothing worth drawing, or it cannot be drawn faithfully").  Only the
text the tool printed is drawn: no block, edge or label is invented.

  p02_edges        successor listing    -> the CFG, one cluster per function/option set
  p02_terminators  terminator listing   -> terminator blocks and their roled successors
  p02_stmtmap      stmt -> block map    -> statements linked to the block that owns them
  p02_forced       forced stmt -> block -> the same, for the forced elements
  p02_exercises    return-in-loop       -> loop / return nesting
  p02_export       --format=json|dot    -> the exported CFG

Deliberately not covered (nothing to draw):
  p02_skeleton, p02_walk   counts, or elements with no edges; `-dump` is the
                           standard CFG::dump text that cfg.py draws
  p02_exercises cyclomatic / loops, p02_observer   one summary line per function
  scripts/optdiff.sh       a `diff -U0` of two dumps: the unchanged lines (and so
                           both graphs) are missing

Commands that also run a tool of another part (p03_ .. p07_) are left to that
part's module, and a listing cut by `| head|tail|sed` or a `grep` is not drawn
unless the rows are independent facts (stmtmap, forced) or the whole graph is
checkable (edges: block count; export: the ` -> ` grep).

The renderer owns colours and fonts; meaning goes only in `class=`.
Standard library only.
"""
from __future__ import annotations

import json
import re
from collections import Counter, OrderedDict, defaultdict
from dataclasses import dataclass, field
from typing import Callable

MAX_LINE = 50  # longer node-label lines are cut with an ellipsis; the full text goes in tooltip=

# ---------------------------------------------------------------- dot helpers


def q(s: str) -> str:
    """A dot string literal; a newline becomes dot's own \\n."""
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def ident(s: str) -> str:
    return re.sub(r"\W+", "_", s).strip("_") or "g"


def clip(s: str, n: int = MAX_LINE) -> str:
    return s if len(s) <= n else s[: n - 1].rstrip() + "…"


def wrap(s: str, width: int = 24) -> str:
    """Break a long edge label into lines (greedy, on spaces)."""
    out, cur = [], ""
    for w in s.split():
        if cur and len(cur) + 1 + len(w) > width:
            out.append(cur)
            cur = w
        else:
            cur = f"{cur} {w}".strip()
    return "\n".join(out + [cur])


def node(name: str, lines: list[str], classes=()) -> str:
    """One node.  `lines[0]` is the title; with more lines they are all left-justified (`\\l`)."""
    if len(lines) == 1:
        label = q(clip(lines[0]))
    else:
        label = '"' + "".join(q(clip(x))[1:-1] + "\\l" for x in lines) + '"'
    attrs = [f"label={label}"]
    if any(len(x) > MAX_LINE for x in lines):
        attrs.append(f"tooltip={q(chr(10).join(lines))}")
    cls = " ".join(dict.fromkeys(c for c in classes if c and c != "block"))
    if cls:
        attrs.append(f'class="{cls}"')
    return f"{name} [{', '.join(attrs)}];"


def edge(a: str, b: str, label: str = "", classes=()) -> str:
    attrs = []
    if label:
        attrs.append(f"label={q(label)}")
    cls = " ".join(dict.fromkeys(c for c in classes if c))
    if cls:
        attrs.append(f'class="{cls}"')
    return f"{a} -> {b}" + (f" [{', '.join(attrs)}]" if attrs else "") + ";"


Group = tuple["str | None", "list[str]"]  # (cluster label or None, dot statements)
Ends = tuple["list[str]", str]  # (top nodes of a cluster, its deepest node)


def _ends(pairs: list[tuple[str, str]], names: list[str]) -> Ends:
    """Where a cluster is chained into a vertical stack: the nodes nothing points to (its top) and
    the deepest node below them (longest path, back edges ignored)."""
    succ: dict[str, list[str]] = defaultdict(list)
    has_in = {b for _, b in pairs}
    for a, b in pairs:
        succ[a].append(b)
    if not names:
        return [], ""
    tops = [n for n in names if n not in has_in] or names[:1]
    state: set[str] = set()
    post: list[str] = []
    for t in tops:
        if t in state:
            continue
        state.add(t)
        stack = [(t, iter(succ[t]))]
        while stack:
            n, it = stack[-1]
            for m in it:
                if m not in state:
                    state.add(m)
                    stack.append((m, iter(succ[m])))
                    break
            else:
                post.append(n)
                stack.pop()
    rank = {n: i for i, n in enumerate(reversed(post))}
    depth = dict.fromkeys(rank, 0)
    for n in reversed(post):
        for m in succ[n]:
            if rank[m] > rank[n]:
                depth[m] = max(depth[m], depth[n] + 1)
    return tops, max(rank, key=lambda n: (depth[n], -rank[n]))


def digraph(name: str, groups: list[Group], attrs: tuple[str, ...] = ("rankdir=TB", "ordering=out"),
            ends: "list[Ends] | None" = None) -> str:
    """One graph; several groups (or a labelled one) become clusters.  With `ends` (one per group)
    the clusters are stacked one below the other, as cfg.py does: an invisible heavy edge from the
    deepest node of a cluster to the top of the next.  In a left-to-right layout dot stacks clusters
    bottom-up, so they are written last-first to read in listing order."""
    stacked = bool(ends) and len(ends) > 1
    if stacked:  # Graphviz 15.1 asserts (streq) on ordering=out together with invisible inter-cluster edges
        attrs = tuple(a for a in attrs if not a.startswith("ordering"))
    out = [f"digraph {ident(name)[:60]} {{"] + [f"  {a};" for a in attrs]
    if len(groups) == 1 and groups[0][0] is None:
        out += [f"  {s}" for s in groups[0][1]]
    else:
        numbered = list(enumerate(groups, 1))
        for k, (label, body) in (reversed(numbered) if "rankdir=LR" in attrs else numbered):
            out.append(f"  subgraph cluster_{k} {{")
            if label:
                out.append(f"    label={q(label)};")
            out += [f"    {s}" for s in body]
            out.append("  }")
        if stacked:
            out += [f"  {last} -> {top} [style=invis, weight=100];"
                    for (_, last), (tops, _) in zip(ends, ends[1:]) for top in tops]
    out.append("}")
    return "\n".join(out) + "\n"


# ------------------------------------------------------------ command helpers

_TOOL = re.compile(r"\bp0\d_[a-z]+(?![\w.])")  # a tool name, not a manifests/p02_x.cpp file
_FLAT = re.compile(r"\\\n")


def _flat(cmd: str) -> str:
    return _FLAT.sub(" ", cmd)


def _foreign(cmd: str) -> bool:
    """Does the command also run a tool of another part?  Then that part's module decides."""
    return any(not t.startswith("p02_") for t in _TOOL.findall(cmd))


def _invocations(cmd: str, tool: str) -> list[list[str]]:
    """The option words (`--x`, before any bare `--`, minus --func) of each run of `tool`."""
    runs = []
    for m in re.finditer(rf"\b{tool}(?![\w.])([^\n;|&]*)", _flat(cmd)):
        words = []
        for w in m.group(1).split():
            if w == "--":
                break
            if w.startswith("--") and not w.startswith("--func="):
                words.append(w)
        runs.append(words)
    return runs


def _safe(fn: Callable[[str, str], "str | None"]) -> Callable[[str, str], "str | None"]:
    """An output we did not foresee is 'nothing to draw', not a failed site build."""

    def run(output: str, cmd: str) -> "str | None":
        try:
            return fn(output, cmd)
        except (ValueError, KeyError, IndexError, TypeError, AttributeError):
            return None

    run.__doc__ = fn.__doc__
    run.__name__ = fn.__name__
    return run


# ----------------------------------------------------------------- CFG model


@dataclass
class Blk:
    id: int
    lines: list[str] = field(default_factory=list)  # label lines under the "B3" title
    classes: list[str] = field(default_factory=list)


@dataclass
class Edge:
    src: int
    dst: "int | None"  # None: the tool printed no target (null)
    label: str = ""
    classes: list[str] = field(default_factory=list)
    live: bool = True  # False: pruned / unreachable; the DFS does not follow it


@dataclass
class Cfg:
    blocks: "dict[int, Blk]"
    edges: list[Edge]
    entry: "int | None" = None
    exit: "int | None" = None
    root: "int | None" = None  # DFS start when no ENTRY is named; not drawn as one


# Terminators with a true branch (successor 0) and a false branch (successor 1).
_TWO_WAY = {
    "IfStmt", "WhileStmt", "ForStmt", "DoStmt", "CXXForRangeStmt", "ObjCForCollectionStmt",
    "ConditionalOperator", "BinaryConditionalOperator", "BinaryOperator",
}
_TERMINATORS = sorted(_TWO_WAY | {
    "SwitchStmt", "CXXTryStmt", "GotoStmt", "IndirectGotoStmt", "BreakStmt", "ContinueStmt",
    "CoreturnStmt", "SEHTryStmt",
})


def _canon(t: str) -> str:
    """p02_edges cuts the terminator class to 14 characters; restore the full name."""
    if t in _TERMINATORS or len(t) < 14:
        return t
    return next((c for c in _TERMINATORS if c.startswith(t)), t)


def _roles(term: str, n: int) -> list[tuple[str, list[str]]]:
    """(label, classes) of each of the n successor edges of a block ending in `term`."""
    if term == "CXXTryStmt" and n == 2:
        return [("handler", ["eh"]), ("unwind", ["eh"])]
    if term in _TWO_WAY and n == 2:
        return [("T", ["t"]), ("F", ["f"])]
    if n > 1:
        return [(f"succ{i}", []) for i in range(n)]
    return [("", []) for _ in range(n)]


def emit_cfg(g: Cfg, pre: str = "") -> "tuple[list[str], Ends]":
    """Nodes then edges of a CFG.  Entry first, then DFS pre-order, so dot cuts the same
    cycles that the DFS back-edge test marks `back`."""
    out_edges: dict[int, list[int]] = defaultdict(list)
    for i, e in enumerate(g.edges):
        out_edges[e.src].append(i)

    order: list[int] = []
    back: set[int] = set()
    state: dict[int, int] = {}  # 1 on the DFS stack, 2 finished
    start = g.entry if g.entry is not None else g.root
    if start in g.blocks:
        state[start] = 1
        order.append(start)
        stack = [(start, iter(out_edges[start]))]
        while stack:
            n, it = stack[-1]
            for ei in it:
                e = g.edges[ei]
                if not e.live or e.dst is None or e.dst not in g.blocks:
                    continue
                if state.get(e.dst) == 1:
                    back.add(ei)
                elif e.dst not in state:
                    state[e.dst] = 1
                    order.append(e.dst)
                    stack.append((e.dst, iter(out_edges[e.dst])))
                    break
            else:
                state[n] = 2
                stack.pop()
    rest = sorted(set(g.blocks) - set(order), reverse=True)
    order += rest

    out = []
    for i in order:
        b = g.blocks[i]
        classes = list(b.classes)
        title = f"B{i}"
        if i == g.entry:
            title += " (ENTRY)"
            classes.append("entry")
        elif i == g.exit:
            title += " (EXIT)"
            classes.append("exit")
        if g.entry in g.blocks and i not in state:
            classes.append("dim")  # not reachable from ENTRY
        out.append(node(f"{pre}B{i}", [title] + b.lines, classes))
    null_nodes, wires, pairs = [], [], []
    for i in order:
        for ei in out_edges[i]:
            e = g.edges[ei]
            classes = e.classes + (["back"] if ei in back else [])
            if e.dst is None:
                dst = f"{pre}null{len(null_nodes) + 1}"
                null_nodes.append(node(dst, ["null"], ["dim"]))
            else:
                dst = f"{pre}B{e.dst}"
            wires.append(edge(f"{pre}B{i}", dst, e.label, classes))
            pairs.append((f"{pre}B{i}", dst))
    names = [f"{pre}B{i}" for i in order] + [f"{pre}null{k}" for k in range(1, len(null_nodes) + 1)]
    return out + null_nodes + wires, _ends(pairs, names)


# --------------------------------------------------------------- p02_edges
#
#   == pruned_if: blocks=5 isLinear=1 edges=4 (+1 pruned)
#   B3   T=IfStmt         succs: (unreach B2) B1   preds: B4

_EDGE_HDR = re.compile(r"^== (\S+): blocks=(\d+) isLinear=\d+ edges=(\d+) \(\+(\d+) pruned\)\s*$")
_EDGE_ROW = re.compile(r"^B(\d+)\s+T=(\S*)\s*succs:(.*?)\s*preds:(.*?)\s*$")
_ADJ = re.compile(r"\(unreach B(\d+)\)|B(\d+)|\b(?:NULL|null)\b")


def _adjacent(text: str) -> list[tuple[str, "int | None"]]:
    """'r' reachable, 'u' (unreach Bn), 'n' null."""
    out = []
    for m in _ADJ.finditer(text):
        out.append(("u", int(m[1])) if m[1] else ("r", int(m[2])) if m[2] else ("n", None))
    return out


def _edge_sections(output: str) -> list[dict]:
    secs, cur = [], None
    for line in output.splitlines():
        m = _EDGE_HDR.match(line)
        if m:
            cur = {"fn": m[1], "blocks": int(m[2]), "edges": int(m[3]), "pruned": int(m[4]), "rows": {}, "bad": False}
            secs.append(cur)
            continue
        m = _EDGE_ROW.match(line)
        if m and cur is not None:
            if int(m[1]) in cur["rows"]:
                cur["bad"] = True  # rows of a second, header-less listing
            cur["rows"][int(m[1])] = (m[2], _adjacent(m[3]), _adjacent(m[4]))
    # a grep-ed or truncated listing has fewer rows than the header announces
    return [s for s in secs if not s["bad"] and len(s["rows"]) == s["blocks"] > 1]


def _edges_cfg(sec: dict) -> Cfg:
    rows = sec["rows"]
    blocks = {i: Blk(i) for i in rows}
    edges: list[Edge] = []
    for i, (t, succs, _preds) in rows.items():
        term = "" if t == "-" else _canon(t)
        if term:
            blocks[i].lines.append(f"T: {term}")
            if len(succs) >= 2:
                blocks[i].classes.append("cond")
        for (kind, dst), (label, classes) in zip(succs, _roles(term, len(succs))):
            if dst is not None and dst not in rows:
                raise ValueError("successor outside the listing")
            if kind == "u":
                label = f"{label} (unreach)" if label else "unreach"
            if kind != "r":
                classes = classes + ["weak"]
            edges.append(Edge(i, dst, label, classes, live=kind == "r"))
    # A noreturn block records the pruned edge only on the predecessor side of its dead successor.
    listed = {(e.src, e.dst) for e in edges if not e.live}
    for i, (_t, _s, preds) in rows.items():
        for kind, src in preds:
            if kind == "u" and src in rows and (src, i) not in listed:
                edges.append(Edge(src, i, "unreach", ["weak"], live=False))
    top = max(rows)
    return Cfg(blocks, edges, entry=top if not rows[top][2] else None, exit=0 if 0 in rows and not rows[0][1] else None)


def _mark_changes(a: Cfg, b: Cfg) -> None:
    """Two listings of one function (e.g. with and without an option): mark the edges that differ."""
    key = lambda e: (e.src, e.dst, e.live)  # noqa: E731
    ca, cb = Counter(map(key, a.edges)), Counter(map(key, b.edges))
    for g, other in ((a, cb), (b, ca)):
        seen: Counter = Counter()
        for e in g.edges:
            seen[key(e)] += 1
            if seen[key(e)] > other[key(e)]:
                e.classes.append("hl")


@_safe
def edges(output: str, cmd: str) -> "str | None":
    if _foreign(cmd):
        return None
    secs = _edge_sections(output)
    if not secs:
        return None
    cfgs = [_edges_cfg(s) for s in secs]
    name = "edges_" + "_".join(OrderedDict.fromkeys(s["fn"] for s in secs))
    if len(secs) == 1:
        return digraph(name, [(None, emit_cfg(cfgs[0])[0])])
    compare = len(secs) == 2 and secs[0]["fn"] == secs[1]["fn"]  # one function, two option sets
    if compare:
        _mark_changes(*cfgs)
    runs = _invocations(cmd, "p02_edges")
    groups: list[Group] = []
    ends: list[Ends] = []
    for k, (s, g) in enumerate(zip(secs, cfgs), 1):
        opts = " ".join(runs[k - 1]) if len(runs) == len(secs) and runs[k - 1] else ""
        stats = f"edges={s['edges']}" + (f" (+{s['pruned']} pruned)" if s["pruned"] else "")
        head = s["fn"] + (f"  {opts}" if opts else "")
        body, e = emit_cfg(g, f"c{k}_")
        groups.append((f"{head}\n{stats}", body))
        ends.append(e)
    # a comparison reads best side by side; different functions are stacked
    return digraph(name, groups, ends=None if compare else ends)


# --------------------------------------------------------- p02_terminators
#
#   == chain
#   B3  StmtBranch IfStmt  line 7
#       T:      if a && b && c
#       cond:   BinaryOperator  `a && b && c`
#       last:   ImplicitCastExpr  `c`
#       succ0:  B2  then
#       succ1:  B1  else

_T_HDR = re.compile(r"^== (\S+)\s*$")
_T_BLK = re.compile(r"^B(\d+)(?:\s+(StmtBranch|TemporaryDtorsBranch|VirtualBaseBranch)(?:\s+(.*?))?)?\s*$")
_T_DET = re.compile(r"^ {4}(T|cond|last|succ\d+|label|loopTarget|noreturn element|inevitably sinking):\s*(.*?)\s*$")
_T_DESC = re.compile(r"^(\(no statement\)|\S+)(?:\s+line (\d+))?$")
_TICKS = re.compile(r"`(.*)`")


@dataclass
class TBlk:
    id: int
    kind: str = ""
    cls: str = ""
    line: "str | None" = None
    term: "str | None" = None
    cond: "str | None" = None
    last: "str | None" = None
    succs: list = field(default_factory=list)  # (block id | None, role)
    label: "str | None" = None
    loop_target: "str | None" = None
    noreturn: "int | None" = None
    sinking: bool = False


def _term_sections(output: str) -> "list[tuple[str, list[TBlk]]] | None":
    """None when a detail line has no block above it (a grep-ed listing)."""
    secs: list[tuple[str, list[TBlk]]] = []
    cur: "list[TBlk] | None" = None
    blk: "TBlk | None" = None
    for line in output.splitlines():
        if not line.strip():
            continue
        if m := _T_HDR.match(line):
            cur, blk = [], None
            secs.append((m[1], cur))
        elif (m := _T_BLK.match(line)) and cur is not None:
            blk = TBlk(int(m[1]), kind=m[2] or "")
            if m[3] and (d := _T_DESC.match(m[3])):
                blk.cls = "" if d[1].startswith("(") else d[1]
                blk.line = d[2]
            cur.append(blk)
        elif m := _T_DET.match(line):
            if blk is None:
                return None
            key, val = m[1], m[2]
            if key == "T":
                blk.term = val
            elif key in ("cond", "last"):
                t = _TICKS.search(val)
                setattr(blk, key, t[1] if t else val)
            elif key.startswith("succ"):
                s = re.match(r"^(?:(\(pruned\))|B(\d+)(?:\s+(.*))?)$", val)
                blk.succs.append((None, "pruned") if s[1] else (int(s[2]), s[3] or ""))
            elif key == "label":
                blk.label = val
            elif key == "loopTarget":
                blk.loop_target = val
            elif key == "noreturn element":
                n = re.search(r"single successor: B(\d+)", val)
                blk.noreturn = int(n[1]) if n else None
            else:
                blk.sinking = True
        else:
            cur, blk = None, None  # another tool's line ends the section
    return secs


def _term_group(blocks: list[TBlk], pre: str) -> "tuple[list[str], Ends, int]":
    """Nodes + edges of one function, where to stack it, and the number of edges."""
    listed = {b.id for b in blocks}
    referenced: "OrderedDict[int, None]" = OrderedDict()  # named by a successor line, not listed itself
    nodes: list[str] = []
    wires: list[str] = []
    pairs: list[tuple[str, str]] = []
    pruned = 0
    for b in blocks:
        n = len(b.succs)
        two_way = n == 2 and (b.kind != "StmtBranch" or b.cls in _TWO_WAY)
        for k, (dst, role) in enumerate(b.succs):
            role = "" if role == "pruned" else role
            if b.cls == "CXXTryStmt" and n == 2:
                label, classes = role, ["eh"]
            elif two_way:
                label, classes = ("T" if k == 0 else "F") + (f": {role}" if role else ""), ["t" if k == 0 else "f"]
            elif n > 1:
                label, classes = f"succ{k}", []
            else:
                label, classes = role, []
            if dst is None:  # "(pruned)": the tool printed no block
                pruned += 1
                nodes.append(node(f"{pre}pruned{pruned}", ["(pruned)"], ["dim"]))
                wires.append(edge(f"{pre}B{b.id}", f"{pre}pruned{pruned}", wrap(label), classes + ["weak"]))
                pairs.append((f"{pre}B{b.id}", f"{pre}pruned{pruned}"))
            else:
                if dst not in listed:
                    referenced.setdefault(dst)
                wires.append(edge(f"{pre}B{b.id}", f"{pre}B{dst}", wrap(label), classes))
                pairs.append((f"{pre}B{b.id}", f"{pre}B{dst}"))
        if b.noreturn is not None:
            if b.noreturn not in listed:
                referenced.setdefault(b.noreturn)
            wires.append(edge(f"{pre}B{b.id}", f"{pre}B{b.noreturn}"))
            pairs.append((f"{pre}B{b.id}", f"{pre}B{b.noreturn}"))

    for b in blocks:
        lines = [f"B{b.id}" + (" (EXIT)" if b.id == 0 else "")]
        if b.kind == "StmtBranch":
            lines.append(f"{b.cls} (line {b.line})" if b.line else b.cls)
        elif b.kind:
            lines.append(f"{b.kind} {b.cls}".strip())
        for key, val in (("T", b.term), ("cond", b.cond), ("last", b.last), ("label", b.label), ("loopTarget", b.loop_target)):
            if val is not None:
                lines.append(f"{key}: {val}")
        if b.noreturn is not None:
            lines.append("noreturn element")
        if b.sinking:
            lines.append("inevitably sinking")
        # a terminator with two or more successors branches; goto/break/continue only lead on
        classes = ["cond"] if b.kind and len(b.succs) >= 2 else ["hl"] if b.kind else []
        nodes.append(node(f"{pre}B{b.id}", lines, classes + (["exit"] if b.id == 0 else [])))
    for i in referenced:
        nodes.append(node(f"{pre}B{i}", [f"B{i}" + (" (EXIT)" if i == 0 else "")], ["exit"] if i == 0 else []))
    names = [f"{pre}B{i}" for i in [*(b.id for b in blocks), *referenced]] + [f"{pre}pruned{k}" for k in range(1, pruned + 1)]
    return nodes + wires, _ends(pairs, names), len(wires)


@_safe
def terminators(output: str, cmd: str) -> "str | None":
    if _foreign(cmd) or re.search(r"\|\s*(head|tail|sed|awk|cut)\b", _flat(cmd)):
        return None
    secs = _term_sections(output)
    if not secs:
        return None
    groups: list[Group] = []
    ends: list[Ends] = []
    for k, (fn, blocks) in enumerate(secs, 1):
        body, e, n = _term_group(blocks, f"c{k}_" if len(secs) > 1 else "")
        if n:  # a function with no successor lines has nothing to connect
            groups.append((fn, body))
            ends.append(e)
    if not groups:
        return None
    name = "terminators_" + "_".join(ident(fn) for fn, _ in groups)
    if len(groups) == 1:
        return digraph(name, [(None, groups[0][1])])
    return digraph(name, groups, ends=ends)


# -------------------------------------------------- p02_stmtmap / p02_forced
#
#   == chain
#     line 7   IfStmt                 if (a && b && c) return 1              -> B3    terminator
#
#   == sum: DeclRefExpr nodes=6  elements default=7 forced=13
#     line 13  `i` -> B4

_MAP_HDR = re.compile(r"^== (\S+)(?:\s+\((AnalysisDeclContext)\))?\s*$")
_MAP_ROW = re.compile(r"^\s+line\s+(\d+)\s+(\w+)(?:\s+(.*?))?\s+->\s+(B\d+|null)\s+(none|element|terminator|label|ancestor)\s*$")
_FORCED_HDR = re.compile(r"^== (\S+): (\w+) nodes=(\d+)\s+elements default=(\d+) forced=(\d+)\s*$")
_FORCED_ROW = re.compile(r"^\s+line (\d+)\s+`(.*)`\s+->\s+(B\d+|null)\s*$")
_BRANCH_STMTS = _TWO_WAY | {"SwitchStmt", "CXXTryStmt", "IndirectGotoStmt"}


def _bipartite(rows: list[dict], pre: str, clustered: bool) -> list[str]:
    """Statements in one column, the blocks that own them in the next, an edge per row.

    The statements are grouped by block (latest block first, i.e. in execution order) so that
    the edges do not cross; `rank=same` + invisible edges pin the column order (dot lays a flat
    chain out bottom-up inside a cluster, hence `clustered`)."""
    def block_key(r):  # B5 before B4 ...; no block last
        return (1, 0) if r["block"] is None else (0, -r["block"])

    ordered = sorted(enumerate(rows), key=lambda p: (block_key(p[1]), p[0]))
    blocks: "OrderedDict[int | None, None]" = OrderedDict((r["block"], None) for _, r in ordered)
    branch = {r["block"] for r in rows if r["why"] == "terminator" and r["cls"] in _BRANCH_STMTS}
    out = []
    names = []
    for _, r in ordered:
        name = f"{pre}s{r['n']}"
        names.append(name)
        out.append(node(name, r["lines"], ["api"]))
    bnames = []
    for b in blocks:
        name = f"{pre}B{b}" if b is not None else f"{pre}null"
        bnames.append(name)
        out.append(node(name, [f"B{b}" if b is not None else "null"], ["dim"] if b is None else ["cond"] if b in branch else []))
    for _, r in ordered:
        why = r["why"]
        classes = ["hl"] if why == "terminator" else ["weak"] if why in ("ancestor", "none") else []
        dst = f"{pre}B{r['block']}" if r["block"] is not None else f"{pre}null"
        out.append(edge(f"{pre}s{r['n']}", dst, r["label"], classes))
    for col in (names, bnames):
        if len(col) > 1:
            out.append("{ rank=same; " + " -> ".join(col[::-1] if clustered else col) + " [style=invis]; }")
    return out


def _shares_a_block(rows: list[dict]) -> bool:
    """A mapping is worth a picture when some block owns several statements; one block per
    statement is just a list of pairs."""
    return any(n > 1 for n in Counter(r["block"] for r in rows).values())


def _stmt_lines(line: str, cls: str, text: str) -> list[str]:
    return [f"line {line}: {cls}"] + ([text] if text else [])


@_safe
def stmtmap(output: str, cmd: str) -> "str | None":
    if _foreign(cmd):
        return None
    secs: "list[tuple[str, list[dict]]]" = []
    cur = None
    for line in output.splitlines():
        if m := _MAP_HDR.match(line):
            cur = []
            secs.append((m[1] + (" (AnalysisDeclContext)" if m[2] else ""), cur))
        elif (m := _MAP_ROW.match(line)):
            if cur is None:  # a listing cut after its header (grep, head)
                cur = []
                secs.append(("", cur))
            blk = None if m[4] == "null" else int(m[4][1:])
            cur.append({"n": len(cur), "cls": m[2], "block": blk, "why": m[5], "label": m[5],
                        "lines": _stmt_lines(m[1], m[2], m[3] or "")})
    secs = [s for s in secs if s[1]]
    if not any(_shares_a_block(rows) for _, rows in secs):
        return None
    several = len(secs) > 1
    groups: list[Group] = [(fn or "(rows)" if several else None, _bipartite(rows, f"c{k}_", several))
                           for k, (fn, rows) in enumerate(secs, 1)]
    names = [ident(fn) for fn, _ in secs if fn]
    return digraph("stmtmap_" + "_".join(names) if names else "stmtmap", groups, ("rankdir=LR",))


@_safe
def forced(output: str, cmd: str) -> "str | None":
    if _foreign(cmd):
        return None
    secs, cur = [], None
    for line in output.splitlines():
        if m := _FORCED_HDR.match(line):
            cur = {"fn": m[1], "cls": m[2], "nodes": m[3], "default": m[4], "forced": m[5], "rows": []}
            secs.append(cur)
        elif (m := _FORCED_ROW.match(line)) and cur is not None:
            n = len(cur["rows"])
            cur["rows"].append({"n": n, "cls": cur["cls"], "block": None if m[3] == "null" else int(m[3][1:]), "why": "element", "label": "",
                                "lines": [f"{cur['cls']} {m[2]}", f"line {m[1]}"]})
    # every registered node is listed: a cut listing has fewer rows than `nodes=`
    secs = [s for s in secs if len(s["rows"]) == int(s["nodes"]) and _shares_a_block(s["rows"])]
    if not secs:
        return None
    groups = [(f"{s['fn']}: {s['cls']} nodes={s['nodes']}\nelements default={s['default']}, forced={s['forced']}",
               _bipartite(s["rows"], f"c{k}_", True)) for k, s in enumerate(secs, 1)]
    return digraph("forced_" + "_".join(ident(s["fn"]) for s in secs), groups, ("rankdir=LR",))


# ------------------------------------------------------------ p02_exercises
#
#   == nested
#     return at line 74 (in B4) is inside: ForStmt@line 72 (header B6) ForStmt@line 71 (header B8)

_EX_ROW = re.compile(r"^\s+return at line (\d+) \(in (B\d+|null)\) is inside:(.*)$")
_EX_LOOP = re.compile(r"(\w+)@line (\d+) \(header (B\d+|null)\)")


@_safe
def exercises(output: str, cmd: str) -> "str | None":
    if _foreign(cmd) or not re.search(r"--mode[= ]return-in-loop", cmd):
        return None
    secs: "list[tuple[str, list]]" = []
    for line in output.splitlines():
        if m := _T_HDR.match(line):
            secs.append((m[1], []))
        elif (m := _EX_ROW.match(line)) and secs:
            secs[-1][1].append((m[1], m[2], _EX_LOOP.findall(m[3])))
    groups: list[Group] = []
    ends: list[Ends] = []
    for k, (fn, rows) in enumerate(secs, 1):
        pre = f"c{k}_" if len(secs) > 1 else ""
        nodes: "OrderedDict[str, str]" = OrderedDict()
        edges: "OrderedDict[tuple[str, str], str]" = OrderedDict()
        for line, blk, loops in rows:
            ids = []
            for cls, ln, hdr in loops:  # innermost first, as printed
                name = f"{pre}L{ln}_{hdr}"
                nodes.setdefault(name, node(name, [f"{cls} (line {ln})", f"header {hdr}"], ["cond"]))
                ids.append(name)
            ret = f"{pre}R{line}_{blk}"
            nodes.setdefault(ret, node(ret, [f"ReturnStmt (line {line})", f"in {blk}"]))
            chain = list(reversed(ids)) + [ret]  # outermost loop ... innermost loop, return
            for a, b in zip(chain, chain[1:]):
                edges.setdefault((a, b), edge(a, b, "contains"))
        if edges:
            groups.append((fn, list(nodes.values()) + list(edges.values())))
            ends.append(_ends(list(edges), list(nodes)))
    if not groups:
        return None
    name = "return_in_loop_" + "_".join(ident(s[0]) for s in secs)
    if len(groups) == 1:
        return digraph(name, [(None, groups[0][1])])
    return digraph(name, groups, ends=ends)


# -------------------------------------------------------------- p02_export

_EDGE_GREP = re.compile(r"""\|\s*grep\s+(?:-E\s+)?(['"])\s*->\s*\1\s*$""")


def _export_filtered(cmd: str, allow_edge_grep: bool) -> bool:
    flat = _flat(cmd).strip()
    if allow_edge_grep and _EDGE_GREP.search(flat):
        flat = _EDGE_GREP.sub("", flat)
    return bool(re.search(r"\|\s*\w", flat.replace("||", "")))


def _unrecord(s: str) -> list[str]:
    """Split a record label on its `\\l` breaks and undo dotEscape's backslashes."""
    lines, cur, i = [], [], 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            if s[i + 1] == "l":
                lines.append("".join(cur))
                cur = []
            else:
                cur.append(s[i + 1])
            i += 2
        else:
            cur.append(c)
            i += 1
    if cur:
        lines.append("".join(cur))
    return lines


def _export_roles(label: str, live: bool) -> tuple[str, list[str]]:
    classes = {"T": ["t"], "F": ["f"], "handler": ["eh"], "unwind": ["eh"]}.get(label, [])
    return label, classes + ([] if live else ["weak"])


def _export_root(g: Cfg) -> None:
    """Edge-only listings name no ENTRY: a single block with no incoming edge starts the DFS."""
    if g.entry is None:
        incoming = {e.dst for e in g.edges}
        roots = [i for i in g.blocks if i not in incoming]
        g.root = roots[0] if len(roots) == 1 else None


def _dot_cfgs(output: str) -> "list[tuple[str, Cfg]]":
    graphs: "list[tuple[str, Cfg]]" = []
    cur: "Cfg | None" = None
    name = ""
    node_re = re.compile(r'^\s*B(\d+)\s*\[label="\{(.*)\}"(.*?)\];\s*$')
    edge_re = re.compile(r"^\s*B(\d+)\s*->\s*B(\d+)\s*(?:\[(.*)\])?;\s*$")
    for line in output.splitlines():
        if m := re.match(r'^digraph\s+"(.*)"\s*\{\s*$', line):
            name, cur = m[1], Cfg({}, [])
            graphs.append((name, cur))
        elif m := node_re.match(line):
            cur = cur or _anon(graphs)
            parts = _unrecord(m[2])
            i = int(m[1])
            blk = cur.blocks.setdefault(i, Blk(i))
            title = parts[0]
            if "(ENTRY)" in title:
                cur.entry = i
            elif "(EXIT)" in title:
                cur.exit = i
            if lab := re.match(r"^B\d+(?:\s*\(\w+\))?\s+(\w+)$", title):
                blk.lines.append(f"label: {lab[1]}")
            blk.lines += parts[1:]
            if "style=dashed" in m[3]:
                blk.lines.append("loopTarget")
        elif m := edge_re.match(line):
            cur = cur or _anon(graphs)
            attrs = m[3] or ""
            lab = re.search(r'label="([^"]*)"', attrs)
            label, classes = _export_roles(lab[1] if lab else "", "style=dashed" not in attrs)
            cur.edges.append(Edge(int(m[1]), int(m[2]), label, classes, live="style=dashed" not in attrs))
    for _, g in graphs:
        for e in g.edges:
            g.blocks.setdefault(e.src, Blk(e.src))
            g.blocks.setdefault(e.dst, Blk(e.dst))
        fan, roles = Counter(e.src for e in g.edges), defaultdict(set)
        for e in g.edges:
            roles[e.src].add(e.label)
        for i, b in g.blocks.items():
            # a terminator line, or (edge-only listings) a T/F edge, marks a block that branches
            if fan[i] >= 2 and (any(x.startswith("T: ") for x in b.lines) or roles[i] & {"T", "F"}):
                b.classes.append("cond")
        _export_root(g)
    return graphs


def _anon(graphs: "list[tuple[str, Cfg]]") -> Cfg:
    """Edge lines with no `digraph` line above them (a grep for ' -> ')."""
    g = Cfg({}, [])
    graphs.append(("cfg", g))
    return g


def _json_cfgs(output: str) -> "list[tuple[str, Cfg]]":
    dec, i, graphs = json.JSONDecoder(), 0, []
    while True:
        while i < len(output) and output[i].isspace():
            i += 1
        if i >= len(output):
            break
        doc, i = dec.raw_decode(output, i)
        blocks, edges = {}, []
        for b in doc["blocks"]:
            lines = []
            for k, el in enumerate(b["elements"]):
                what = f"{el['class']} {el['text']}" if "class" in el else f"{el['kind']} {el.get('detail', '')}"
                lines.append(f"{k}: {what}".rstrip())
            t = b.get("terminator")
            if t:
                lines.append(f"T: {t['text']}")
            if b.get("label"):
                lines.append(f"label: {b['label']}")
            if b.get("loopTarget"):
                lines.append(f"loopTarget: {b['loopTarget']}")
            if b.get("noreturn"):
                lines.append("noreturn")
            n = len(b["succs"])
            blocks[b["id"]] = Blk(b["id"], lines, ["cond"] if t and n >= 2 else [])
            for s in b["succs"]:
                label, classes = _export_roles(s.get("role", ""), s["reachable"])
                edges.append(Edge(b["id"], s.get("to"), label, classes, live=s["reachable"] and "to" in s))
        graphs.append((doc["function"], Cfg(blocks, edges, entry=doc["entry"], exit=doc["exit"])))
    return graphs


@_safe
def export(output: str, cmd: str) -> "str | None":
    if _foreign(cmd) or "--outdir" in cmd:
        return None
    text = output.strip()
    if "--format=json" in cmd or text.startswith("{"):
        if _export_filtered(cmd, False):
            return None
        graphs = _json_cfgs(text)  # a document cut by `head` raises JSONDecodeError (a ValueError): None
    else:
        if _export_filtered(cmd, True):
            return None
        graphs = _dot_cfgs(text)
    graphs = [(n, g) for n, g in graphs if len(g.blocks) > 1 and g.edges]
    if not graphs:
        return None
    name = "export_" + "_".join(ident(n) for n, _ in graphs)
    if len(graphs) == 1:
        return digraph(name, [(None, emit_cfg(graphs[0][1])[0])])
    emitted = [emit_cfg(g, f"c{k}_") for k, (_, g) in enumerate(graphs, 1)]
    return digraph(name, [(n, body) for (n, _), (body, _) in zip(graphs, emitted)], ends=[e for _, e in emitted])


# -------------------------------------------------------------------- registry


def _tool(name: str) -> re.Pattern:
    return re.compile(rf"\b{name}(?![\w.])")  # not manifests/{name}.cpp


PARSERS: "list[tuple[re.Pattern, Callable[[str, str], str | None]]]" = [
    (_tool("p02_edges"), edges),
    (_tool("p02_terminators"), terminators),
    (_tool("p02_stmtmap"), stmtmap),
    (_tool("p02_forced"), forced),
    (_tool("p02_exercises"), exercises),
    (_tool("p02_export"), export),
]
