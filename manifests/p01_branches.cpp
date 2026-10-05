// Part 1.3 -- one function per control-flow construct.
int g(int);

int if_else(int a) {
  int r;
  if (a)
    r = 1;
  else
    r = 2;
  return r;
}

int and_or(int a, int b, int c) {
  if (a && (b || c))
    return 1;
  return 0;
}

int ternary(int a) {
  return a ? g(1) : g(2);
}

int while_loop(int n) {
  int s = 0;
  while (n > 0) {
    s += n;
    --n;
  }
  return s;
}

int do_loop(int n) {
  int s = 0;
  do {
    s += n;
    --n;
  } while (n > 0);
  return s;
}

int range_for(const int (&arr)[4]) {
  int s = 0;
  for (int v : arr)
    s += v;
  return s;
}

int brk_cont(int n) {
  int s = 0;
  for (int i = 0; i < n; ++i) {
    if (i % 2)
      continue;
    if (i > 10)
      break;
    s += i;
  }
  return s;
}

int sw(int k) {
  int r = 0;
  switch (k) {
  case 0:
    r = 10;
    break;
  case 1:
    r = 20;
    // falls through
  case 2:
    r += 5;
    break;
  default:
    r = -1;
  }
  return r;
}

int jump(int n) {
  int s = 0;
loop:
  if (n <= 0)
    goto done;
  s += n;
  --n;
  goto loop;
done:
  return s;
}
