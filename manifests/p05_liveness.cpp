// Part 5.1 -- backward liveness. Small functions, one idea each.
int g(int);

// Straight line: the classic gen/kill example.
int straight(int a, int b) {
  int x = a + 1;
  int y = x * 2;
  return y;
}

// A diamond: x is live around the join only if the join reads it.
int diamond(int a) {
  int x;
  if (a > 0)
    x = 1;
  else
    x = 2;
  return x;
}

// A loop: i and sum are live around the back edge.
int loop(int n) {
  int sum = 0;
  for (int i = 0; i < n; ++i)
    sum += g(i);
  return sum;
}

// Overwritten store: the first assignment is never read.
int overwritten(int a) {
  int x = 1;
  x = a;
  return x;
}
