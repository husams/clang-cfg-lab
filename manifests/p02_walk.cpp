// Part 2.4 -- one function that produces (nearly) every CFGElement kind when
// built with the "kitchen" preset:
//
//   Initializer .......... D::D        (base and member initializers)
//   BaseDtor, MemberDtor .. D::~D      (destruction of bases and members)
//   everything else ....... showcase
struct M {
  M();
  ~M();
};
struct B {
  B();
  virtual ~B();
};
struct T {
  T();
  ~T();
  bool ok() const;
};
struct D : B {
  M m;
  D();
  ~D();
};
D::D() : B(), m() {}
D::~D() {}

T mk();
void release(int *);

int showcase(D *p, int n) {
  T t;                                  // Constructor + AutomaticObjectDtor
  int *q = new int(n);                  // NewAllocator
  for (int i = 0; i < n; ++i)           // LoopExit when the loop is left
    if (mk().ok())                      // CXXRecordTypedCall + TemporaryDtor
      break;
  {
    __attribute__((cleanup(release))) int c = 1; // CleanupFunction
    n += c;
  }
  delete p;                             // DeleteDtor
  delete q;
  return n;                             // Statement (and LifetimeEnds/ScopeEnd after it)
}
