// Part 2.6 -- terminators, conditions, labels, loop targets, noreturn blocks.
[[noreturn]] void die();
int g(int);

// getTerminatorStmt() is the whole condition, getLastCondition() is only `c`
int chain(int a, int b, int c) {
  if (a && b && c)
    return 1;
  return 0;
}

int ternary(int a) { return a ? g(1) : g(2); }

// a loop edge block carries getLoopTarget()
int loop(int n) {
  int s = 0;
  for (int i = 0; i < n; ++i)
    s += i;
  return s;
}

// labels: LabelStmt, CaseStmt, DefaultStmt, CXXCatchStmt
int labels(int k) {
  switch (k) {
  case 1: return 1;
  default: break;
  }
  goto out;
out:
  return 0;
}

int catching(int n) {
  try {
    g(n);
  } catch (int e) {
    return e;
  }
  return 0;
}

// a call to a [[noreturn]] function ends its block: single successor, Exit
int noret(int a) {
  if (a)
    die();
  return 1;
}

// a temporary that exists on only one side of && needs a *conditional*
// destructor: CFGTerminator::TemporaryDtorsBranch (needs AddTemporaryDtors)
struct T {
  T();
  ~T();
  bool ok() const;
};
T mk();
int cond_temp(int n) { return n && mk().ok(); }

// a loop with no condition: no `cond:`, and the exit edge is pruned
int forever(int x) {
  for (;;) {
    if (g(x))
      break;
    ++x;
  }
  return x;
}
