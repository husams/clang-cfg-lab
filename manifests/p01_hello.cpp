// Part 1.1 -- the smallest interesting CFGs.
//
// sign():  one branch  -> a diamond (if / else / join)
// sum():   one loop    -> a cycle in the graph
int sign(int x) {
  if (x < 0)
    return -1;
  return 1;
}

int sum(int n) {
  int total = 0;
  for (int i = 0; i < n; ++i)
    total += i;
  return total;
}
