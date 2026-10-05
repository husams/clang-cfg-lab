// Part 10.6 -- how the Static Analyzer goes interprocedural: one file for every IPA switch (also used by 9.7, 9.8 and 11.3)
//
// Run it through the analyzer only (scripts/dumpcfg.sh), never through a build/bin tool.
// main() asks four questions with clang_analyzer_eval (checker debug.ExprInspection):
// each is TRUE when the analyzer inlined the call and "unknown" (a FALSE and a TRUE
// warning on the same line) when it evaluated the call conservatively.
//   helper(1) == 2       a small plain function
//   big(0) == 0          a function over the size limit of the shallow mode (max-inlinable-size)
//   dispatch(d) == 11    a virtual call: only the dynamic type of d tells Derived::run from Base::run
//   fact(3) == 6         recursion: three nested frames, so inline-max-stack-depth matters
// null_chain() is the other half: nothing calls it, so it is a top-level entry of its own,
// and the null pointer it passes travels two calls down (forward_ptr -> read_ptr) before it
// is dereferenced. That is a path report to attach notes to (-analyzer-output=text) and an
// exploded graph with exactly two inlined frames on the path to the bug.

void clang_analyzer_eval(int);

int helper(int x) { return x + 1; }

int big(int x) { if (x > 0) x++; if (x > 1) x++; if (x > 2) x++; if (x > 3) x++; if (x > 4) x++; return x; }

struct Base { virtual int run(int x) { return x; } virtual ~Base() {} };
struct Derived : Base { int run(int x) override { return x + 10; } };
int dispatch(Base &b) { return b.run(1); }

int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

int main() {
  clang_analyzer_eval(helper(1) == 2);
  clang_analyzer_eval(big(0) == 0);
  Derived d;
  clang_analyzer_eval(dispatch(d) == 11);
  clang_analyzer_eval(fact(3) == 6);
  return 0;
}

int read_ptr(int *p) { return *p; }
int forward_ptr(int *q) { return read_ptr(q); }
int null_chain() { return forward_ptr(nullptr); }
