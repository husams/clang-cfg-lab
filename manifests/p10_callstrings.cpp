// Part 10.5 -- context sensitivity: k-limited call strings, one answer per way of getting to a function
//
// "May the divisor of divide() be zero?" depends on who called it: safe() passes 2, risky() passes 0,
// pass_on() passes whatever it was given. A summary has one answer per function and must merge
// them into top ("maybe"); a call string keeps the last k call sites apart. The functions:
//   divide(a, b)    the division: a / b
//   safe()          divide(10, 2)                a divisor that is never zero
//   risky(x)        divide(x, 0)                 a divisor that is always zero (x only feeds `a`)
//   forward / pass_on   a parameter handed on unchanged through two levels
//   loop(n)         recursion: loop(n - 1) calls loop again, so the call string never stops
//                   growing and the limit k is what ends the analysis
// main() calls each of them once, every call on a line of its own so a call site is `main@L<line>`.

int divide(int a, int b) { return a / b; }

int safe() { return divide(10, 2); }

int risky(int x) { return divide(x, 0); }

int pass_on(int d) { return divide(100, d); }
int forward(int v) { return pass_on(v); }

int loop(int n) {
  if (n <= 0) return risky(n);
  return loop(n - 1);
}

int main() {
  int total = risky(1);
  total += safe();
  total += forward(5);
  total += loop(3);
  return total;
}
