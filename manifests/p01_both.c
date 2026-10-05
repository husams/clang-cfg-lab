/* Part 1.7 -- valid C *and* valid C++. Dump it twice and compare. */
struct S { int a; int b; };

int pick(struct S *s, int k) {
  int r = 0;
  while (k > 0 && s->a != 0) {
    r += s->a > s->b ? s->a : s->b;
    --k;
  }
  return r;
}
