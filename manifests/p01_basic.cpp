// Part 1.2 -- a function with a bit of everything, for learning to read dumps.
// R has a destructor so the CFG shows an implicit destructor call.
struct R {
  R(int);
  ~R();
  int v;
};

int g(int);

int f(int a, int b) {
  int x;
  if (a > 0 && b > 0)
    x = 1;
  else
    x = 2;
  R r(x);
  for (int i = 0; i < a; ++i) {
    if (i == 3)
      break;
    x += g(i);
  }
  return x + r.v;
}
