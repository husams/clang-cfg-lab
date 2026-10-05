// Part 3.4 -- initializers, default member initializers, aggregates.
struct Base {
  Base();
  Base(int);
  ~Base();
};
struct M {
  M();
  ~M();
};

struct A : Base {
  int a = 5;
  M m;
  int plain;
  A();
  A(int);
  A(const A &);
};

// base + members in declaration order, even if written in another order
A::A() : plain(1) {}

// written initializers
A::A(int n) : Base(n), m(), a(n) {}

// delegating constructor: one initializer, no base/member initializers
A::A(const A &o) : A(o.a) {}

// a constructor with no mem-initializer list at all
struct Empty {
  int x;
  M m;
  Empty() {}
};

// aggregates with default member initializers
struct Agg {
  int x = 1;
  int y;
};
int aggregate_empty() {
  Agg g{};
  return g.x + g.y;
}
int aggregate_designated() {
  Agg g{.y = 2};
  return g.x + g.y;
}
int aggregate_full() {
  Agg g{3, 4};
  return g.x + g.y;
}
