// Part 8.9 -- shared header of the cross-TU sample: two declarations, an inline function and a sink.
int a_fn(int n);                           // defined in p08_xtu_a.cpp
int b_fn(int n);                           // defined in p08_xtu_b.cpp
[[noreturn]] void fail();                  // defined nowhere: the sink
inline int shared(int x) { return x + 1; } // defined in every TU that includes this header
