// Part 1.4 / 1.7 -- C++ constructs that only exist in a C++ CFG.
struct T {
  T();
  ~T();
  bool ok() const;
  int v;
};

T mk();
void cleanup(int *);
[[noreturn]] void die();

// implicit destructors: t is destroyed on every path out of its scope
int dtors(int n) {
  T t;
  if (t.ok())
    return n;
  return n + t.v;
}

// a temporary with a destructor in the middle of a condition
int temporaries(int n) {
  if (mk().ok() && n)
    return 1;
  return 0;
}

// lifetime / scope / loop exit
int scopes(int n) {
  int total = 0;
  while (n > 0) {
    int sq = n * n;
    total += sq;
    --n;
  }
  return total;
}

// new, delete, function-local static
int heap_static(int n) {
  static int cache = n;
  int *p = new int(3);
  int r = *p + cache;
  delete p;
  return r;
}

// exceptions and a noreturn call
int eh(int n) {
  try {
    if (n < 0)
      throw 1;
    if (n == 0)
      die();
  } catch (int e) {
    return e;
  }
  return n;
}

// __attribute__((cleanup)) runs a function at scope exit
int with_cleanup(int n) {
  __attribute__((cleanup(cleanup))) int c = 1;
  return n + c;
}

// an aggregate with a default member initializer (cfg-expand-default-aggr-inits)
struct Agg {
  int x = 1;
  int y;
};
int aggregate() {
  Agg g{.y = 2};
  return g.x + g.y;
}
