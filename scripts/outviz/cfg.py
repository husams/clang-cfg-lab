"""Graphs from CFG text: `debug.DumpCFG` dumps, cfgshape.sh summaries, dominator trees, DOT output.

Everything funnels into one small model (Fn = a function's blocks, each with its
ordered successors) and one renderer, so every format gets the same drawing rules:

  * Succs[0] / Succs[1] of a block whose terminator is a conditional branch are the
    true / false edges (class t / f, labels T / F); a `switch` numbers its case edges;
    the edges out of a `try` dispatch are exception edges (eh).
  * DFS back edges from ENTRY are class back; `Bn(Unreachable)` and `NULL` successors are
    weak edges (NULL ends in a dim stub node).
  * ENTRY / EXIT blocks are class entry / exit, blocks that end in a branch are cond.
  * Only what the text shows is drawn. A block that is named as a successor but whose text is
    not part of the output becomes a dim stub; edges are never inferred, and nothing is drawn
    for an output that holds fewer than two blocks or no edge.
"""
import re
from dataclasses import dataclass, field

MAX_W = 42  # label lines longer than this are cut with an ellipsis (the full block text goes in the tooltip)
MAX_ELEMS = 10  # a block with more elements shows this many, then "+N more"

SUCC_TOKEN = re.compile(r"(B\d+)(\(Unreachable\))?|NULL|null")


@dataclass
class Blk:
    name: str
    flag: str = ""  # ENTRY / EXIT / NORETURN / INDIRECT GOTO DISPATCH
    label: str = ""  # "case 2:", "default:", "catch (int e):", "loop:"
    elems: list[str] = field(default_factory=list)  # "1: x" ...
    n_elems: int = 0  # element count when only the count is known (cfgshape)
    term: str | None = None
    succs: list[tuple[str, bool, str]] = field(default_factory=list)  # (target | "NULL", unreachable, explicit label)
    preds: list[tuple[str, bool]] = field(default_factory=list)
    seen_succs: bool = False


@dataclass
class Fn:
    title: str = ""
    blocks: dict[str, Blk] = field(default_factory=dict)
    content: bool = True  # False when the text only lists edges: no block is a "not shown" stub then


# --------------------------------------------------------------------------
# rendering
# --------------------------------------------------------------------------
def q(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def clip(s: str, w: int = MAX_W) -> str:
    s = s.replace("\t", " ")
    return s if len(s) <= w else s[: w - 1].rstrip() + "…"


def label_of(lines: list[tuple[str, str]]) -> str:
    """[(text, 'c' | 'l')] -> the inside of a dot label string: centered ('\\n') or left-justified ('\\l') lines."""
    if len(lines) == 1:
        return q(lines[0][0])[1:-1]
    return "".join(q(t)[1:-1] + ("\\n" if j == "c" else "\\l") for t, j in lines)


def term_kind(term: str) -> str:
    t = term.strip()
    if re.match(r"switch\b|goto \*", t):
        return "switch"
    if re.match(r"(__)?try\b", t):
        return "try"
    if re.match(r"(goto|break|continue|return)\b", t):
        return "jump"
    return "branch"


def back_edges(blocks: dict[str, Blk]) -> set[tuple[str, int]]:
    """DFS from ENTRY over the reachable successors; an edge into a block still on the stack is a back edge."""
    entry = next((n for n, b in blocks.items() if b.flag == "ENTRY"), None)
    if entry is None:
        return set()
    state: dict[str, int] = {entry: 1}
    back: set[tuple[str, int]] = set()
    stack = [(entry, iter(enumerate(blocks[entry].succs)))]
    while stack:
        n, it = stack[-1]
        for i, (t, unreachable, _) in it:
            if t not in blocks or unreachable:
                continue
            if state.get(t) == 1:
                back.add((n, i))
            elif t not in state:
                state[t] = 1
                stack.append((t, iter(enumerate(blocks[t].succs))))
                break
        else:
            state[n] = 2
            stack.pop()
    return back


def head_of(b: Blk) -> str:
    return b.name + (f" ({b.flag})" if b.flag else "")


def block_lines(b: Blk) -> list[tuple[str, str]]:
    """Label lines of a block: header centered, the rest left-justified."""
    out = [(head_of(b), "c")]
    if b.label:
        out.append((clip(b.label), "l"))
    out += [(clip(e), "l") for e in b.elems[:MAX_ELEMS]]
    if len(b.elems) > MAX_ELEMS:
        out.append((f"… +{len(b.elems) - MAX_ELEMS} more", "l"))
    if b.n_elems:
        out.append((f"{b.n_elems} elems", "l"))
    if b.term is not None:
        out.append((clip("T: " + b.term), "l"))
    return out


def block_tooltip(b: Blk) -> str:
    lines = [head_of(b)] + ([b.label] if b.label else []) + b.elems + ([f"{b.n_elems} elems"] if b.n_elems else [])
    if b.term is not None:
        lines.append("T: " + b.term)
    return "\\n".join(q(x)[1:-1] for x in lines)


def edge_style(blocks: dict[str, Blk], i: int, target: str, unreachable: bool, explicit: str, kind: str | None, two: bool):
    """(label, classes) of the i-th successor edge of a block whose terminator is `kind`."""
    cls, label = [], explicit
    dest = blocks.get(target)
    if two:
        cls.append("t" if i == 0 else "f")
        label = label or ("T" if i == 0 else "F")
    elif explicit in ("T", "F"):
        cls.append(explicit.lower())
    elif kind == "switch":
        label = str(i)  # the successor index; the case / default label is on the destination block
    elif kind == "try":
        cls.append("eh")
        label = dest.label.rstrip(":") if dest and dest.label else ""
    if unreachable:
        cls.append("weak")
        label = f"{label} unreachable".strip()
    return label, cls


def stmt(ident: str, attrs: dict[str, str]) -> str:
    return f"{ident} [" + ", ".join(f"{k}={v}" for k, v in attrs.items()) + "];"


def fn_body(fn: Fn, pre: str, indent: str) -> list[str]:
    """Node and edge statements of one function; node ids are pre + block name."""
    blocks, back = fn.blocks, back_edges(fn.blocks)
    nodes: list[str] = []
    stubs: dict[str, str] = {}
    edges: list[str] = []
    links: list[tuple[str, str]] = []  # (tail, head) node ids of the drawn edges

    def nid(n: str) -> str:
        return q(pre + n)

    for n, b in blocks.items():
        cls = ["entry"] * (b.flag == "ENTRY") + ["exit"] * (b.flag == "EXIT")
        if b.term is not None and term_kind(b.term) in ("branch", "switch"):
            cls.append("cond")
        attrs = {"label": f'"{label_of(block_lines(b))}"'}
        if len(block_lines(b)) > 1:
            attrs["tooltip"] = f'"{block_tooltip(b)}"'
        if cls:
            attrs["class"] = q(" ".join(cls))
        nodes.append(stmt(nid(n), attrs))

    def add_edge(src: str, i: int, target: str, unreachable: bool, explicit: str, kind: str | None, two: bool) -> None:
        label, cls = edge_style(blocks, i, target, unreachable, explicit, kind, two)
        if target == "NULL":
            ghost = f"null_{pre}{src}_{i}"
            stubs[ghost] = stmt(q(ghost), {"label": '"NULL"', "class": '"dim"', "tooltip": '"no successor: the edge was pruned"'})
            tgt = q(ghost)
            cls.append("weak")
        else:
            if target not in blocks and target not in stubs:
                note = {"tooltip": '"not part of this output"'} if fn.content else {}
                stubs[target] = stmt(nid(target), {"label": q(target), "class": q("dim" if fn.content else "block"), **note})
            tgt = nid(target)
        if (src, i) in back:
            cls.append("back")
        attrs = ({"label": q(label)} if label else {}) | ({"class": q(" ".join(cls))} if cls else {})
        edges.append(f"{nid(src)} -> {tgt}" + (" [" + ", ".join(f"{k}={v}" for k, v in attrs.items()) + "]" if attrs else "") + ";")
        links.append((nid(src), tgt))

    for n, b in blocks.items():
        kind = term_kind(b.term) if b.term is not None else None
        two = kind == "branch" and len(b.succs) == 2
        for i, (t, unreachable, explicit) in enumerate(b.succs):
            add_edge(n, i, t, unreachable, explicit, kind, two)
    # an edge a block's Preds line shows (e.g. `B4(Unreachable)` on the successor of a noreturn block)
    # that the predecessor's own Succs line does not list
    for n, b in blocks.items():
        for p, unreachable in b.preds:
            pb = blocks.get(p)
            if pb is not None and pb.seen_succs and not any(t == n for t, _, _ in pb.succs):
                add_edge(p, len(pb.succs), n, unreachable, "", None, False)
    order = [nid(n) for n in blocks] + [re.match(r"\S+", s).group(0) for s in stubs.values()]
    return [indent + s for s in [*nodes, *stubs.values(), *edges, *stack_components(order, links)]]


def stack_components(order: list[str], links: list[tuple[str, str]]) -> list[str]:
    """A slice of a dump can hold unconnected pieces; Graphviz would put them in one row. Chain them: an invisible
    edge from the deepest node of each piece to the first node of the next (see stack())."""
    parent = {n: n for n in order}

    def find(n: str) -> str:
        while parent[n] != n:
            parent[n] = parent[parent[n]]
            n = parent[n]
        return n

    for a, b in links:
        if a in parent and b in parent:
            parent[find(a)] = find(b)
    comps: dict[str, list[str]] = {}
    for n in order:
        comps.setdefault(find(n), []).append(n)
    if len(comps) < 2:
        return []
    kids: dict[str, list[str]] = {}
    for a, b in links:
        kids.setdefault(a, []).append(b)
    heads = {b for _, b in links}
    ends = []
    for nodes in comps.values():
        top = next((n for n in nodes if n not in heads), nodes[0])
        depth, todo = {top: 0}, [top]
        while todo:
            u = todo.pop()
            for v in kids.get(u, []):
                if v not in depth:
                    depth[v] = depth[u] + 1
                    todo.append(v)
        ends.append((top, max(depth, key=lambda n: depth[n])))
    return [f"{a_last} -> {b_first} [style=invis, weight=100];" for (_, a_last), (b_first, _) in zip(ends, ends[1:])]


def worth_drawing(fns: list[Fn]) -> bool:
    blocks = [b for f in fns for b in f.blocks.values()]
    return len(blocks) >= 2 and sum(len(b.succs) for b in blocks) >= 1


def graph_name(fns: list[Fn]) -> str:
    m = re.search(r"([\w:~]+)\s*\(", fns[0].title) if len(fns) == 1 else None
    return "cfg_" + re.sub(r"\W+", "_", m.group(1)).strip("_") if m else "cfg"


def stack(clusters: list[tuple[str, str]]) -> list[str]:
    """Several functions are drawn as clusters one below the other, not side by side: an invisible, heavy edge from
    the last node of each cluster to the first node of the next ranks them in sequence (so a wide row of small,
    unreadable graphs becomes one column that fits the page width). clusters = [(first node id, last node id)]."""
    return [f"  {q(a_last)} -> {q(b_first)} [style=invis, weight=100];" for (_, a_last), (b_first, _) in zip(clusters, clusters[1:])]


def render(fns: list[Fn]) -> str | None:
    fns = [f for f in fns if f.blocks]
    if not fns or not worth_drawing(fns):
        return None
    out = [f"digraph {graph_name(fns)} {{", "  ordering=out;"]
    if len(fns) == 1:
        if fns[0].title:
            out.append(f"  label={q(clip(fns[0].title, 80))}; labelloc=t; labeljust=l;")
        out += fn_body(fns[0], "", "  ")
    else:
        ends = []
        for k, fn in enumerate(fns, 1):
            out.append(f"  subgraph cluster_f{k} {{")
            if fn.title:
                out.append(f"    label={q(clip(fn.title, 80))}; labeljust=l;")
            out += fn_body(fn, f"f{k}_", "    ")
            out.append("  }")
            names = list(fn.blocks)
            ends.append((f"f{k}_" + next((n for n in names if fn.blocks[n].flag == "ENTRY"), names[0]),
                         f"f{k}_" + next((n for n in names if fn.blocks[n].flag == "EXIT"), names[-1])))
        out += stack(ends)
    out.append("}")
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------
# debug.DumpCFG (also p02_skeleton -dump, p03_lambdas --dump, ...)
# --------------------------------------------------------------------------
BLOCK_HDR = re.compile(r"^ \[(B\d+)(?: \(([^)]*)\))?\]\s*$")
FN_HDR = re.compile(r"^[^\s\[(\-]")
DIAG = re.compile(r":\d+:\d+: |^\s*\d+ \|")  # clang diagnostics and their source excerpts are not function headers


def succ_tokens(s: str) -> list[tuple[str, bool, str]]:
    return [("NULL" if m.group(0) in ("NULL", "null") else m.group(1), bool(m.group(2)), "") for m in SUCC_TOKEN.finditer(s)]


def is_header(lines: list[str], i: int) -> bool:
    """An unindented line directly followed (blank lines aside) by a block header names the function."""
    if not FN_HDR.match(lines[i]) or DIAG.search(lines[i]):
        return False
    nxt = next((x for x in lines[i + 1:] if x.strip()), "")
    return bool(BLOCK_HDR.match(nxt))


def parse_dumpcfg(text: str, cmd: str = "") -> str | None:
    lines = text.split("\n")
    fns: list[Fn] = []
    fn: Fn | None = None
    cur: Blk | None = None
    title, last = "", None  # `last`: what a continuation line extends ('elem' | 'term')
    for i, ln in enumerate(lines):
        m = BLOCK_HDR.match(ln)
        if m:
            if fn is None or m.group(1) in fn.blocks:  # a new function (or a repeated block id)
                fn = Fn(title)
                fns.append(fn)
                title = ""
            cur = Blk(m.group(1), flag=(m.group(2) or "").strip(), seen_succs=(m.group(2) or "").strip() == "EXIT")
            fn.blocks[cur.name] = cur
            last = None
        elif is_header(lines, i):
            fn, cur, title, last = None, None, ln.strip(), None
        elif cur is None or not ln.strip():
            continue
        elif (m := re.match(r"^ {3}(\d+): ?(.*)$", ln)):
            cur.elems.append(f"{m.group(1)}: {m.group(2).strip()}")
            last = "elem"
        elif (m := re.match(r"^ {3}T: ?(.*)$", ln)):
            cur.term, last = m.group(1).strip(), "term"
        elif (m := re.match(r"^ {3}Preds \(\d+\):(.*)$", ln)):
            cur.preds, last = [(t, u) for t, u, _ in succ_tokens(m.group(1)) if t != "NULL"], None
        elif (m := re.match(r"^ {3}Succs \(\d+\):(.*)$", ln)):
            cur.succs, cur.seen_succs, last = succ_tokens(m.group(1)), True, None
        elif (m := re.match(r"^ {2}(\S.*:)\s*$", ln)) and not cur.elems and cur.term is None:
            cur.label = m.group(1)  # `case 2:`, `default:`, `catch (int e):`, `loop:`
        elif last == "elem" and cur.elems:  # a statement that spans lines
            cur.elems[-1] += " " + ln.strip()
        elif last == "term" and cur.term is not None:
            cur.term += " " + ln.strip()
    return render(fns) if any("Succs (" in x for x in lines) else None


# --------------------------------------------------------------------------
# scripts/cfgshape.sh: one line per block
# --------------------------------------------------------------------------
SHAPE_LINE = re.compile(r"^\s+(B\d+)\s+(?P<flag>.*?)\s*(?P<n>\d+) elems\s+(?:(?P<term>T: .*?)\s+)?->\s*(?P<succ>.*?)\s*$")


def parse_cfgshape(text: str, cmd: str = "") -> str | None:
    fns: list[Fn] = []
    fn: Fn | None = None
    for ln in text.split("\n"):
        m = SHAPE_LINE.match(ln)
        if m:
            if fn is None:
                fn = Fn()
                fns.append(fn)
            flag = m.group("flag").strip()
            b = Blk(m.group(1), n_elems=int(m.group("n")), term=(m.group("term") or "")[3:] or None, seen_succs=True)
            if flag.endswith(":"):
                b.label = flag
            else:
                b.flag = flag
            b.succs = succ_tokens(m.group("succ"))
            fn.blocks[b.name] = b
        elif ln.strip() and not ln.startswith(" "):
            fn = Fn(ln.strip())
            fns.append(fn)
    return render(fns)


# --------------------------------------------------------------------------
# debug.DumpDominators / DumpPostDominators / DumpControlDependencies
# --------------------------------------------------------------------------
TREE_HDR = [
    (re.compile(r"^Immediate post dominance tree"), "post"),
    (re.compile(r"^Immediate dominance tree"), "dom"),
    (re.compile(r"^Control dependencies"), "cd"),
]
TREE_TITLE = {"dom": "immediate dominators: idom -> block", "post": "immediate post-dominators: ipdom -> block",
              "cd": "control dependence: decider -> dependent block"}
PAIR = re.compile(r"^\((\d+),(\d+)\)\s*$")


def parse_domtree(text: str, cmd: str = "") -> str | None:
    default = ("post" if "DumpPostDominators" in cmd else "dom" if "DumpDominators" in cmd
               else "cd" if "DumpControlDependencies" in cmd else None)
    groups: list[tuple[str, list[tuple[int, int]]]] = []
    for ln in text.split("\n"):
        kind = next((k for pat, k in TREE_HDR if pat.match(ln)), None)
        if kind:
            groups.append((kind, []))
        elif (m := PAIR.match(ln)):
            if not groups and default:
                groups.append((default, []))
            if groups:
                groups[-1][1].append((int(m.group(1)), int(m.group(2))))
    groups = [g for g in groups if len({x for p in g[1] for x in p}) >= 3 and any(n != d for n, d in g[1])]
    if not groups:
        return None
    out = ["digraph domtree {"]

    ends: list[tuple[str, str]] = []

    def body(kind: str, pairs: list[tuple[int, int]], pre: str, ind: str) -> None:
        # (node, idom) pairs; for control dependence (dependent, decider). Edges run parent -> child.
        roots = {n for n, d in pairs if n == d}
        deciders = {d for _, d in pairs} if kind == "cd" else set()
        for n in sorted({x for p in pairs for x in p}):
            cls = "entry" if kind == "dom" and n in roots else "exit" if kind == "post" and n in roots else "cond" if n in deciders else ""
            out.append(f"{ind}{q(f'{pre}B{n}')} [label=\"B{n}\"" + (f', class="{cls}"' if cls else "") + "];")
        out.extend(f"{ind}{q(f'{pre}B{d}')} -> {q(f'{pre}B{n}')};" for n, d in pairs if n != d or kind == "cd")  # (n, n) is the root, except in a CDG
        # first / last node for stacking several trees: a node nothing points to, and the deepest node below it
        kids: dict[int, list[int]] = {}
        for n, d in pairs:
            if n != d:
                kids.setdefault(d, []).append(n)
        top = min(roots) if roots else min({d for _, d in pairs} - {n for n, d in pairs if n != d} or {pairs[0][1]})
        depth, todo = {top: 0}, [top]
        while todo:
            u = todo.pop()
            for v in kids.get(u, []):
                if v not in depth:
                    depth[v] = depth[u] + 1
                    todo.append(v)
        ends.append((f"{pre}B{top}", f"{pre}B{max(depth, key=lambda x: (depth[x], x))}"))

    if len(groups) == 1:
        out.append(f"  label={q(TREE_TITLE[groups[0][0]])}; labelloc=t; labeljust=l;")
        body(*groups[0], "", "  ")
    else:
        for k, (kind, pairs) in enumerate(groups, 1):
            out.append(f"  subgraph cluster_t{k} {{ label={q(TREE_TITLE[kind])}; labeljust=l;")
            body(kind, pairs, f"t{k}_", "    ")
            out.append("  }")
        out += stack(ends)
    out.append("}")
    return "\n".join(out) + "\n"


# --------------------------------------------------------------------------
# DOT text: debug.ViewCFG files and p02_export --format=dot
# --------------------------------------------------------------------------
DOT_NODE = re.compile(r'^\s*"?(?P<id>[\w.]+)"?\s*\[(?P<attrs>.*)\]\s*;?\s*$')
DOT_EDGE = re.compile(r'^\s*"?(?P<a>[\w.]+)"?\s*->\s*"?(?P<b>[\w.]+)"?\s*(?:\[(?P<attrs>.*)\])?\s*;?\s*$')
DOT_LABEL = re.compile(r'\blabel\s*=\s*"((?:[^"\\]|\\.)*)"')
BLOCK_ROW = re.compile(r"^\[?(B\d+)(?: \(([^)]*)\))?\]?$")


def dot_rows(label: str) -> list[str]:
    """A record/box label -> its rows: split at the \\l \\n \\r line ends, outer braces dropped, escapes resolved."""
    s = label.strip()
    if s.startswith("{") and s.endswith("}"):
        s = s[1:-1]
    return [re.sub(r"\\(.)", r"\1", x).strip() for x in re.split(r"\\[lnr]", s) if x.strip()]


def parse_dot_text(text: str, cmd: str = "") -> str | None:
    fn = Fn()
    ids: dict[str, str] = {}  # dot id -> block name
    edges: list[tuple[str, str, str]] = []
    for ln in text.split("\n"):
        if (m := DOT_EDGE.match(ln)):
            edges.append((m.group("a"), m.group("b"), m.group("attrs") or ""))
            continue
        m = DOT_NODE.match(ln)
        lab = DOT_LABEL.search(m.group("attrs")) if m and m.group("id") not in ("node", "edge", "graph") else None
        rows = dot_rows(lab.group(1)) if lab else []
        hm = BLOCK_ROW.match(rows[0]) if rows else None
        if not hm:
            continue
        b = Blk(hm.group(1), flag=(hm.group(2) or "").strip(), seen_succs=True)
        for r in rows[1:]:
            if r.startswith("T:"):
                b.term = r[2:].strip()
            else:
                b.elems.append(r)
        ids[m.group("id")] = b.name
        fn.blocks[b.name] = b
    if not edges:
        return None
    fn.content = bool(fn.blocks)  # edges only (a grep of the dot file): the blocks are real, just not described
    for a, bn, attrs in edges:
        src, dst = ids.get(a, a), ids.get(bn, bn)
        lab = DOT_LABEL.search(attrs)
        for n in (src,) + ((dst,) if not fn.content else ()):
            fn.blocks.setdefault(n, Blk(n, seen_succs=True))
        fn.blocks[src].succs.append((dst, "dashed" in attrs, lab.group(1) if lab else ""))
    return render([fn])


# --------------------------------------------------------------------------
# dispatch: by command where the command says what the output is, else by what the output looks like
# --------------------------------------------------------------------------
def parse_sniff(text: str, cmd: str = "") -> str | None:
    if "Succs (" in text and re.search(r"(?m)^ \[B\d+[^\]]*\]\s*$", text):
        return parse_dumpcfg(text, cmd)
    if re.search(r"(?m)^\s+B\d+\s.*\d+ elems\s.*->", text):
        return parse_cfgshape(text, cmd)
    if re.search(r"(?m)^(Immediate (post )?dominance tree|Control dependencies) ", text):
        return parse_domtree(text, cmd)
    if re.search(r"(?m)^\s*digraph\b", text) or (re.search(r"--format=dot|\.dot\b", cmd) and re.search(r'(?m)^\s*"?B\d+"? -> "?B\d+', text)):
        return parse_dot_text(text, cmd)
    return None


PARSERS = [
    (re.compile(r"cfgshape\.sh"), parse_cfgshape),
    (re.compile(r"Dump(Post)?Dominators|DumpControlDependencies"), parse_domtree),
    (re.compile(r""), parse_sniff),
]
