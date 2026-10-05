// Part 2.2 / 2.3 -- one construct per CFG::BuildOptions field.
// Each function is small on purpose: flip one option and diff the summary.
struct T {
  T();
  T(const T &);
  ~T();
  bool ok() const;
  int v;
};
T mk();
int may_throw(int);

// ---- constructor: AddInitializers, AddCXXDefaultInitExprInCtors ------------
struct Base {
  Base();
  virtual ~Base();
};
struct A : Base {
  int a = 5; // default member initializer
  T t;
  A();
};
A::A() {} // implicit base init, implicit `a = 5`, implicit `t()`

// ---- AddImplicitDtors -------------------------------------------------------
int dtors(int n) {
  T t;
  if (t.ok())
    return n;
  return n + t.v;
}

// ---- AddTemporaryDtors ------------------------------------------------------
int temps(int n) {
  if (mk().ok() && n)
    return 1;
  return 0;
}

// ---- AddLifetime, AddScopes, AddLoopExit ----------------------------------
int scopes(int n) {
  int total = 0;
  while (n > 0) {
    int sq = n * n;
    total += sq;
    --n;
  }
  return total;
}

// ---- AddStaticInitBranches, AddCXXNewAllocator ----------------------------
int heap_static(int n) {
  static int cache = n;
  int *p = new int(3);
  int r = *p + cache;
  delete p;
  return r;
}

// ---- AddCXXDefaultInitExprInAggregates, OmitImplicitValueInitializers -----
struct Agg {
  int x = 1;
  int y;
};
int aggregate() {
  Agg g{};
  return g.x + g.y;
}

// ---- AddRichCXXConstructors, MarkElidedCXXConstructors --------------------
int elide() {
  T t = mk(); // C++17: constructed in place, no copy
  return t.v;
}
int temp_member() { return T(mk()).v; }

// ---- AddVirtualBaseBranches -----------------------------------------------
struct VB : virtual Base {
  VB(int);
  int m;
};
VB::VB(int a) : Base(), m(a) {}

// ---- AssumeReachableDefaultInSwitchStatements ----------------------------
enum Color { Red, Green };
int covered(Color c) {
  switch (c) {
  case Red:
    return 1;
  case Green:
    return 2;
  }
  return 3;
}

// ---- PruneTriviallyFalseEdges ---------------------------------------------
int pruned_if(int n) {
  if (0)
    n = 1;
  return n;
}
int pruned_while(int n) {
  while (1) {
    if (n > 3)
      break;
    ++n;
  }
  return n;
}

// ---- AddEHEdges -----------------------------------------------------------
int eh(int n) {
  try {
    n = may_throw(n);
  } catch (...) {
    n = 0;
  }
  return n;
}
