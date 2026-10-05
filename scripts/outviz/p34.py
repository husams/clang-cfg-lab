"""Graphs from the Part 3 and Part 4 tool outputs.

  p03_elems --blocks   the CFG: one header line per block with terminator, label, successors,
                       the printed elements become the block's lines
  p03_eh               try-dispatch / handler / throw / noreturn blocks and their exception edges
  p04_dom              the dominator and post-dominator trees; back edges (and the retreating edge
                       of an irreducible CFG) on top; with only the `loop` lines, the loop nest
  p04_graphs           the cyclic SCCs and the DFS back edges
  p04_reach --matrix   the isReachable relation (weak edges, not CFG edges)
  p04_slice            the control dependence graph (cd), the dominance-frontier relation (df)

Only what the text shows is drawn. p04_orders, p04_solver, p04_context, p03_ctors, p03_compare and
p03_lambdas print no successor relation, so they are not registered. An output with a line this module
does not know (a mixed bash block, a sed-filtered listing, the --query or --count forms) gives None; so does
a listing that lacks blocks. A grep that only drops the `== name` header (or element lines) still draws when
every block header is there.

Edge rules shared with cfg.py: the two successors of a conditional branch are T / F (class t / f), the
edges out of a try dispatch and every extra successor of a block without terminator (the may-throw edge
of AddEHEdges) are eh, a pruned (null) successor ends in a dim stub, ENTRY / EXIT are class entry / exit
and DFS back edges from ENTRY are class back. Blocks that ENTRY cannot reach are dim.
"""
import re
from dataclasses import dataclass, field

MAX_W = 50  # label lines longer than this are cut with an ellipsis (the full text goes in the tooltip)
MAX_ELEMS = 12  # a block with more printed elements shows this many, then "+N more"
SPLIT_W = 30  # an element line wider than this puts its details on a second line
MAX_SIDE_BY_SIDE = 8  # dominator + post-dominator tree: more nodes than this on their widest levels, and the trees are stacked
MAX_SECTIONS = 4  # more functions than this in one output: not one figure
MAX_REACH_EDGES = 24  # a denser isReachable relation is a hairball, not a picture


class _Skip(Exception):
    """The output is not (all) in a format this module draws."""


# --------------------------------------------------------------------------
# dot helpers
# --------------------------------------------------------------------------
def q(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def clip(s: str, w: int = MAX_W) -> str:
    return s if len(s) <= w else s[: w - 1].rstrip() + "…"


def gname(*parts: str) -> str:
    s = re.sub(r"[^A-Za-z0-9_]+", "_", "_".join(parts)).strip("_")[:48] or "g"
    return s if not s[0].isdigit() else "g_" + s


def node(nid: str, header: str, lines: list[str] | tuple = (), cls: list[str] | tuple = (), extra: list[str] | tuple = ()) -> str:
    """A node: centered header, then left-justified lines; the tooltip holds the untruncated text."""
    shown = [clip(x) for x in lines]
    label = q(header)[1:-1]
    if shown:
        label += "\\n" + "".join(q(x)[1:-1] + "\\l" for x in shown)
    attrs = [f'label="{label}"']
    if shown != list(lines):
        attrs.append('tooltip="' + "\\n".join(q(x)[1:-1] for x in [header, *lines]) + '"')
    if cls:
        attrs.append(f'class="{" ".join(cls)}"')
    return f"{q(nid)} [{', '.join([*attrs, *extra])}];"


def edge(a: str, b: str, cls: list[str] | tuple = (), label: str = "", extra: list[str] | tuple = ()) -> str:
    attrs = ([f"label={q(label)}"] if label else []) + ([f'class="{" ".join(cls)}"'] if cls else []) + list(extra)
    return f"{q(a)} -> {q(b)}" + (f" [{', '.join(attrs)}]" if attrs else "") + ";"


def digraph(name: str, body: list[str], title: str = "") -> str:
    out = [f"digraph {name} {{", "  rankdir=TB;"]
    if not any("style=invis" in x for x in body):  # dot 15 crashes on ordering=out with invisible edges between clusters
        out.append("  ordering=out;")
    if title:
        out.append(f"  label={q(clip(title, 80))}; labelloc=t; labeljust=l;")
    out += ["  " + x for x in body]
    out.append("}")
    return "\n".join(out) + "\n"


def cluster(key: str, label: str, body: list[str], group: bool = True) -> list[str]:
    head = f"subgraph cluster_{key} {{"
    attrs = f"label={q(label)}; labeljust=l;" + (' class="group";' if group else "")
    return [head, "  " + attrs, *["  " + x for x in body], "}"]


def sections(text: str, hdr: re.Pattern) -> list[tuple[re.Match | None, list[str]]]:
    """Split an output at its `== name ...` header lines; a headerless output is one section."""
    secs: list[tuple[re.Match | None, list[str]]] = []
    for raw in text.splitlines():
        line = raw.rstrip()
        if not line.strip():
            continue
        m = hdr.match(line)
        if m:
            secs.append((m, []))
        else:
            if not secs:
                secs.append((None, []))
            secs[-1][1].append(line)
    if not secs:
        raise _Skip
    return secs


def dfs(succ: dict[int, list[int]], start: int) -> tuple[set[int], set[tuple[int, int]]]:
    """(blocks reachable from start, DFS back edges): an edge into a block still on the stack is a back edge."""
    state = {start: 1}
    back: set[tuple[int, int]] = set()
    stack = [(start, iter(succ.get(start, [])))]
    while stack:
        n, it = stack[-1]
        for s in it:
            if state.get(s) == 1:
                back.add((n, s))
            elif s not in state:
                state[s] = 1
                stack.append((s, iter(succ.get(s, []))))
                break
        else:
            state[n] = 2
            stack.pop()
    return set(state), back


def ids(s: str) -> list[int]:
    """'B3 B1 B0' -> [3, 1, 0]; anything else in the string is not a block list."""
    toks = s.split()
    if not all(re.fullmatch(r"B\d+", t) for t in toks):
        raise _Skip
    return [int(t[1:]) for t in toks]


NODE_STMT = re.compile(r'^"((?:[^"\\]|\\.)*)" \[label=')
EDGE_STMT = re.compile(r'^"((?:[^"\\]|\\.)*)" -> "((?:[^"\\]|\\.)*)"')


def ends(body: list[str]) -> tuple[list[str], list[str]]:
    """(nodes without an incoming edge, nodes without an outgoing edge) of a body, as far as ranking sees them."""
    nodes, dsts, srcs = [], set(), set()
    for ln in body:
        ln = ln.strip()
        if m := NODE_STMT.match(ln):
            nodes.append(m[1])
        elif (m := EDGE_STMT.match(ln)) and "constraint=false" not in ln and "style=invis" not in ln:
            srcs.add(m[1])
            dsts.add(m[2])
    return [n for n in nodes if n not in dsts], [n for n in nodes if n not in srcs]


def render(name: str, bodies: list[tuple[str, list[str]]], edges: int, title: str = "") -> str | None:
    """One body: drawn as is; several: one cluster each, stacked top to bottom with invisible edges. None when no edge was drawn."""
    if not edges or len(bodies) > MAX_SECTIONS:
        return None
    if len(bodies) == 1:
        return digraph(name, bodies[0][1], bodies[0][0] or title)
    out: list[str] = []
    for k, (label, body) in enumerate(bodies, 1):
        out += cluster(f"f{k}", label, body)
    for (_, a), (_, b) in zip(bodies, bodies[1:]):
        for x in ends(a)[1]:
            for y in ends(b)[0]:
                out.append(edge(x, y, extra=["style=invis"]))
    return digraph(name, out, title)


# --------------------------------------------------------------------------
# p03_elems --blocks
# --------------------------------------------------------------------------
E_HDR = re.compile(r"^== (?P<fn>.+?) @L(?P<ln>\d+): (?P<n>\d+) blocks$")
E_BLK = re.compile(
    r"^B(?P<id>\d+)(?: (?P<role>ENTRY|EXIT))?(?: term=(?P<term>\S+))?(?: label=(?P<label>\S+))?"
    r"(?P<nr> noreturn)? ->(?P<succ>(?: B\d+| \(pruned\))*)$"
)
E_ELEM = re.compile(r"^\s+B(?P<b>\d+)\.(?P<i>\d+) (?P<kind>\w+)\s*(?P<rest>.*)$")
TRY_TERMS = {"CXXTryStmt", "SEHTryStmt", "ObjCAtTryStmt"}
MULTIWAY_TERMS = {"SwitchStmt", "IndirectGotoStmt", "GCCAsmStmt", "MSAsmStmt"}


@dataclass
class Blk:
    id: int
    role: str = ""
    term: str = ""  # "StmtBranch:IfStmt"
    label: str = ""
    noreturn: bool = False
    succs: list[int | None] = field(default_factory=list)  # None = pruned
    elems: list[list[str]] = field(default_factory=list)  # one entry per element, one or two label lines


def elem_lines(idx: str, kind: str, rest: str) -> list[str]:
    """One printed element -> one label line, or two (`idx: Kind`, then the details) when it would be wide."""
    r = re.sub(r"\bdtor=", "", rest)
    r = re.sub(r"\s*\bnoreturn=0\b", "", r)
    r = re.sub(r"\bnoreturn=1\b", "noreturn", r)
    r = re.sub(r"\s*\btype=.*?(?=\s+trigger=|$)", "", r)
    r = re.sub(r"\btrigger=", "by ", r)
    r = re.sub(r"\s{2,}", " ", r).strip()
    if kind == "Statement":  # the statement class is the interesting word
        kind, _, r = r.partition(" ")
        r = r.strip()
    one = f"{idx}: {kind} {r}".rstrip()
    return [one] if len(one) <= SPLIT_W or not r else [f"{idx}: {kind}", "    " + r]


def term_line(term: str) -> str:
    kind, _, stmt = term.partition(":")
    if kind == "StmtBranch":
        return "T: " + stmt
    return f"T: {stmt} ({kind})" if stmt else "T: " + kind


def parse_elems(text: str, cmd: str) -> str | None:
    try:
        return _elems(text)
    except _Skip:
        return None


def _elems(text: str) -> str | None:
    secs = sections(text, E_HDR)
    if len(secs) > 1 and any(h is None for h, _ in secs):
        raise _Skip
    bodies: list[tuple[str, list[str]]] = []
    edges = 0
    names: list[str] = []
    for k, (h, lines) in enumerate(secs):
        blocks: dict[int, Blk] = {}
        cur: Blk | None = None
        for line in lines:
            if m := E_BLK.match(line):
                cur = Blk(int(m["id"]), m["role"] or "", m["term"] or "", m["label"] or "", bool(m["nr"]))
                cur.succs = [None if t == "(pruned)" else int(t[1:]) for t in m["succ"].split()]
                blocks[cur.id] = cur
            elif (m := E_ELEM.match(line)) and cur and int(m["b"]) == cur.id:
                cur.elems.append(elem_lines(m["i"], m["kind"], m["rest"]))
            else:
                raise _Skip
        if h is not None and len(blocks) != int(h["n"]):
            raise _Skip  # some blocks are missing
        if h is None:  # the `== name` line was filtered out (grep): accept it only when every block is there
            if sorted(blocks) != list(range(len(blocks))) or blocks[0].role != "EXIT" or blocks[len(blocks) - 1].role != "ENTRY":
                raise _Skip
        pre = f"f{k + 1}_" if len(secs) > 1 else ""
        body, n_edges = cfg_body(blocks, pre)
        edges += n_edges
        if h:
            names.append(h["fn"])
            bodies.append((f"{h['fn']} @L{h['ln']}: {h['n']} blocks", body))
        else:
            bodies.append(("", body))
    return render("elems_" + gname(*names), bodies, edges)


def cfg_body(blocks: dict[int, Blk], pre: str) -> tuple[list[str], int]:
    entry = next((b.id for b in blocks.values() if b.role == "ENTRY"), None)
    succ = {b.id: [s for s in b.succs if s is not None and s in blocks] for b in blocks.values()}
    reach, back = dfs(succ, entry) if entry is not None else (set(blocks), set())
    nodes: list[str] = []
    edges: list[str] = []
    stubs: list[str] = []
    for b in blocks.values():
        head = f"B{b.id}" + (f" ({b.role})" if b.role else " (NORETURN)" if b.noreturn else "")
        lines = ([f"label: {b.label}"] if b.label else []) + [x for e in b.elems[:MAX_ELEMS] for x in e]
        if len(b.elems) > MAX_ELEMS:
            lines.append(f"… +{len(b.elems) - MAX_ELEMS} more")
        if b.term:
            lines.append(term_line(b.term))
        cls = [{"ENTRY": "entry", "EXIT": "exit"}[b.role]] if b.role else []
        if b.term and len(b.succs) >= 2:
            cls.append("cond")
        if b.id not in reach:
            cls.append("dim")
        nodes.append(node(pre + f"B{b.id}", head, lines, cls))

        kind, _, stmt = b.term.partition(":")
        two = bool(b.term) and len(b.succs) == 2 and stmt not in TRY_TERMS | MULTIWAY_TERMS
        for i, s in enumerate(b.succs):
            if two:
                cls, label = (["t"], "T") if i == 0 else (["f"], "F")
            elif stmt in TRY_TERMS or (not b.term and i > 0):
                cls, label = ["eh"], ""  # try dispatch; the may-throw edge next to the normal one
            else:
                cls, label = [], ""
            if s is None:
                stub = f"{pre}null_B{b.id}_{i}"
                stubs.append(node(stub, "(pruned)", cls=["dim"]))
                dst = stub
            else:
                dst = pre + f"B{s}"
                if s not in blocks:
                    raise _Skip
                if (b.id, s) in back:
                    cls = [*cls, "back"]
            edges.append(edge(pre + f"B{b.id}", dst, cls, label))
    return [*nodes, *stubs, *edges], len(edges)


# --------------------------------------------------------------------------
# p03_eh
# --------------------------------------------------------------------------
H_HDR = re.compile(r"^== (?P<fn>.+?): (?P<n>\d+) blocks, (?P<t>\d+) try-dispatch block\(s\), AddEHEdges=(?P<eh>[01])$")
H_TRY = re.compile(r"^try B(?P<id>\d+)\s+(?P<what>\w+)@L(?P<ln>\d+)\s+preds=\[(?P<preds>[^\]]*)\]$")
H_HANDLER = re.compile(r"^\s+handler B(?P<id>\d+)\s+(?P<what>.+)$")
H_UNMATCHED = re.compile(r"^\s+unmatched -> B(?P<id>\d+)(?: \(exit\))?$")
H_PRUNED = re.compile(r"^\s+\(pruned\)$")
H_THROW = re.compile(r"^throw B(?P<id>\d+)\s+rethrow=(?P<re>[01])\s+->(?P<succ>(?: B\d+)*)$")
H_NORETURN = re.compile(r"^noreturn B(?P<id>\d+) ->(?P<succ>(?: B\d+)*)$")


def parse_eh(text: str, cmd: str) -> str | None:
    try:
        return _eh(text)
    except _Skip:
        return None


def _eh(text: str) -> str | None:
    secs = sections(text, H_HDR)
    bodies: list[tuple[str, list[str]]] = []
    edges = 0
    names: list[str] = []
    for k, (h, lines) in enumerate(secs):
        if h is None:
            raise _Skip
        pre = f"f{k + 1}_" if len(secs) > 1 else ""
        roles: dict[int, list[str]] = {}  # block -> label lines
        cls: dict[int, set[str]] = {}
        es: list[tuple[str, int | str, str, str]] = []  # (src, dst, class, label); dst is a block id or a stub name
        stubs: list[str] = []
        cur: int | None = None

        def add(b: int, line: str, c: str = "") -> None:
            roles.setdefault(b, [])
            if line and line not in roles[b]:
                roles[b].append(line)
            cls.setdefault(b, set())
            if c:
                cls[b].add(c)

        for line in lines:
            if m := H_TRY.match(line):
                cur = int(m["id"])
                add(cur, f"{m['what']}@L{m['ln']}", "cond")
                for p in ids(m["preds"].replace(",", " ")):
                    add(p, "")
                    es.append((f"B{p}", cur, "eh", ""))
            elif (m := H_HANDLER.match(line)) and cur is not None:
                add(int(m["id"]), m["what"])
                es.append((f"B{cur}", int(m["id"]), "eh", ""))
            elif (m := H_UNMATCHED.match(line)) and cur is not None:
                add(int(m["id"]), "")
                es.append((f"B{cur}", int(m["id"]), "eh", "unmatched"))
            elif H_PRUNED.match(line) and cur is not None:
                stub = f"null_B{cur}_{len(stubs)}"
                stubs.append(stub)
                es.append((f"B{cur}", stub, "eh", ""))
            elif m := H_THROW.match(line):
                cur = None
                b = int(m["id"])
                add(b, "rethrow" if m["re"] == "1" else "throw")
                for s in ids(m["succ"]):
                    add(s, "")
                    es.append((f"B{b}", s, "eh", ""))
            elif m := H_NORETURN.match(line):
                cur = None
                b = int(m["id"])
                add(b, "noreturn call")
                for i, s in enumerate(ids(m["succ"])):
                    add(s, "")
                    es.append((f"B{b}", s, "eh" if i else "", ""))
            else:
                raise _Skip

        body = []
        for b in sorted(roles):
            head = f"B{b}" + (" (EXIT)" if b == 0 else "")
            c = sorted(cls[b]) + (["exit"] if b == 0 else [])
            body.append(node(pre + f"B{b}", head, roles[b], c))
        body += [node(pre + s, "(pruned)", cls=["dim"]) for s in stubs]
        uniq: dict[tuple[str, int | str], tuple[str, str]] = {}  # (src, dst) -> (class, label): one edge, keep the label
        for src, dst, c, label in es:
            old = uniq.get((src, dst))
            uniq[(src, dst)] = (old[0] or c, old[1] or label) if old else (c, label)
        for (src, dst), (c, label) in uniq.items():
            d = pre + (f"B{dst}" if isinstance(dst, int) else dst)
            body.append(edge(pre + src, d, [c] if c else [], label))
        edges += len(uniq)
        names.append(h["fn"])
        bodies.append((f"{h['fn']}  AddEHEdges={h['eh']}", body))
    return render("eh_" + gname(*names), bodies, edges)


# --------------------------------------------------------------------------
# p04_dom
# --------------------------------------------------------------------------
D_HDR = re.compile(r"^== (?P<fn>.+?): (?P<n>\d+) blocks$")
D_KV = re.compile(r"^(?P<k>root|idom|ipdom|loops|loop|retreat)\s*: (?P<v>.*)$")
D_TREE = re.compile(r"^(?P<k>dom|pdom) tree:$")
D_TREE_NODE = re.compile(r"^(?P<ind> +)(?P<n>B\d+|<virtual>)$")
D_IDOM = re.compile(r"B(?P<b>\d+)(?:<-(?P<p>B\d+|\?|<virtual>)|(?P<root>=root))")
D_LOOP = re.compile(r"^header B(?P<h>\d+) depth (?P<d>\d+) back-edges from (?P<t>B\d+(?:,B\d+)*) body \{(?P<body>B\d+(?: B\d+)*)\}$")
D_RETREAT = re.compile(r"^B(?P<u>\d+) -> B(?P<v>\d+) goes back in DFS but B\d+ does not dominate B\d+ \(irreducible\)$")

VIRTUAL = -1  # the null root of a post-dominator tree
NOT_IN_TREE = -2


@dataclass
class Loop:
    header: int
    depth: int
    tails: list[int]
    body: set[int]


@dataclass
class DomSec:
    idom: dict[int, int | None] | None = None  # block -> parent (None = root, NOT_IN_TREE, VIRTUAL)
    ipdom: dict[int, int | None] | None = None
    loops: list[Loop] = field(default_factory=list)
    retreats: list[tuple[int, int]] = field(default_factory=list)
    saw_loops: bool = False


def parse_idoms(v: str) -> dict[int, int | None]:
    out: dict[int, int | None] = {}
    toks = v.split()
    if not toks:
        raise _Skip
    for t in toks:
        m = D_IDOM.fullmatch(t)
        if not m:
            raise _Skip
        p = m["p"]
        out[int(m["b"])] = None if m["root"] else NOT_IN_TREE if p == "?" else VIRTUAL if p == "<virtual>" else int(p[1:])
    return out


def parse_tree(lines: list[str]) -> dict[int, int | None]:
    """The indented `dom tree:` / `pdom tree:` listing -> block -> parent."""
    out: dict[int, int | None] = {}
    stack: list[int] = []  # ids by depth
    for ln in lines:
        m = D_TREE_NODE.match(ln)
        if not m or len(m["ind"]) % 2:
            raise _Skip
        depth = len(m["ind"]) // 2 - 1
        n = VIRTUAL if m["n"] == "<virtual>" else int(m["n"][1:])
        if depth < 0 or depth > len(stack):
            raise _Skip
        del stack[depth:]
        if n != VIRTUAL:
            out[n] = stack[-1] if stack else None
        stack.append(n)
    return out


def parse_dom(text: str, cmd: str) -> str | None:
    try:
        return _dom(text)
    except _Skip:
        return None


def _dom(text: str) -> str | None:
    secs = sections(text, D_HDR)
    if len(secs) > 1 and any(h is None for h, _ in secs):
        raise _Skip
    bodies: list[tuple[str, list[str]]] = []
    edges = 0
    names: list[str] = []
    for k, (h, lines) in enumerate(secs):
        s = DomSec()
        tree_lines: dict[str, list[str]] = {}
        which = ""
        for line in lines:
            if m := D_TREE.match(line):
                which = m["k"]
                tree_lines[which] = []
            elif which and D_TREE_NODE.match(line):
                tree_lines[which].append(line)
            elif m := D_KV.match(line):
                which = ""
                kk, v = m["k"], m["v"].strip()
                if kk == "idom":
                    s.idom = parse_idoms(v)
                elif kk == "ipdom":
                    s.ipdom = parse_idoms(v)
                elif kk == "loops":
                    if v != "none":
                        raise _Skip
                    s.saw_loops = True
                elif kk == "loop":
                    lm = D_LOOP.match(v)
                    if not lm:
                        raise _Skip
                    s.saw_loops = True
                    s.loops.append(Loop(int(lm["h"]), int(lm["d"]), ids(lm["t"].replace(",", " ")), set(ids(lm["body"]))))
                elif kk == "retreat":
                    if v.startswith("every retreating edge"):
                        continue
                    rm = D_RETREAT.match(v)
                    if not rm:
                        raise _Skip
                    s.retreats.append((int(rm["u"]), int(rm["v"])))
                # `root` carries nothing the idom lines do not
            else:
                raise _Skip
        if s.idom is None and "dom" in tree_lines:
            s.idom = parse_tree(tree_lines["dom"])
        if s.ipdom is None and "pdom" in tree_lines:
            s.ipdom = parse_tree(tree_lines["pdom"])
        fn = h["fn"] if h else ""
        pre = f"f{k + 1}_" if len(secs) > 1 else ""
        body, n_edges = dom_body(s, pre)
        edges += n_edges
        names.append(fn)
        bodies.append((fn, body))
    return render("dom_" + gname(*names), bodies, edges)


def breadth(parents: dict[int, int | None]) -> int:
    """Nodes on the widest level of a tree given as block -> parent (a virtual root counts as a level)."""
    depth: dict[int, int] = {}

    def of(b: int) -> int:
        if b not in depth:
            p = parents.get(b)
            depth[b] = 0 if p is None or p == NOT_IN_TREE else 1 if p == VIRTUAL else of(p) + 1
        return depth[b]

    levels: dict[int, int] = {}
    for b in parents:
        if parents[b] != NOT_IN_TREE:
            levels[of(b)] = levels.get(of(b), 0) + 1
    return max(levels.values(), default=1)


def tree_cluster(kind: str, parents: dict[int, int | None], pre: str, s: DomSec, both: bool) -> tuple[list[str], int]:
    """One (post-)dominator tree as a cluster; parent -> child edges, loops drawn on the dominator tree."""
    tp = pre + ("d_" if both and kind == "dom" else "p_" if both else "")
    headers = {lp.header: lp for lp in s.loops} if kind == "dom" else {}
    nodes: list[str] = []
    edges: list[str] = []
    members = set(parents)
    if any(p == VIRTUAL for p in parents.values()):
        nodes.append(node(tp + "virtual", "<virtual>", cls=["dim"]))
    for b in sorted(members):
        p = parents[b]
        head = f"B{b}" + (" (EXIT)" if b == 0 else " (ENTRY)" if p is None and kind == "dom" else "")
        cls = ["exit"] if b == 0 else ["entry"] if p is None and kind == "dom" else []
        lines: list[str] = []
        if b in headers:
            lines.append(f"loop header, depth {headers[b].depth}")
            cls.append("hl")
        if p == NOT_IN_TREE:
            lines.append("not in tree")
            cls.append("dim")
        nodes.append(node(tp + f"B{b}", head, lines, cls))
    for b in sorted(members):
        p = parents[b]
        if p == VIRTUAL:
            edges.append(edge(tp + "virtual", tp + f"B{b}"))
        elif p is not None and p != NOT_IN_TREE:
            if p not in members:
                raise _Skip
            edges.append(edge(tp + f"B{p}", tp + f"B{b}"))
    if kind == "dom":
        for lp in s.loops:
            for t in lp.tails:
                if t not in members or lp.header not in members:
                    raise _Skip
                edges.append(edge(tp + f"B{t}", tp + f"B{lp.header}", ["back"], extra=["constraint=false"]))
        for u, v in s.retreats:
            if u not in members or v not in members:
                raise _Skip
            edges.append(edge(tp + f"B{u}", tp + f"B{v}", ["hl"], "retreat", ["constraint=false"]))
    label = "dominator tree" if kind == "dom" else "post-dominator tree"
    return cluster(f"{pre}{kind}", label, [*nodes, *edges]), len(edges)


def loop_nest(s: DomSec, pre: str) -> tuple[list[str], int]:
    """The natural loops as nested clusters (no tree in the output); None-like Skip if they do not nest."""
    loops = sorted(s.loops, key=lambda lp: -len(lp.body))
    parent: dict[int, int | None] = {}
    for i, lp in enumerate(loops):
        parent[i] = None
        for j in range(i - 1, -1, -1):
            o = loops[j].body
            if lp.body < o:
                parent[i] = j
                break
            if not lp.body.isdisjoint(o):
                raise _Skip  # overlapping loops cannot be clusters
    owner: dict[int, int] = {}  # block -> innermost loop
    for i, lp in enumerate(loops):
        for b in lp.body:
            owner[b] = i
    headers = {lp.header for lp in loops}

    def draw(i: int) -> list[str]:
        lp = loops[i]
        body: list[str] = []
        for b in sorted(x for x, o in owner.items() if o == i):
            lines = [f"loop header, depth {lp.depth}"] if b == lp.header else []
            body.append(node(pre + f"B{b}", f"B{b}", lines, ["hl"] if b in headers else []))
        for j in sorted(parent):
            if parent[j] == i:
                body += draw(j)
        return cluster(f"{pre}loop_B{lp.header}", f"loop B{lp.header}, depth {lp.depth}", body)

    out: list[str] = []
    for i in sorted(parent):
        if parent[i] is None:
            out += draw(i)
    n = 0
    for lp in loops:
        for t in lp.tails:
            out.append(edge(pre + f"B{t}", pre + f"B{lp.header}", ["back"]))
            n += 1
    for u, v in s.retreats:
        for b in (u, v):
            if b not in owner:
                out.append(node(pre + f"B{b}", f"B{b}"))
        out.append(edge(pre + f"B{u}", pre + f"B{v}", ["hl"], "retreat"))
        n += 1
    return out, n


def dom_body(s: DomSec, pre: str) -> tuple[list[str], int]:
    trees = [(k, p) for k, p in (("dom", s.idom), ("pdom", s.ipdom)) if p is not None]
    if not trees:
        if not s.loops:
            raise _Skip
        return loop_nest(s, pre)
    clusters: list[list[str]] = []
    n = 0
    for kind, parents in trees:
        c, e = tree_cluster(kind, parents, pre, s, len(trees) > 1)
        clusters.append(c)
        n += e
    body = [x for c in clusters for x in c]
    if len(trees) == 2 and sum(breadth(p) for _, p in trees) > MAX_SIDE_BY_SIDE:  # too wide next to each other: one above the other
        body += [edge(a, b, extra=["style=invis"]) for a in ends(clusters[0])[1] for b in ends(clusters[1])[0]]
    return body, n


# --------------------------------------------------------------------------
# p04_graphs
# --------------------------------------------------------------------------
G_HDR = D_HDR
G_KV = re.compile(
    r"^(?P<k>post_order|reverse post|depth_first|inverse po|scc \(sinks 1st\)|cyclic sccs|dfs back edges)\s*: (?P<v>.*)$"
)
G_COMP = re.compile(r"([\[{])((?:B\d+ ?)+)[\]}]")


def parse_graphs(text: str, cmd: str) -> str | None:
    try:
        return _graphs(text)
    except _Skip:
        return None


def _graphs(text: str) -> str | None:
    secs = sections(text, G_HDR)
    if len(secs) > 1 and any(h is None for h, _ in secs):
        raise _Skip
    bodies: list[tuple[str, list[str]]] = []
    edges = 0
    names: list[str] = []
    for k, (h, lines) in enumerate(secs):
        comps: list[tuple[bool, list[int]]] | None = None  # (cyclic, blocks) in scc_iterator order
        cyclic: list[list[int]] | None = None
        back: list[tuple[int, int]] | None = None
        for line in lines:
            m = G_KV.match(line)
            if not m:
                raise _Skip
            kk, v = m["k"], m["v"].strip()
            if kk == "scc (sinks 1st)":
                found = G_COMP.findall(v)
                if not found or " ".join(f"{o}{b.strip()}{'}' if o == '{' else ']'}" for o, b in found) != v:
                    raise _Skip
                comps = [(o == "[", ids(b)) for o, b in found]
            elif kk == "cyclic sccs":
                cm = re.match(r"^(\d+) of (\d+)(?:\s+(.*))?$", v)
                if not cm:
                    raise _Skip
                found = G_COMP.findall(cm[3] or "")
                cyclic = [ids(b) for _, b in found]
            elif kk == "dfs back edges":
                back = []
                if v != "-":
                    for t in v.split():
                        bm = re.fullmatch(r"B(\d+)->B(\d+)", t)
                        if not bm:
                            raise _Skip
                        back.append((int(bm[1]), int(bm[2])))
        if not back:
            continue
        if comps is None:
            comps = [(True, c) for c in cyclic or []]
        pre = f"f{k + 1}_" if len(secs) > 1 else ""
        body: list[str] = []
        seen: set[int] = set()
        order: list[int] = []  # one block per component, to keep scc_iterator's order top to bottom
        for i, (cyc, blocks) in enumerate(comps):
            nodes = [node(pre + f"B{b}", f"B{b}") for b in blocks]
            seen.update(blocks)
            order.append(blocks[0])
            body += cluster(f"{pre}scc{i}", "cycle (SCC)", nodes) if cyc else nodes
        for b in sorted({x for e in back for x in e} - seen):
            body.append(node(pre + f"B{b}", f"B{b}"))
        for a, b in zip(order, order[1:]):
            body.append(edge(pre + f"B{a}", pre + f"B{b}", extra=["style=invis"]))
        for a, b in back:
            body.append(edge(pre + f"B{a}", pre + f"B{b}", ["back"]))
        edges += len(back)
        names.append(h["fn"] if h else "")
        bodies.append((h["fn"] if h else "", body))
    if not bodies:
        return None
    return render("scc_" + gname(*names), bodies, edges, "")


# --------------------------------------------------------------------------
# p04_reach --matrix
# --------------------------------------------------------------------------
R_HDR = D_HDR
R_SCAN = re.compile(r"^scan\s*: (?P<n>\d+) reachable from entry: (?P<v>.*)$")
R_SET = re.compile(r"^(?P<k>dead|self|cycle)\s*: (?P<v>.*)$")
R_ROW = re.compile(r"^B(?P<id>\d+)\s+-> (?P<v>.*)$")
R_QUERY = re.compile(r"^isReachable\(B\d+, B\d+\) = (?:true|false)$")


def blocklist(v: str) -> list[int]:
    return [] if v.strip() == "-" else ids(v)


def parse_reach(text: str, cmd: str) -> str | None:
    try:
        return _reach(text)
    except _Skip:
        return None


def _reach(text: str) -> str | None:
    secs = sections(text, R_HDR)
    bodies: list[tuple[str, list[str]]] = []
    edges = 0
    names: list[str] = []
    for k, (h, lines) in enumerate(secs):
        sets: dict[str, list[int]] = {}
        rows: dict[int, list[int]] = {}
        for line in lines:
            if m := R_SCAN.match(line):
                ids(m["v"])  # the live blocks are every block not in `dead`
            elif m := R_SET.match(line):
                sets[m["k"]] = blocklist(m["v"])
            elif m := R_ROW.match(line):
                rows[int(m["id"])] = blocklist(m["v"])
            elif not R_QUERY.match(line):
                raise _Skip
        if not rows or h is None or len(rows) != int(h["n"]):
            continue  # not --matrix, or a filtered matrix
        pairs = [(a, b) for a, bs in rows.items() for b in bs]
        if any(b not in rows for _, b in pairs):
            raise _Skip
        if not pairs or len(pairs) > MAX_REACH_EDGES:
            continue
        pre = f"f{k + 1}_" if len(secs) > 1 else ""
        dead = set(sets.get("dead", []))
        cyc = set(sets.get("cycle", []))
        body = []
        for b in rows:
            cls = ["dim"] if b in dead else ["hl"] if b in cyc else []
            lines_ = ["not reached from entry"] if b in dead else ["on a cycle"] if b in cyc else []
            body.append(node(pre + f"B{b}", f"B{b}" + (" (EXIT)" if b == 0 else ""), lines_, cls))
        body += [edge(pre + f"B{a}", pre + f"B{b}", ["weak"]) for a, b in pairs]
        edges += len(pairs)
        names.append(h["fn"])
        bodies.append((f"{h['fn']}: edge A -> B means isReachable(A, B), not a CFG edge", body))
    if not bodies:
        return None
    return render("reach_" + gname(*names), bodies, edges)


# --------------------------------------------------------------------------
# p04_slice: control dependence (cd, --dump) and dominance frontiers (df)
# --------------------------------------------------------------------------
S_HDR = D_HDR
S_CD = re.compile(r"^B(?P<id>\d+)\s+depends on: (?P<v>.*)$")
S_CD_DEP = re.compile(r"B(?P<id>\d+) \[(?P<term>\w+): (?P<cond>.*?)\](?:, (?=B\d+ \[)|$)")
S_DF = re.compile(r"^DF\(B(?P<id>\d+)\)\s*= \{(?P<v>[^}]*)\}$")
S_ASSIGN = re.compile(r"^blocks assigning '(?P<var>[^']+)': (?P<v>.*)$")
S_PHI = re.compile(r"^IDFCalculatorBase phi blocks: (?P<v>.*)$")
S_CLOSURE = re.compile(r"^closure of DF by hand:\s+(?P<v>.*)$")
S_DUMP_HDR = re.compile(r"^Control dependencies \(Node#,Dependency#\):$")
S_PAIR = re.compile(r"^\((?P<n>\d+),(?P<d>\d+)\)$")


def parse_slice(text: str, cmd: str) -> str | None:
    try:
        return _slice(text)
    except _Skip:
        return None


def _slice(text: str) -> str | None:
    secs = sections(text, S_HDR)
    if len(secs) > 1 and any(h is None for h, _ in secs):
        raise _Skip
    bodies: list[tuple[str, list[str]]] = []
    edges = 0
    names: list[str] = []
    kind = ""
    for k, (h, lines) in enumerate(secs):
        cd: dict[int, list[tuple[int, str, str]]] = {}  # block -> [(controller, terminator, condition)]
        pairs: list[tuple[int, int]] = []  # (dependent, controller) from the Clang dump
        df: dict[int, list[int]] = {}
        assign: list[int] | None = None
        var = ""
        phi: list[int] | None = None
        for line in lines:
            if m := S_CD.match(line):
                v = m["v"].strip()
                deps = []
                if v != "-":
                    found = list(S_CD_DEP.finditer(v))
                    if not found or found[0].start() != 0 or found[-1].end() != len(v):
                        raise _Skip
                    deps = [(int(d["id"]), d["term"], d["cond"]) for d in found]
                cd[int(m["id"])] = deps
            elif S_DUMP_HDR.match(line):
                continue
            elif m := S_PAIR.match(line):
                pairs.append((int(m["n"]), int(m["d"])))
            elif m := S_DF.match(line):
                df[int(m["id"])] = [] if not m["v"].strip() else ids(m["v"])
            elif m := S_ASSIGN.match(line):
                var, assign = m["var"], blocklist(m["v"])
            elif m := S_PHI.match(line):
                phi = blocklist(m["v"])
            elif S_CLOSURE.match(line):
                continue
            else:
                raise _Skip
        pre = f"f{k + 1}_" if len(secs) > 1 else ""
        fn = h["fn"] if h else ""
        if cd or pairs:
            if cd and h is not None and len(cd) != int(h["n"]) - 1:
                raise _Skip  # ENTRY is not listed; any other missing block means a filtered listing
            body, n = cdg_body(cd, pairs, pre)
            kind = kind or "cdg"
            title = f"{fn}: edge A -> B means B is control dependent on A" if fn else "edge A -> B means B is control dependent on A"
        elif df:
            if h is None or len(df) != int(h["n"]):
                raise _Skip
            body, n = df_body(df, assign, var, phi, pre)
            kind = kind or "df"
            title = f"{fn}: edge A -> B means B is in the dominance frontier of A"
        else:
            raise _Skip
        edges += n
        names.append(fn)
        bodies.append((title, body))
    return render(kind + "_" + gname(*(n for n in names if n)), bodies, edges)


def solo_cluster(pre: str, label: str, items: list[tuple[str, str]]) -> list[str]:
    """Blocks that take part in no edge, in one column (invisible edges stack them) instead of one wide row."""
    body = [stmt for _, stmt in items]
    body += [edge(a, b, extra=["style=invis"]) for (a, _), (b, _) in zip(items, items[1:])]
    return cluster(f"{pre}solo", label, body) if items else []


def cdg_body(cd: dict[int, list[tuple[int, str, str]]], pairs: list[tuple[int, int]], pre: str) -> tuple[list[str], int]:
    es: list[tuple[int, int]] = []  # (controller, dependent)
    cond: dict[int, str] = {}
    for b, deps in cd.items():
        for d, term, c in deps:
            es.append((d, b))
            cond.setdefault(d, f"{term}: {c}")
    for n, d in pairs:
        if (d, n) not in es:
            es.append((d, n))
    blocks = sorted(set(cd) | {x for e in es for x in e})
    controllers = {d for d, _ in es}
    dependents = {b for _, b in es}
    body: list[str] = []
    solo: list[tuple[str, str]] = []
    for b in blocks:
        lines = [cond[b]] if b in cond else []
        nid = pre + f"B{b}"
        stmt = node(nid, f"B{b}" + (" (EXIT)" if b == 0 else ""), lines, ["cond"] if b in controllers else [])
        if b in controllers or b in dependents:
            body.append(stmt)
        else:
            solo.append((nid, stmt))
    body += [edge(pre + f"B{d}", pre + f"B{b}") for d, b in es]
    return solo_cluster(pre, "no dependences either way", solo) + body, len(es)


def df_body(df: dict[int, list[int]], assign: list[int] | None, var: str, phi: list[int] | None, pre: str) -> tuple[list[str], int]:
    es = [(a, b) for a, bs in sorted(df.items()) for b in bs]
    if any(b not in df for _, b in es):
        raise _Skip
    touched = {x for e in es for x in e}
    body: list[str] = []
    solo: list[tuple[str, str]] = []
    for b in sorted(df):
        lines = []
        cls = []
        if assign and b in assign:
            lines.append(f"assigns {var}")
        if phi and b in phi:
            lines.append(f"phi for {var}")
            cls.append("hl")
        nid = pre + f"B{b}"
        stmt = node(nid, f"B{b}" + (" (EXIT)" if b == 0 else ""), lines, cls)
        if b in touched:
            body.append(stmt)
        else:
            solo.append((nid, stmt))
    body += [edge(pre + f"B{a}", pre + f"B{b}", ["weak"]) for a, b in es]
    return solo_cluster(pre, "empty frontier, in no frontier", solo) + body, len(es)


# --------------------------------------------------------------------------
PARSERS = [
    (re.compile(r"\bp03_elems\b"), parse_elems),
    (re.compile(r"\bp03_eh\b"), parse_eh),
    (re.compile(r"\bp04_dom\b"), parse_dom),
    (re.compile(r"\bp04_graphs\b"), parse_graphs),
    (re.compile(r"\bp04_reach\b"), parse_reach),
    (re.compile(r"\bp04_slice\b"), parse_slice),
]
