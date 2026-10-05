// Part 6.3 -- loops for the interval analysis (p06_widen).

// i grows by one per iteration; the exit condition is unknown (n).
int unbounded(int n) {
  int i = 0;
  while (n) {
    i = i + 1;
    n = n - 1;
  }
  return i;
}

// The loop condition bounds i: with branch refinement the analysis can
// converge even without widening.
int count_up() {
  int i = 0;
  while (i < 10)
    i++;
  return i;
}

// No loop at all: widening and convergence checks never fire.
int straight() {
  int i = 0;
  i = i + 1;
  return i;
}
