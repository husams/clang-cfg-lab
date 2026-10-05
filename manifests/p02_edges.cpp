// Part 2.5 / 2.6 -- one function per successor-ordering rule, plus the
// constructs that produce pruned (unreachable) edges.
int g(int);

// if: succs = [then, else]
int if_order(int a) {
  if (a)
    return 1;
  else
    return 2;
}

// ?:  the condition block has succs = [lhs, rhs]
int cond_order(int a) { return a ? g(1) : g(2); }

// &&  and ||: succs = [rhs-evaluated-next, short-circuit-target]
int and_order(int a, int b) { return a && b; }
int or_order(int a, int b) { return a || b; }

// loops: the condition block lists [body, exit]
int loop_order(int n) {
  int s = 0;
  while (n > 0)
    s += n--;
  return s;
}

// switch: cases first (source order), default last
int switch_order(int k) {
  switch (k) {
  case 1: return 10;
  case 2: return 20;
  default: return 0;
  }
}

// A switch over an enum that covers every enumerator: the implicit
// "no case matched" edge is unreachable
enum Color { Red, Green };
int covered(Color c) {
  switch (c) {
  case Red: return 1;
  case Green: return 2;
  }
  return 3;
}

// Pruned edges: conditions the CFG builder can evaluate at compile time
int pruned_if(int n) {
  if (0)
    n = 1;
  return n;
}

int pruned_while(int n) {
  while (1) {
    if (n > 3)
      break;
    ++n;
  }
  return n;
}

// try/catch: with AddEHEdges the call inside try gets an edge to the handler
int may_throw(int);
int eh(int n) {
  try {
    n = may_throw(n);
  } catch (...) {
    n = 0;
  }
  return n;
}
