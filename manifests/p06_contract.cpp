// Part 6.1 -- samples for tracing the DataflowAnalysis contract (p06_contract).

// if/else: the join point sees two predecessors.
int branch(int a) {
  int x = 0, y = 0;
  if (a)
    x = 1;
  else
    y = 2;
  return x + y;
}

// A loop: the framework revisits the loop header until nothing changes.
int loop(int n) {
  int s = 0;
  while (n) {
    s = s + n;
    n = n - 1;
  }
  return s;
}
