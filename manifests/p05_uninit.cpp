// Part 5.3 -- every kind of uninitialised use.
void take_const_ref(const int &);
void take_const_ptr(const int *);
void take_ptr(int *);
int g(int);

// Always: no path assigns x.
int always(int a) {
  int x;
  return x + a;
}

// Sometimes: one branch of the if leaves x alone.
int sometimes(int a) {
  int x;
  if (a)
    x = 1;
  return x;
}

// Maybe: no single branch is to blame.
int maybe(int a, int b) {
  int x;
  switch (a) {
  case 0: x = 1; break;
  case 1: x = 2; break;
  case 2: break;
  }
  if (b)
    x = 3;
  return x;
}

// Loop: first iteration reads garbage.
int in_loop(int n) {
  int x;
  int sum = 0;
  for (int i = 0; i < n; ++i) {
    sum += x;
    x = i;
  }
  return sum;
}

// Self-initialisation idiom.
int self_init() {
  int x = x;
  return x;
}

// Const reference / const pointer: passing is a read.
int const_uses() {
  int r;
  int p;
  take_const_ref(r);
  take_const_ptr(&p);
  return 0;
}

// Non-const pointer: the callee may initialise it, no warning.
int escape_ok() {
  int e;
  take_ptr(&e);
  return e;
}

// Initialised on every path: silent.
int clean(int a) {
  int x;
  if (a)
    x = 1;
  else
    x = 2;
  return x;
}
