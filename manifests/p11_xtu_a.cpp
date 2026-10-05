// Part 11.5 -- cross-TU sample, first translation unit: main, a_fn and a file-local helper.
#include "p11_xtu.h"

static int local() { return shared(1); }
int a_fn(int n) { return n <= 0 ? local() : b_fn(n - 1); }
int main() { return a_fn(3); }
