// Part 6.2 -- what AdornedCFG adds, and what runDataflowAnalysis returns (p06_adorned).

// Straight line: every block is reachable and has a state.
int simple(int a) {
  int x = a;
  return x;
}

// `?:` splits one expression across blocks: the condition's value is produced in
// one block and consumed in the join block.
int ternary(bool a, int b, int c) {
  return a ? b : c;
}

// A statement after `return` gets a block, but no path leads to it.
int dead_tail(int a) {
  return a;
  a = 5;
}

// `while (true)` has no exit edge: the code after it is unreachable.
int endless(int a) {
  while (true)
    a++;
  return a;
}

// A loop, for the visit-count experiments.
int counted(int n) {
  int s = 0;
  while (n) {
    s += n;
    n--;
  }
  return s;
}

template <class T> T tmpl(T x) { return x; }
int use_tmpl() { return tmpl(1); }
