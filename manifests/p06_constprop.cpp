// Part 6.1 -- samples for the constant-propagation analysis (p06_constprop).

// Straight-line code: every value is a known constant.
int straight(int a) {
  int x = 1;
  int y = x + 2;
  return y * 2;
}

// Both branches assign the same constant: the join keeps it.
int same_const(int a) {
  int x;
  if (a)
    x = 5;
  else
    x = 5;
  return x;
}

// Different constants: the join is Top.
int diverge(int a) {
  int x;
  if (a)
    x = 1;
  else
    x = 2;
  return x;
}

// A loop: i is never touched, s changes every iteration.
int loop(int n) {
  int i = 0;
  int s = 0;
  while (n) {
    s += 2;
    n = n - 1;
  }
  return s + i;
}
