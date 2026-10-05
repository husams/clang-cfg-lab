// Part 11.2 -- virtual calls: the static callee, devirtualisation and class-hierarchy analysis (CHA).
// The call graph records one edge per virtual call site: the method found by name lookup
// in the *static* type of the object expression.
int leaf(int x) { return x + 1; }
int helper(int x) { return leaf(x) * 2; }
int other(int x) { return x - 1; }
int extra(int x) { return x + 2; }

struct Base {
  virtual int run(int x) { return leaf(x); }
  virtual ~Base() {}
};
struct Derived : Base {
  int run(int x) override { return helper(x); }
};
// 'final': nothing can derive from it, so Final::run is the only possible target
struct Final final : Base {
  int run(int x) override { return other(x); }
};
// an overrider in a class that nothing in this TU ever instantiates (CHA still adds it)
struct Unused : Base {
  int run(int x) override { return extra(x); }
};

// Site A: base reference, dynamic type unknown -> the call graph has Base::run only; CHA adds the rest.
int dispatch(Base &b) { return b.run(3); }
// Site B: the object is a 'final' class; the cast hides it from name lookup (static callee is Base::run).
int call_final(Final &f) { return static_cast<Base &>(f).run(1); }
// Site C: a local object has exactly its declared type; same cast, same hidden callee.
int call_local() {
  Derived d;
  return static_cast<Base &>(d).run(2);
}

// an abstract base: the static callee is a pure virtual declaration without a body
struct Shape {
  virtual int area() = 0;
};
struct Square : Shape {
  int area() override { return 4; }
};
int use_shape(Shape &s) { return s.area(); }

// an abstract base with no overrider anywhere in this TU: no possible target at all
struct Orphan {
  virtual int pure(int) = 0;
};
int use_orphan(Orphan &o) { return o.pure(1); }

int main() {
  Derived d;
  Final f;
  Square q;
  return dispatch(d) + call_final(f) + call_local() + use_shape(q);
}
