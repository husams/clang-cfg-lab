// Part 10.3 -- bottom-up summaries: which functions can reach a [[noreturn]] sink
//
// fail() is the sink. The functions below show the three answers (none, may,
// always), recursion that the fixed point has to iterate on, and a non-recursive
// call chain for the depth summary.

// The sink: declared, never defined.
[[noreturn]] void fail();

// Every path ends in the sink.
void die() { fail(); }

// Both branches end in a sink, through two different callees.
void always_dies(int x) {
  if (x)
    fail();
  else
    die();
}

// Only one branch reaches the sink.
int guarded(int x) {
  if (x < 0) die();
  return x;
}

// Reaches no sink.
int safe(int x) { return x + 1; }

// Self recursion that meets the sink inside the cycle.
void retry(int n) {
  if (n == 0) fail();
  retry(n - 1);
}

// Self recursion without a sink.
int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

// Mutual recursion: the sink enters through pong only, ping learns it one pass later.
int ping(int n);
int pong(int n) {
  if (n == 0) die();
  return ping(n - 1);
}
int ping(int n) { return n == 0 ? 0 : pong(n - 1); }

// A three-member cycle: the sink enters through ring_c, so it takes three passes to reach ring_a.
int ring_b(int n);
int ring_c(int n);
int ring_a(int n) { return n == 0 ? 0 : ring_b(n - 1); }
int ring_b(int n) { return n == 0 ? 0 : ring_c(n - 1); }
int ring_c(int n) {
  if (n == 0) fail();
  return ring_a(n - 1);
}

// A deep chain without recursion: depth counts the calls down to the leaf.
int level4() { return 4; }
int level3() { return level4() + 1; }
int level2() { return level3() + 1; }
int level1() { return level2() + 1; }
int top() { return level1() + safe(0); }

int main() { return top() + guarded(1) + ping(3) + fact(4); }
