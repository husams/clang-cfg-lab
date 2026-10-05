"""Graphs from the output of the Part 5-7 tools (p05_*, p06_*, p07_*).

Most of these tools print per-block facts or diagnostics with no edge in sight (liveness, constprop,
taint, optional, tu, combined, verify ...): the text names blocks but never says which block follows
which, so nothing is drawn for them (the CFG itself is drawn from the `CFG::dump` text in cfg.py).
The outputs below do encode a graph, and only what the text shows is drawn:

  p06_env          decl -> StorageLocation -> Value (+ record fields, pointees, properties); a Value that
                   is the contents of two or more places is highlighted (they share one object)
  p06_log          the flow-condition definition chain ("(V6 = (V4 & V5))" ...) as a DAG of atoms
  p07_ctx          `--explain`: caller -> callee, one edge per call that pushCall can or cannot take
  p07_persist      `--dump-tables`: the persisted TSV rows are a CFG (entry, exit, T/F, back edges)
  p05_lifetime     `--facts`: loans -> origins and the origin-to-origin flows, with the step order

A listing that cannot be drawn faithfully (a block named as a successor but absent, a second
snapshot, several flow conditions) returns None, like an output with one node or no edge. An output
that holds several functions (or several runs) gets one cluster per function (run).

Persisted tables only say "conditional terminator"; with exactly two successors, successor 0 is drawn
as the T edge and successor 1 as the F edge (the CFG convention), which a two-way `switch` would share.
"""

from __future__ import annotations

import re
import shlex
from typing import Callable

WIDE_PX = 780  # estimated width of the widest rank above which orient() lays a graph out left to right
MAX_W = 50  # label lines longer than this are cut with an ellipsis; the full text goes in the tooltip

_KEYWORDS = {"node", "edge", "graph", "digraph", "subgraph", "strict"}
_ENTITY = re.compile(r"&(?=#?\w+;)")  # dot reads "&amp;" inside labels; keep a literal "&name;" literal


# --------------------------------------------------------------------------
# a small dot writer
# --------------------------------------------------------------------------
def _esc(text: str) -> str:
    """The inside of a dot string literal."""
    text = text.replace("\\", "\\\\").replace('"', '\\"').replace("\t", " ").replace("\n", " ")
    return _ENTITY.sub("&amp;", text)


def _q(text: str) -> str:
    return '"' + _esc(text) + '"'


def _ident(name: str) -> str:
    if re.fullmatch(r"[A-Za-z_]\w*", name) and name.lower() not in _KEYWORDS:
        return name
    return _q(name)


def _clip(text: str, width: int = MAX_W) -> str:
    text = text.replace("\t", " ")
    return text if len(text) <= width else text[: width - 1].rstrip() + "…"


def _gname(base: str) -> str:
    name = re.sub(r"\W+", "_", base).strip("_") or "graph"
    return "g_" + name if name[0].isdigit() else name


class _Graph:
    """Nodes, edges and clusters in creation order; node() and edge() are idempotent upserts."""

    def __init__(self, name: str, title: str | None = None, ordering: bool = False):
        self.name = _gname(name)
        self.title = title
        self.ordering = ordering
        self.nodes: dict[str, dict] = {}
        self.edges: list[dict] = []
        self.clusters: dict[str, str] = {}
        self.rankdir = "TB"

    def cluster(self, cid: str, label: str) -> None:
        self.clusters[cid] = label

    def node(self, nid: str, lines: list[str], cls: str = "", left: bool = False, tip: str | None = None, cluster: str | None = None) -> None:
        n = self.nodes.setdefault(nid, {"cluster": cluster, "cls": ""})
        clipped = [_clip(x) for x in lines]
        if tip is None and clipped != lines:
            tip = "\n".join(lines)
        classes = " ".join(dict.fromkeys((n["cls"] + " " + cls).split()))  # a class once given (hl) is kept
        n.update(lines=clipped, cls=classes, left=left, tip=tip)

    def ensure(self, nid: str, lines: list[str], cls: str = "", cluster: str | None = None) -> None:
        if nid not in self.nodes:
            self.node(nid, lines, cls, cluster=cluster)

    def add_class(self, nid: str, cls: str) -> None:
        n = self.nodes[nid]
        if cls not in n["cls"].split():
            n["cls"] = (n["cls"] + " " + cls).strip()

    def edge(self, a: str, b: str, label: str = "", cls: str = "", tip: str | None = None) -> bool:
        """Add an edge unless an identical one exists; True when it was added."""
        e = {"a": a, "b": b, "label": label, "cls": cls, "tip": tip}
        if any(x["a"] == a and x["b"] == b and x["label"] == label and x["cls"] == cls for x in self.edges):
            return False
        self.edges.append(e)
        return True

    def drawable(self) -> bool:
        return len(self.nodes) >= 2 and bool(self.edges)

    def orient(self, budget: int = WIDE_PX) -> None:
        """TB, unless the widest rank would not fit the page: then LR, where the fan-out stacks vertically.

        The estimate is the label width of the nodes of each rank (a rank = longest path from a root, the
        edges that close a cycle dropped); dot lays a wide rank out about a third wider than this.
        """
        if self.clusters:
            return
        succ: dict[str, list[str]] = {n: [] for n in self.nodes}
        for e in self.edges:
            if e["a"] in succ and e["b"] in succ and e["a"] != e["b"] and e["b"] not in succ[e["a"]]:
                succ[e["a"]].append(e["b"])
        color: dict[str, int] = {}
        acyclic: dict[str, list[str]] = {n: [] for n in succ}  # without the DFS back edges
        for root in succ:
            if root in color:
                continue
            color[root] = 1
            stack = [(root, iter(succ[root]))]
            while stack:
                node, it = stack[-1]
                for nxt in it:
                    if color.get(nxt) == 1:
                        continue
                    acyclic[node].append(nxt)
                    if nxt not in color:
                        color[nxt] = 1
                        stack.append((nxt, iter(succ[nxt])))
                        break
                else:
                    color[node] = 2
                    stack.pop()
        indeg = {n: 0 for n in succ}
        for bs in acyclic.values():
            for b_ in bs:
                indeg[b_] += 1
        order = [n for n in succ if indeg[n] == 0]
        rank = {n: 0 for n in succ}
        for node in order:
            for nxt in acyclic[node]:
                rank[nxt] = max(rank[nxt], rank[node] + 1)
                indeg[nxt] -= 1
                if indeg[nxt] == 0:
                    order.append(nxt)
        used: dict[int, float] = {}
        for n, v in self.nodes.items():
            used[rank[n]] = used.get(rank[n], 0) + max((len(x) for x in v["lines"]), default=1) * 7.2 + 23 + 34
        if max(used.values(), default=0) > budget:
            self.rankdir = "LR"

    def _node_stmt(self, nid: str, n: dict, indent: str) -> str:
        if n["left"]:  # the first line (the block name) centered, the rest left-justified
            label = _esc(n["lines"][0]) + "\\n" + "".join(_esc(x) + "\\l" for x in n["lines"][1:])
        else:
            label = "\\n".join(_esc(x) for x in n["lines"])
        attrs = [f'label="{label}"']
        if n["cls"]:
            attrs.append(f"class={_q(n['cls'])}")
        if n["tip"]:
            attrs.append(f"tooltip={_q(n['tip'])}")
        return f"{indent}{_ident(nid)} [{', '.join(attrs)}];"

    def dot(self) -> str:
        for e in self.edges:  # an edge never names a node that was not declared
            for end in (e["a"], e["b"]):
                self.nodes.setdefault(end, {"cluster": None, "lines": [end], "cls": "", "left": False, "tip": None})
        out = [f"digraph {self.name} {{", f"  rankdir={self.rankdir};"]
        if self.ordering:
            out.append("  ordering=out;")
        if self.title:
            out.append(f"  label={_q(_clip(self.title, 80))}; labelloc=t; labeljust=l;")
        for cid, label in self.clusters.items():
            out.append(f"  subgraph {_ident(cid)} {{")
            out.append(f"    label={_q(_clip(label, 80))}; labeljust=l;")
            out.append('    class="group";')
            out += [self._node_stmt(nid, n, "    ") for nid, n in self.nodes.items() if n["cluster"] == cid]
            out.append("  }")
        out += [self._node_stmt(nid, n, "  ") for nid, n in self.nodes.items() if n["cluster"] not in self.clusters]
        for e in self.edges:
            attrs = []
            if e["label"]:
                attrs.append(f"label={_q(e['label'])}")
            if e["cls"]:
                attrs.append(f"class={_q(e['cls'])}")
            if e["tip"]:
                attrs.append(f"tooltip={_q(e['tip'])}")
            out.append(f"  {_ident(e['a'])} -> {_ident(e['b'])}" + (f" [{', '.join(attrs)}]" if attrs else "") + ";")
        out.append("}")
        return "\n".join(out) + "\n"


# --------------------------------------------------------------------------
# helpers on the command text
# --------------------------------------------------------------------------
def _tool(*names: str) -> re.Pattern:
    """A command that runs one of the tools: `build/bin/<tool>` or `scripts/run.sh <tool>`.

    The tool must be the program: `manifests/p06_env.cpp` as the argument of another tool is not a match.
    """
    return re.compile(r"(?:build/bin/|run\.sh\s+)(?:" + "|".join(map(re.escape, names)) + r")(?![\w.-])")


def _invocations(command: str, tool: str) -> list[list[str]]:
    """The arguments of every `<tool> ...` call in the command, in order (up to the next ; | & > or end of line)."""
    calls = []
    for m in _tool(tool).finditer(command):
        rest = re.split(r"[;|&>]|\n", command[m.end():], maxsplit=1)[0]
        try:
            calls.append(shlex.split(rest))
        except ValueError:
            calls.append(rest.split())
    return calls


def _options(args: list[str]) -> list[str]:
    """The tool options of one call, without the source file and the options that only select the function or mode."""
    return [a for a in args if a.startswith("-") and not a.startswith(("--func=", "--mode=")) and a != "--"]


def _filtered(command: str) -> bool:
    return bool(re.search(r"\|\s*(?:grep|sed|head|tail|awk)\b", command))


# --------------------------------------------------------------------------
# p06_env
# --------------------------------------------------------------------------
_SECTION = re.compile(r"^== (?P<name>.*?)\s*$")
_AT = re.compile(r"^\s+at `.*`:\s*$")
_AFTER = re.compile(r"^after setValue\(.*\):$")
_LOC_LINE = re.compile(r"^(?P<name>[.$]?\w*): (?P<loc>(?:Record|Scalar) .*|\(null location\))$")
_CALL_LINE = re.compile(r"^(?P<name>\w+) = (?P<val>\S.*)$")
_RET_LINE = re.compile(r"^value of the returned expression: (?P<val>.*)$")
_CV_LINE = re.compile(r"^createValue\((?P<ty>.*)\) = (?P<val>.*)$")
_CO_LINE = re.compile(r"^createObject\((?P<ty>.*)\): (?P<loc>.*)$")
_PTEE_LINE = re.compile(r"^pointee location: (?P<loc>.*)$")
_LOC_TEXT = re.compile(r"^(?P<kind>Record|Scalar) (?P<id>L\d+) (?P<rest>.*)$")
_VAL_NAME = re.compile(r"^([A-Za-z]+)#(\d+)$")


def _parse_value(text: str) -> dict | None:
    """`Integer#3`, `AtomicBool#2 formula=V2`, `Pointer#1 -> L2`, `Integer#3 {checked=FormulaBool#4}`, `(no value)`."""
    text = text.strip()
    if text == "(no value)":
        return {"none": True}
    props: list[tuple[str, str]] = []
    while True:
        m = re.search(r" \{([^={}]+)=([A-Za-z]+#\d+)\}$", text)
        if not m:
            break
        props.insert(0, (m.group(1), m.group(2)))
        text = text[: m.start()]
    ptr = None
    m = re.search(r" -> (L\d+)$", text)
    if m:
        ptr, text = m.group(1), text[: m.start()]
    formula = None
    m = re.search(r" formula=(.*)$", text)
    if m:
        formula, text = m.group(1), text[: m.start()]
    m = _VAL_NAME.match(text)
    if not m:
        return None
    return {"kind": m.group(1), "id": m.group(2), "formula": formula, "ptr": ptr, "props": props}


class _Env:
    """One `== name` section of p06_env: builds decl -> location -> value chains into the shared graph."""

    def __init__(self, g: _Graph, pfx: str, cluster: str | None, refs: dict[str, int]):
        self.g, self.p, self.c, self.refs = g, pfx, cluster, refs
        self.stack: list[tuple[int, str]] = []  # (indent, record location node) of the open records
        self.after = False  # the next location line is the state after setValue / setProperty
        self.snapshots = 0
        self.n = 0

    def uid(self, kind: str) -> str:
        self.n += 1
        return f"{self.p}{kind}{self.n}"

    def null(self, text: str) -> str:
        nid = self.uid("null")
        label = {"(null location)": ["null"], "(no value)": ["no", "value"]}.get(text, [text])
        self.g.node(nid, label, "data dim", tip=text, cluster=self.c)
        return nid

    def value(self, v: dict) -> str:
        nid = f"{self.p}v{v['id']}"
        lines = [f"{v['kind']}#{v['id']}"] + ([f"formula={v['formula']}"] if v["formula"] else [])
        self.g.node(nid, lines, "data", cluster=self.c)
        if v["ptr"]:
            stub = f"{self.p}{v['ptr']}"
            self.g.ensure(stub, [v["ptr"]], "data", self.c)
            self.g.edge(nid, stub, "pointee")
        for name, target in v["props"]:
            pid = f"{self.p}v{_VAL_NAME.match(target).group(2)}"
            self.g.ensure(pid, [target], "data", self.c)
            self.g.edge(nid, pid, name)
        return nid

    def refer(self, src: str, v: dict, label: str, cls: str = "") -> str:
        """Edge from a place (location, decl, expression) to the Value it holds; returns the value node."""
        if v.get("none"):
            dst = self.null("(no value)")
            self.g.edge(src, dst, label, "weak")
            return dst
        dst = self.value(v)
        if self.g.edge(src, dst, label, cls):
            self.refs[dst] = self.refs.get(dst, 0) + 1
        return dst

    def loc(self, text: str, label: str = "", cls: str = "") -> tuple[str, bool]:
        """Draw a location line (`Scalar L3 int -> Integer#3`, `Record L1 Point`, `(null location)`); (node, is_record)."""
        if text == "(null location)":
            return self.null(text), False
        m = _LOC_TEXT.match(text)
        if not m:
            raise ValueError(text)
        nid = f"{self.p}{m['id']}"
        if m["kind"] == "Record":
            self.g.node(nid, [m["id"], f"Record {m['rest']}"], "data", cluster=self.c)
            return nid, True
        ty, _, vtxt = m["rest"].partition(" -> ")
        self.g.node(nid, [m["id"], ty], "data", tip=text, cluster=self.c)
        v = _parse_value(vtxt)
        if v is None:
            raise ValueError(text)
        vid = self.refer(nid, v, label, cls)
        if cls and not v.get("none"):
            self.g.add_class(vid, "hl")
        return nid, False

    def feed(self, raw: str) -> bool:
        """One output line; False when the section cannot be drawn faithfully."""
        if not raw.strip():
            return True
        if _AT.match(raw):
            self.snapshots += 1
            return self.snapshots == 1
        indent, body = len(raw) - len(raw.lstrip()), raw.strip()
        g = self.g
        if _AFTER.match(body):
            self.after = True
            return True
        if m := _CV_LINE.match(body):
            cv = f"{self.p}createValue"
            g.node(cv, ["createValue"], "api", cluster=self.c)
            if m["val"] == "nullptr":
                g.edge(cv, self.null("nullptr"), m["ty"], "weak")
            elif v := _parse_value(m["val"]):
                g.edge(cv, self.value(v), m["ty"])
            return True
        if m := _CO_LINE.match(body):
            co = f"{self.p}createObject"
            g.node(co, ["createObject"], "api", cluster=self.c)
            lid, _ = self.loc(m["loc"])
            g.edge(co, lid, m["ty"])
            return True
        if m := _PTEE_LINE.match(body):
            self.loc(m["loc"])
            return True
        if m := _RET_LINE.match(body):
            v = _parse_value(m["val"])
            if v is None:
                return True
            ret = f"{self.p}ret"
            g.node(ret, ["return", "expr"], "api", cluster=self.c)
            self.refer(ret, v, "")
            return True
        if m := _LOC_LINE.match(body):
            name = m["name"]
            if self.after and name.startswith("$"):
                self.after = False
                self.loc(m["loc"], "after setValue", "hl")
                return True
            while self.stack and self.stack[-1][0] >= indent:
                self.stack.pop()
            if name[:1] in ".$":
                if not self.stack:  # a field line whose record line is not part of the output
                    return True
                lid, rec = self.loc(m["loc"])
                g.edge(self.stack[-1][1], lid, name)
            else:
                lid, rec = self.loc(m["loc"])
                decl = f"{self.p}d_{name or 'unnamed'}"
                g.node(decl, [name or "(unnamed)"], "api", tip=body, cluster=self.c)
                g.edge(decl, lid, "", "weak" if lid.startswith(f"{self.p}null") else "")
            if rec:
                self.stack.append((indent, lid))
            return True
        if m := _CALL_LINE.match(body):
            v = _parse_value(m["val"])
            if v is not None:
                decl = f"{self.p}d_{m['name']}"
                g.node(decl, [m["name"]], "api", tip=body, cluster=self.c)
                self.refer(decl, v, "")
            return True
        return True


def env_graph(output: str, command: str) -> str | None:
    sections: list[tuple[str | None, list[str]]] = []
    for line in output.splitlines():
        if m := _SECTION.match(line):
            sections.append((m["name"], []))
        else:
            if not sections:
                sections.append((None, []))
            sections[-1][1].append(line)
    if not sections:
        return None
    calls = _invocations(command, "p06_env")
    multi = len(sections) > 1
    first = sections[0][0] or "listing"
    base = "create" if first.startswith("createValue") else first
    extra = [_options(c) for c in calls] if len(calls) == len(sections) else []
    suffix = "_".join(re.sub(r"\W+", "_", o.lstrip("-")) for o in (extra[0] if extra and not multi else []))
    g = _Graph("env_" + (base if not multi else f"{base}_{len(sections)}") + (("_" + suffix) if suffix else "") + ("_filtered" if _filtered(command) else ""))
    refs: dict[str, int] = {}
    try:
        for k, (name, lines) in enumerate(sections, 1):
            cluster = None
            if multi:
                cluster = f"cluster_s{k}"
                opts = " ".join(extra[k - 1]) if extra else ""
                g.cluster(cluster, f"{name or 'run ' + str(k)}  {opts or '(defaults)'}")
            env = _Env(g, f"s{k}_" if multi else "", cluster, refs)
            for line in lines:
                if not env.feed(line):
                    return None
    except ValueError:
        return None
    for vid, n in refs.items():  # a Value held by two or more places is one shared object
        if n >= 2:
            g.add_class(vid, "hl")
    g.orient()
    return g.dot() if g.drawable() else None


# --------------------------------------------------------------------------
# p06_log: the flow-condition definition chain
# --------------------------------------------------------------------------
_FC_HEAD = "Flow condition constraints before simplification:"
_FC_DEF = re.compile(r"^\((?P<lhs>V\d+) = (?P<rhs>.*)\)$")
_FC_ATOM = re.compile(r"(!?)(V\d+)")


def flowcond_graph(output: str, command: str) -> str | None:
    lines = output.splitlines()
    heads = [i for i, line in enumerate(lines) if line.strip() == _FC_HEAD]
    tokens = [m[1] for line in lines if (m := re.match(r"^Flow condition token: (V\d+)\s*$", line))]
    if len(heads) != 1 or len(tokens) > 1:
        return None  # several states in one output: one graph cannot show them faithfully
    token = tokens[0] if tokens else None
    true_atoms = next((m[1] for line in lines if (m := re.match(r"^True atoms: \((.*)\)\s*$", line))), "")
    defs: dict[str, str] = {}
    asserted: set[str] = set()
    other: list[str] = []
    for line in lines[heads[0] + 1:]:
        line = line.strip()
        if re.fullmatch(r"V\d+", line):
            asserted.add(line)
        elif m := _FC_DEF.match(line):
            defs[m["lhs"]] = m["rhs"]
        elif line.startswith("(") and line.endswith(")"):
            other.append(line)
        else:
            break
    g = _Graph("flowcond_" + (token or "chain"))
    atoms: list[str] = []
    for text in [*defs, *defs.values(), *asserted, *other, *([token] if token else [])]:
        for m in _FC_ATOM.finditer(text):
            if m[2] not in atoms:
                atoms.append(m[2])
    # definitions first so the chain reads top-down from the token
    for a in sorted(atoms, key=lambda x: int(x[1:]), reverse=True):
        lines_ = [a + (" (token)" if a == token else "")]
        if a in defs:
            rhs = defs[a]
            lines_.append("= " + (rhs[1:-1] if rhs.startswith("(") and rhs.endswith(")") and rhs.count("(") == 1 else rhs))
        if a in asserted:
            lines_.append("asserted")
        tip = f"{a}" + (f" = {defs[a]}" if a in defs else "") + (" (asserted)" if a in asserted else "")
        if re.search(rf"\b{a}\b", true_atoms):
            tip += "; known true"
        g.node(a, lines_, "data hl" if a == token else "data", tip=tip)
    for a, rhs in defs.items():
        for m in _FC_ATOM.finditer(rhs):
            g.edge(a, m[2], "!" if m[1] else "")
    for k, text in enumerate(other, 1):
        nid = f"c{k}"
        g.node(nid, [text], "note")
        for m in _FC_ATOM.finditer(text):
            g.edge(nid, m[2], "!" if m[1] else "", "weak")
    return g.dot() if g.drawable() else None


# --------------------------------------------------------------------------
# p07_ctx --explain: the calls pushCall can and cannot descend into
# --------------------------------------------------------------------------
_CTX_HEAD = re.compile(r"^(?P<fn>\w+):\s*$")
_CTX_CALL = re.compile(
    r"^\s+call (?P<callee>\w+) at line (?P<line>\d+): "
    r"(?:(?P<ok>descendable)(?: \[(?P<note>[^\]]*)\])?|not descended \((?P<why>.*)\))\s*$"
)


def ctx_graph(output: str, command: str) -> str | None:
    caller = None
    heads: list[str] = []
    calls: dict[tuple, dict] = {}
    for line in output.splitlines():
        if m := _CTX_CALL.match(line):
            if caller is None:
                return None
            key = (caller, m["callee"], m["ok"] is not None, m["note"] or m["why"] or "")
            calls.setdefault(key, {"lines": []})["lines"].append(m["line"])
        elif m := _CTX_HEAD.match(line):
            caller = m["fn"]
            heads.append(caller)
    if not calls:
        return None
    g = _Graph("ctx_calls")
    no_body = {c for (_, c, ok, why) in calls if not ok and why == "no body"}
    has_body = set(heads) | {c for (_, c, ok, why) in calls if ok or why != "no body"}
    for fn in dict.fromkeys(heads + [c for (_, c, _, _) in calls]):
        if fn in {c for (k, c, _, _) in calls} | {k for (k, _, _, _) in calls}:  # a function with no edge adds nothing
            g.node(fn, [fn], "api dim" if fn in no_body and fn not in has_body else "api")
    for (caller, callee, ok, why), c in calls.items():
        where = "L" + ",".join(c["lines"])  # source lines of the calls; the reason is only spelled out when it is not "no body"
        if ok:
            g.edge(caller, callee, where + (" global" if why else ""), "hl" if why else "",
                   tip=f"line {', '.join(c['lines'])}: descendable" + (f" [{why}]" if why else ""))
        else:
            short = why.split(" (")[0]
            g.edge(caller, callee, where + ("" if short == "no body" else " " + short), "weak",
                   tip=f"line {', '.join(c['lines'])}: not descended ({why})")
    g.orient()
    return g.dot() if g.drawable() else None


# --------------------------------------------------------------------------
# p07_persist --dump-tables: the persisted rows are a CFG
# --------------------------------------------------------------------------
_ROW = re.compile(r"^(?P<fn>\S+)\t(?P<id>\d+)\t(?P<n>\d+)\t(?P<term>\S+)\t(?P<succ>-|\d+(?:,\d+)*)\s*$")


def _back_edges(succ: dict[int, list[int]], entry: int) -> tuple[set[tuple[int, int]], set[int]]:
    """DFS from the entry: (edges into a block still on the stack, reachable blocks)."""
    color = {entry: 1}
    back: set[tuple[int, int]] = set()
    stack = [(entry, iter(succ[entry]))]
    while stack:
        b, it = stack[-1]
        for s in it:
            if color.get(s) == 1:
                back.add((b, s))
            elif s not in color:
                color[s] = 1
                stack.append((s, iter(succ[s])))
                break
        else:
            color[b] = 2
            stack.pop()
    return back, set(color)


def tables_graph(output: str, command: str) -> str | None:
    rows: dict[str, list[dict]] = {}
    for line in output.splitlines():
        if m := _ROW.match(line):
            rows.setdefault(m["fn"], []).append(m.groupdict())
    fns = {fn: r for fn, r in rows.items() if len(r) >= 2}
    if not fns:
        return None
    multi = len(fns) > 1
    g = _Graph("cfg_" + (next(iter(fns)) if not multi else "tables"), ordering=True)
    for k, (fn, rs) in enumerate(fns.items(), 1):
        pfx = f"{fn}_" if multi else ""
        if multi:
            g.cluster(f"cluster_f{k}", fn)
        else:
            g.title = f"{fn} (persisted tables)"
        cluster = f"cluster_f{k}" if multi else None
        info = {int(r["id"]): r for r in rs}
        succ = {b: ([] if r["succ"] == "-" else [int(x) for x in r["succ"].split(",")]) for b, r in info.items()}
        if any(s not in info for ss in succ.values() for s in ss):
            return None  # a successor with no row: the listing is partial
        preds = {s for ss in succ.values() for s in ss}
        entry = max((b for b in info if b not in preds), default=None)
        if entry is None:
            return None
        back, reach = _back_edges(succ, entry)
        for b, r in info.items():
            name = f"B{b}"
            if b == entry or (b == 0 and not succ[b]):
                flag = "ENTRY" if b == entry else "EXIT"
                g.node(pfx + name, [f"{name} ({flag})"], flag.lower(), cluster=cluster)
                continue
            n = int(r["n"])
            lines = [name, f"{n} elems"] + ([f"T: {r['term']}"] if r["term"] != "-" else [])
            cls = ("cond " if r["term"] != "-" else "") + ("" if b in reach else "dim")
            g.node(pfx + name, lines, cls.strip(), left=True, tip="\n".join(lines), cluster=cluster)
        for b, ss in succ.items():
            two = len(ss) == 2 and info[b]["term"] != "-"  # a conditional terminator: successor 0 is T, 1 is F
            for i, s in enumerate(ss):
                cls = []
                label = ""
                if two:
                    cls.append("t" if i == 0 else "f")
                    label = "T" if i == 0 else "F"
                if b not in reach:
                    cls.append("weak")
                if (b, s) in back:
                    cls.append("back")
                g.edge(f"{pfx}B{b}", f"{pfx}B{s}", label, " ".join(cls))
    return g.dot() if g.drawable() else None


# --------------------------------------------------------------------------
# p05_lifetime --facts: loans, origins and the flows between them
# --------------------------------------------------------------------------
_LT_BLOCK = re.compile(r"^\s*Block (B\d+):\s*$")
_LT_ISSUE = re.compile(r"^\s*Issue \((?P<loan>\d+) \(Path: (?P<path>.*?)\), ToOrigin: (?P<o>\d+) \((?P<od>.*)\)\)\s*$")
_LT_EXPIRE = re.compile(r"^\s*Expire \((?P<loan>\d+) \(Path: (?P<path>.*)\)\)\s*$")
_LT_USE = re.compile(r"^\s*Use \((?P<o>\d+) \((?P<od>.*)\), (?P<mode>\w+)\)\s*$")
_LT_ESC = re.compile(r"^\s*OriginEscapes \((?P<o>\d+) \((?P<od>.*)\)\)\s*$")
_LT_END = re.compile(r"^\s*(?P<k>Dest|Src):\s+(?P<o>\d+) \((?P<od>.*)\)\s*$")


def _origin_lines(oid: str, desc: str) -> list[str]:
    parts = re.split(r", (?=[A-Z][A-Za-z]* ?:)", desc)
    return [f"O{oid}"] + [re.sub(r"^(\w+) :", r"\1:", p) for p in parts]


def lifetime_graph(output: str, command: str) -> str | None:
    funcs: list[list] = []  # [name | None, events] per `Function:` header; events are (block, kind, data) in output order
    block = None
    flow: dict[str, tuple[str, str]] = {}

    def add(kind: str, data: tuple) -> None:
        if not funcs:  # a listing that starts after the `Function:` line (sed -n ...)
            funcs.append([None, []])
        funcs[-1][1].append((block, kind, data))

    for line in output.splitlines():
        if m := re.match(r"^Function: (\S+)\s*$", line):
            funcs.append([m[1], []])
            block = None
        elif m := _LT_BLOCK.match(line):
            block = m[1]
        elif re.match(r"^\s*OriginFlow:\s*$", line):
            flow = {}
        elif m := _LT_END.match(line):
            flow[m["k"]] = (m["o"], m["od"])
            if len(flow) == 2 and block:
                add("flow", (flow["Src"], flow["Dest"]))
                flow = {}
        elif block and (m := _LT_ISSUE.match(line)):
            add("issue", (m["loan"], m["path"], m["o"], m["od"]))
        elif block and (m := _LT_EXPIRE.match(line)):
            add("expire", (m["loan"], m["path"]))
        elif block and (m := _LT_USE.match(line)):
            add("use", (m["o"], m["od"], m["mode"]))
        elif block and (m := _LT_ESC.match(line)):
            add("escape", (m["o"], m["od"]))
    funcs = [f for f in funcs if f[1]]
    if not funcs:
        return None
    multi = len(funcs) > 1
    given = next((m[1] for a in _invocations(command, "p05_lifetime") for x in a if (m := re.match(r"--func=(.+)", x))), "")
    g = _Graph("lifetime_facts" + ("" if multi else "_" + (funcs[0][0] or given or "block")))

    for k, (fname, events) in enumerate(funcs, 1):
        pfx = f"f{k}_" if multi else ""
        cluster = None
        if multi:
            cluster = f"cluster_f{k}"
            g.cluster(cluster, fname or "facts")
        blocks = list(dict.fromkeys(b for b, _, _ in events))
        step: dict[str, int] = {}

        def origin(oid: str, desc: str) -> str:
            g.node(f"{pfx}O{oid}", _origin_lines(oid, desc), "data", cluster=cluster)
            return f"{pfx}O{oid}"

        def loan(lid: str, path: str) -> str:
            g.node(f"{pfx}L{lid}", [f"Loan {lid}", f"Path: {path}"], "data", cluster=cluster)
            return f"{pfx}L{lid}"

        def note(kind: str, tag: str, lines_: list[str]) -> str:
            nid = f"{pfx}{kind}{tag}".replace("·", "_")
            g.node(nid, lines_, "note", cluster=cluster)
            return nid

        for blk, kind, d in events:
            step[blk] = step.get(blk, 0) + 1
            tag = f"{blk}·{step[blk]}" if len(blocks) > 1 else str(step[blk])
            if kind == "issue":
                g.edge(loan(d[0], d[1]), origin(d[2], d[3]), f"{tag} issue")
            elif kind == "flow":
                (so, sd), (do, dd) = d
                g.edge(origin(so, sd), origin(do, dd), f"{tag} flow")
            elif kind == "expire":
                g.edge(note("x", tag, [f"{tag} expire", f"loan {d[0]}"]), loan(d[0], d[1]), "", "weak")
            elif kind == "use":
                g.edge(note("u", tag, [f"{tag} use", d[2]]), origin(d[0], d[1]), "", "weak")
            elif kind == "escape":
                g.edge(note("e", tag, [f"{tag} escapes"]), origin(d[0], d[1]), "", "weak")
    return g.dot() if g.drawable() else None


PARSERS: list[tuple[re.Pattern, Callable[[str, str], str | None]]] = [
    (_tool("p06_env"), env_graph),
    (_tool("p06_log"), flowcond_graph),
    (_tool("p07_ctx"), ctx_graph),
    (_tool("p07_persist"), tables_graph),
    (_tool("p05_lifetime"), lifetime_graph),
]
