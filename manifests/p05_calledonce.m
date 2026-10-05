// Part 5.5 -- called-once parameters in Objective-C, the only language where Sema runs the check.
// A "called_once" callback must run exactly once on every path.
typedef void (^Handler)(void);
#define CALLED_ONCE __attribute__((called_once))

// Correct: exactly one call on each path.
void ok(int c, Handler h CALLED_ONCE) {
  if (c)
    h();
  else
    h();
}

// Called twice when c is true.
void twice(int c, Handler h CALLED_ONCE) {
  if (c)
    h();
  h();
}

// Not called when c is false.
void missing_else(int c, Handler h CALLED_ONCE) {
  if (c)
    h();
}

// Called inside a loop: the body may run many times, so this is a double call.
void loop_calls(int n, Handler h CALLED_ONCE) {
  for (int i = 0; i < n; ++i)
    h();
}

// Not called for the case labels that skip it.
void switch_gap(int c, Handler h CALLED_ONCE) {
  switch (c) {
  case 0:
    h();
    break;
  case 1:
    break;
  }
}

// Convention-based: no attribute, but the name says "completion handler".
void conventional(int c, Handler completionHandler) {
  if (c)
    completionHandler();
}
