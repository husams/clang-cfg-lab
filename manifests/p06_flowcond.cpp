// Part 6.5 -- conditions the SAT solver can decide (p06_flowcond).

// Nothing is known about a: the condition is genuinely open.
int open_cond(bool a) {
  if (a)
    return 1;
  return 0;
}

// Inside the then-branch the flow condition contains `a`, so the inner test is redundant.
int nested_same(bool a) {
  if (a) {
    if (a)
      return 1;
  }
  return 0;
}

// The else-branch contains !a: the inner test can never be true.
int nested_opposite(bool a) {
  if (a)
    return 1;
  else {
    if (a)
      return 2;
  }
  return 0;
}

// The two paths join: nothing is known afterwards, so the second `if` is open.
int after_join(bool a) {
  int r = 0;
  if (a)
    r = 1;
  if (a)
    r = 2;
  return r;
}

// Through a boolean local: b is the same Value as a.
int through_local(bool a) {
  bool b = a;
  if (a) {
    if (b)
      return 1;
  }
  return 0;
}

// Compound conditions: the true edge of `a && b` knows both a and b.
int compound_a(bool a, bool b) {
  if (a && b) {
    if (a)
      return 1;
  }
  return 0;
}

int compound_b(bool a, bool b) {
  if (a && b) {
    if (!b)
      return 2;
  }
  return 0;
}

// An impossible path: its flow condition is unsatisfiable, so the solver
// "proves" both c and !c there.
int vacuous(bool a, bool c) {
  if (a) {
    if (!a) {
      if (c)
        return 1;
    }
  }
  return 0;
}

// Integers: x and y hold the very same Value, so x == y is always true.
int equal_ints(int x) {
  int y = x;
  if (x == y)
    return 1;
  return 0;
}

// Pointers: `if (p)` (pointer-to-bool) gets no BoolValue, and p == nullptr is not tied to it.
int pointer_checks(int *p) {
  if (p) {
    if (p == nullptr)
      return 1;
  }
  return 0;
}

// Integer truthiness: every `int -> bool` conversion makes a fresh, unrelated bool.
int int_truthy(int x) {
  if (x) {
    if (x)
      return 1;
  }
  return 0;
}

// Arithmetic comparisons are not modelled: no BoolValue for `<`.
int not_modelled(int x) {
  if (x < 3) {
    if (x < 3)
      return 1;
  }
  return 0;
}
