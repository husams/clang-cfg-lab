// Part 2.9 -- inputs for the three exercises (cyclomatic complexity, loop
// counting, return-inside-loop finder). Expected answers in the comments.
int g(int);

// complexity 1: straight line
int straight(int a) { return a + 1; }

// complexity 2: one `if`
int one_if(int a) {
  if (a > 0)
    return 1;
  return 0;
}

// complexity 4: if + && + ||   (1 + if + && + ||)
int logic(int a, int b, int c) {
  if (a && b || c)
    return 1;
  return 0;
}

// complexity 4: switch with three cases and a default (1 + 3; `default` adds nothing)
int sw(int k) {
  switch (k) {
  case 1: return 10;
  case 2: return 20;
  case 3: return 30;
  default: return 0;
  }
}

// complexity 3: for + ?:  -> 1 loop, no return inside it
int loops1(int n) {
  int s = 0;
  for (int i = 0; i < n; ++i)
    s += (i % 2) ? i : -i;
  return s;
}

// complexity 6; 3 loops (while, do, for); two returns inside loops
int find(int n) {
  while (n > 100) {
    if (g(n) == 0)
      return n;           // return inside while
    n /= 2;
  }
  do {
    n += 3;
  } while (n < 10);
  for (int i = 0; i < n; ++i) {
    if (i == 7)
      return i;           // return inside for
  }
  return -1;              // outside any loop
}

// complexity 2; a loop written with goto: 0 loop statements, 1 back edge
int goto_loop(int n) {
  int s = 0;
again:
  if (n > 0) {
    s += n;
    --n;
    goto again;
  }
  return s;
}

// complexity 4; a return nested two loops deep
int nested(int n) {
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < i; ++j)
      if (i * j == 42)
        return i;         // return inside both loops
  return 0;
}
