// Part 4 -- sample functions for the graph-algorithm tools. One construct each.
int g(int);
int h(int);

// Straight-line code: one block between ENTRY and EXIT.
int straight(int a) {
  int b = a + 1;
  return b * 2;
}

// A diamond: the smallest CFG with a join.
int diamond(int a) {
  int x;
  if (a > 0)
    x = 1;
  else
    x = 2;
  return x;
}

// A single natural loop.
int while_loop(int n) {
  int s = 0;
  while (n > 0) {
    s += n;
    --n;
  }
  return s;
}

// Two nested loops: two headers, two back edges.
int nested(int n, int m) {
  int s = 0;
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < m; ++j)
      s += i * j;
  return s;
}

// break and continue add edges out of and back into the loop.
int break_continue(int n) {
  int s = 0;
  for (int i = 0; i < n; ++i) {
    if (i == 3)
      continue;
    if (i == 7)
      break;
    s += g(i);
  }
  return s;
}

// Two entries into the loop {L0, L1}: the classic irreducible CFG.
int irreducible(int a) {
  if (a)
    goto L1;
L0:
  a = g(a);
L1:
  a = h(a);
  if (a > 0)
    goto L0;
  return a;
}

// Code after return lives in a block with no predecessors.
int dead_code(int a) {
  return a;
  a = g(a);
  return a + 1;
}

// A constant-false condition: the 'then' edge is pruned by default.
int pruned_if(int a) {
  if (0)
    a = g(a);
  return a;
}

// An infinite loop with one break: EXIT is reachable only through the break.
int forever(int a) {
  for (;;) {
    a = g(a);
    if (a > 100)
      break;
  }
  return a;
}

// Slicing target: the result depends on 'a' (through the branch) but not on 'b'.
int slice_demo(int a, int b) {
  int x = 0;
  int y = 0;
  if (a > 0)
    x = 1;
  else
    x = 2;
  if (b > 0)
    y = 1;
  y = y + x;
  return x;
}

// Dominance frontier / phi placement: v is assigned in the two arms and in the loop.
int phi_demo(int a, int n) {
  int v = 0;
  if (a)
    v = 1;
  else
    v = 2;
  a += 1; // a separate join block, so the loop header is a second phi site
  while (n > 0) {
    v = v + n;
    --n;
  }
  return v;
}

// Liveness / maybe-uninitialised target.
int solver_demo(int a, int n) {
  int x;
  int y = 0;
  if (a)
    x = 1;
  while (n > 0) {
    y = y + x;
    --n;
  }
  return y;
}

// A loop whose facts need several trips around it: values travel one 'if' per iteration.
int solver_chain(int n) {
  int a = 0;
  int b = 0;
  int c = 0;
  int d = 0;
  for (int i = 0; i < n; ++i) {
    if (i & 1)
      a = b;
    if (i & 2)
      b = c;
    if (i & 4)
      c = d;
    if (i & 8)
      d = i;
  }
  return a + b + c + d;
}
