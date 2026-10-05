// Part 5.2 -- dead stores found with LiveVariables::Observer.
int g(int);
int sink(int *);

// The first store is overwritten before anything reads x.
int overwritten(int a) {
  int x = 1;
  x = a;
  return x;
}

// The call result is stored and then overwritten.
int call_result(int a) {
  int y = 0;
  y = g(a);
  y = 5;
  return y;
}

// Stores that ARE read: the branch merges, the loop carries i around.
int live_stores(int a) {
  int x = 0;
  if (a)
    x = 1;
  return x;
}

// Never read at all.
int never_read(int a) {
  int unused = a + 1;
  return a;
}

// Address taken: a callee may read the first store, so we must stay quiet.
int address_taken(int a) {
  int z = a;
  sink(&z);
  z = 9;
  return 0;
}

// A store in a loop that only the next iteration reads.
int loop_carried(int n) {
  int prev = 0;
  for (int i = 0; i < n; ++i) {
    int cur = g(i);
    prev = cur;
  }
  return prev;
}
