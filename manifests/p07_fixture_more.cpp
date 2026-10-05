// Part 7.4 -- regression fixture: shapes that once went wrong or that guard a design decision.
// Format: see tools/p07_verify/main.cpp.
namespace std {
template <class T> T &&move(T &t) noexcept { return static_cast<T &&>(t); }
} // namespace std

struct Buf {
  Buf();
  Buf(Buf &&);
  Buf &operator=(Buf &&);
  void use() const;
  void reset();
};
void sink(Buf &&);
bool pick(int);

// early return: the use is only reached on the not-moved path
void early_return(bool c) {
  Buf a;
  if (c) {
    sink(std::move(a));
    return;
  }
  a.use();
}

// switch: one case moves, the join after the switch is "possibly moved"
void switch_join(int k) {
  Buf a;
  switch (k) {
  case 0:
    sink(std::move(a));
    break;
  case 1:
    break;
  default:
    a.reset();
  }
  a.use(); // expect: possible
}

// break leaves the loop with the object moved
void loop_break() {
  Buf a;
  while (pick(0)) {
    sink(std::move(a));
    break;
  }
  a.use(); // expect: possible
}

// a data member of a class
struct Holder {
  Buf m;
  void f() {
    m.use();
    sink(std::move(m));
    m.use(); // expect: certain
  }
  void g() {
    sink(std::move(m));
    m.reset();
    m.use();
  }
};

// the moved object is the destination of a move-assignment in the same statement
void assign_both() {
  Buf a, b;
  a = std::move(b);
  a.use();
  b.use(); // expect: certain
}

// a reference to the object: same location, so same state
void via_reference() {
  Buf a;
  Buf &r = a;
  sink(std::move(r));
  a.use(); // expect: certain
}

// a pointer to the object
void via_pointer() {
  Buf a;
  Buf *p = &a;
  sink(std::move(*p));
  a.use(); // expect: certain
}

// regression: the first block is a loop header and the object is a parameter, so the
// (unconstrained) starting state flows into the header on every iteration
void loop_param(Buf &p, bool c) {
  while (c)
    p.use();
}
