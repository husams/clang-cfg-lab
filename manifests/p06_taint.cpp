// Part 6.6 -- taint tracking with property values (p06_taint).
int source();
int sanitize(int v);
void sink(int v);

void direct() {
  int x = source();
  sink(x);
}

// Copies share the Value, so they share the property.
void copy() {
  int x = source();
  int y = x;
  sink(y);
}

// Arithmetic creates a new Value: the transfer function must propagate.
void arithmetic() {
  int x = source();
  int y = x + 1;
  sink(y);
}

void clean() {
  int x = 0;
  sink(x);
}

void sanitized() {
  int x = source();
  int y = sanitize(x);
  sink(y);
}

// Only one path taints x: after the merge it MAY be tainted.
void branch(bool a) {
  int x = 0;
  if (a)
    x = source();
  sink(x);
}

// Both paths taint x.
void both(bool a) {
  int x;
  if (a)
    x = source();
  else
    x = source();
  sink(x);
}

// The flow condition remembers WHICH path: tainted iff a.
void correlated(bool a) {
  int x = 0;
  if (a)
    x = source();
  if (a)
    sink(x);
  else
    sink(x);
}

// A loop: the loop-head join/compare has to converge.
void loop(int n) {
  int x = 0;
  while (n) {
    x = x + source();
    n = n - 1;
  }
  sink(x);
}

// Two variables swap Values on every iteration: after the loop either may hold the taint.
void swap_loop(int n) {
  int x = source();
  int y = 0;
  while (n) {
    int t = x;
    x = y;
    y = t;
    n = n - 1;
  }
  sink(x);
  sink(y);
}
