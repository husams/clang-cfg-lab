// Part 10.7 -- BodyFarm in C: a function with no body in the source gets one in the analyzer
//
// dispatch_once is declared here and defined nowhere, as it is in <dispatch/dispatch.h>.
// BodyFarm recognises it by name and signature and synthesizes
//   if (*predicate != ~0L) { *predicate = ~0L; block(); }
// so the analyzer sees the block run, and x == 1 is TRUE (faux-bodies=false makes it unknown).
// Compile with -fblocks: a block literal is a Clang extension in C. x is __block so the
// block can assign to it. BodyFarm checks the shape of the declaration: two parameters, the
// first a pointer to an integer type (dispatch_once_t), the second a void (^)(void) block.

void clang_analyzer_eval(int);

typedef long dispatch_once_t;
void dispatch_once(dispatch_once_t *predicate, void (^block)(void));

int main(void) {
  static dispatch_once_t pred;
  __block int x = 0;
  dispatch_once(&pred, ^{ x = 1; });
  clang_analyzer_eval(x == 1);
  return 0;
}
