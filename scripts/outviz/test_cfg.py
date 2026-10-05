"""Tests for outviz.cfg: `python3 -m unittest discover -s scripts/outviz` (or run this file).

The samples are real outputs of this lab (docs/part_1, part_2, part_3), trimmed.
"""
import re
import subprocess
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from outviz import cfg  # noqa: E402

SIGN = """int sign(int x)
 [B4 (ENTRY)]
   Succs (1): B3

 [B1]
   1: 1
   2: return [B1.1];
   Preds (1): B3
   Succs (1): B0

 [B2]
   1: 1
   2: -[B2.1]
   3: return [B2.2];
   Preds (1): B3
   Succs (1): B0

 [B3]
   1: x
   2: [B3.1] (ImplicitCastExpr, LValueToRValue, int)
   3: 0
   4: [B3.2] < [B3.3]
   T: if [B3.4]
   Preds (1): B4
   Succs (2): B2 B1

 [B0 (EXIT)]
   Preds (2): B1 B2
"""

SWITCH_TRY = """int eh(int n)
 [B6 (ENTRY)]
   Succs (1): B5

 [B1]
   1: n
   Preds (2): B4(Unreachable) B2
   Succs (1): B0

 [B2]
   T: try ...
   Preds (1): B6
   Succs (2): B3 B0

 [B3]
  catch (int e):
   1: catch (int e) {
[B3.4]}
   2: e
   Preds (1): B2
   Succs (1): B0

 [B4 (NORETURN)]
   1: die()
   Preds (1): B5
   Succs (1): B0

 [B5]
   T: switch [B5.1]
   Preds (1): B6
   Succs (3): B4 B2 B3

 [B0 (EXIT)]
   Preds (3): B1 B3 B2
"""

PRUNED = """int pruned_while(int n)
 [B3 (ENTRY)]
   Succs (1): B2

 [B1]
   Preds (1): B2
   Succs (1): B0

 [B2]
   1: 1
   T: while [B2.1]
   Preds (2): B3 B1
   Succs (2): B2 NULL

 [B0 (EXIT)]
   Preds (1): B1
"""

SHAPE = """int sum(int n)
  B6   ENTRY             0 elems                           -> B5
  B1                     3 elems                           -> B0
  B2                     2 elems                           -> B4
  B3                     4 elems                           -> B2
  B4                     5 elems  T: for (...; _; ...)     -> B3 B1
  B5                     4 elems                           -> B4
  B0   EXIT              0 elems                           -> -
"""

SLICE = """   Succs (1): B3

 [B1]
   1: n
   Preds (1): B3
   Succs (1): B0

 [B3]
   T: if [B3.5]
   Preds (1): B4
   Succs (2): B2 B1
"""


def edges(dot: str) -> list[tuple[str, str, str]]:
    """(tail, head, attribute text) of every edge statement."""
    return re.findall(r'"([^"]+)" -> "([^"]+)"(?: \[([^\]]*)\])?;', dot)


def valid(dot: str) -> bool:
    return subprocess.run(["dot", "-Tsvg"], input=dot, capture_output=True, text=True).returncode == 0


class DumpCFG(unittest.TestCase):
    def test_branch_edges_and_classes(self):
        dot = cfg.parse_dumpcfg(SIGN)
        self.assertIn(("B3", "B2", 'label="T", class="t"'), edges(dot))
        self.assertIn(("B3", "B1", 'label="F", class="f"'), edges(dot))
        self.assertIn('"B4" [label="B4 (ENTRY)", class="entry"]', dot)
        self.assertIn('class="cond"', dot.split('"B3" [')[1].split("\n")[0])
        self.assertNotIn("back", dot)
        self.assertTrue(valid(dot))

    def test_switch_try_unreachable_and_multiline_element(self):
        dot = cfg.parse_dumpcfg(SWITCH_TRY)
        e = edges(dot)
        self.assertIn(("B5", "B4", 'label="0"'), e)  # switch edges carry the successor index
        self.assertIn(("B5", "B3", 'label="2"'), e)
        self.assertIn(("B2", "B3", 'label="catch (int e)", class="eh"'), e)
        self.assertIn(("B4", "B1", 'label="unreachable", class="weak"'), e)  # only B1's Preds line shows it
        self.assertIn("1: catch (int e) { [B3.4]}", dot)  # a statement that spans lines is one element
        self.assertTrue(valid(dot))

    def test_null_successor_and_back_edge(self):
        dot = cfg.parse_dumpcfg(PRUNED)
        e = edges(dot)
        self.assertIn(("B2", "B2", 'label="T", class="t back"'), e)
        self.assertIn(("B2", "null_B2_1", 'label="F", class="f weak"'), e)
        self.assertIn('"null_B2_1" [label="NULL", class="dim"', dot)
        self.assertTrue(valid(dot))

    def test_slice_has_stubs_and_no_back_edges(self):
        dot = cfg.parse_dumpcfg(SLICE)
        self.assertIn('"B0" [label="B0", class="dim"', dot)  # named as a successor, not part of the output
        self.assertIn(("B3", "B2", 'label="T", class="t"'), edges(dot))
        self.assertEqual(len(edges(dot)), 3)  # the orphan `Succs (1): B3` line has no block, so no edge
        self.assertTrue(valid(dot))

    def test_nothing_to_draw(self):
        self.assertIsNone(cfg.parse_sniff("B1 -> B0\nsome text\n", "x"))
        self.assertIsNone(cfg.parse_dumpcfg(" [B1]\n   1: x\n   Succs (1): B0\n"))  # one block only
        self.assertIsNone(cfg.parse_sniff("17,20c17,21\n<    1: [B6.10] ? [B4.4]\n---\n", "diff a b"))

    def test_clusters_for_several_functions(self):
        dot = cfg.parse_dumpcfg(SIGN + "\n" + PRUNED)
        self.assertIn("subgraph cluster_f1", dot)
        self.assertIn('"f2_B2" -> "f2_B2"', dot)
        self.assertTrue(valid(dot))


class Shape(unittest.TestCase):
    def test_cfgshape(self):
        dot = cfg.parse_cfgshape(SHAPE)
        self.assertIn(("B2", "B4", 'class="back"'), edges(dot))
        self.assertIn(("B4", "B3", 'label="T", class="t"'), edges(dot))
        self.assertIn("5 elems", dot)
        self.assertTrue(valid(dot))


class Trees(unittest.TestCase):
    DOM = "Immediate dominance tree (Node#,IDom#):\n(0,3)\n(1,3)\n(2,3)\n(3,4)\n(4,4)\n"

    def test_dominator_tree(self):
        dot = cfg.parse_domtree(self.DOM)
        self.assertEqual({(a, b) for a, b, _ in edges(dot)}, {("B3", "B0"), ("B3", "B1"), ("B3", "B2"), ("B4", "B3")})
        self.assertIn('"B4" [label="B4", class="entry"]', dot)
        self.assertTrue(valid(dot))

    def test_post_dominators_root_is_exit_and_control_dependence_has_self_loop(self):
        self.assertIn('class="exit"', cfg.parse_domtree("Immediate post dominance tree (Node#,IDom#):\n(0,0)\n(1,0)\n(2,0)\n"))
        cd = cfg.parse_domtree("Control dependencies (Node#,Dependency#):\n(2,4)\n(3,4)\n(4,4)\n")
        self.assertIn(("B4", "B4", ""), edges(cd))

    def test_too_small_to_draw(self):
        self.assertIsNone(cfg.parse_domtree("Immediate dominance tree (Node#,IDom#):\n(0,2)\n"))
        self.assertIsNone(cfg.parse_domtree("(1,2)\n(3,4)\n", "no checker named here"))


class DotText(unittest.TestCase):
    VIEW = ('digraph unnamed {\n\n\tN1 [shape=record,label="{ [B0 (EXIT)]\\l}"];\n'
            '\tN2 [shape=record,label="{ [B3]\\l  1: x\\l   T: if [B3.1]\\l}"];\n\tN2 -> N1;\n\tN2 -> N1;\n'
            '\tN5 [shape=record,label="{ [B4 (ENTRY)]\\l}"];\n\tN5 -> N2;\n}\n')

    def test_viewcfg_dot(self):
        dot = cfg.parse_sniff(self.VIEW, "awk ... out/dot/p01_hello.1.sign.dot")
        self.assertIn(("B3", "B0", 'label="T", class="t"'), edges(dot))  # file order is Succs order
        self.assertIn('class="entry"', dot)
        self.assertTrue(valid(dot))

    def test_edge_only_dot_labels_are_kept(self):
        out = '  B1 -> B0;\n  B3 -> B2 [label="T" style=dashed color=gray];\n  B3 -> B1 [label="F" color="#c62828"];\n'
        dot = cfg.parse_sniff(out, "build/bin/p02_export x.cpp --format=dot | grep -E ' -> '")
        self.assertIn(("B3", "B2", 'label="T unreachable", class="t weak"'), edges(dot))
        self.assertNotIn("dim", dot)  # the blocks are real, just not described
        self.assertTrue(valid(dot))


if __name__ == "__main__":
    unittest.main()
