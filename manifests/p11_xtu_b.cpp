// Part 11.5 -- cross-TU sample, second translation unit: b_fn closes the cycle, cleanup reaches the sink.
#include "p11_xtu.h"

static int local() { return shared(2); }

// calls back into p11_xtu_a.cpp: the cycle a_fn -> b_fn -> a_fn exists only in the merged graph
int b_fn(int n) { return n <= 0 ? local() : a_fn(n - 1); }
void die() { fail(); }
void cleanup() { die(); }
