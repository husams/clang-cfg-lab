// Part 7.5 -- context-sensitive analysis: what descending into callees changes.
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
bool unknown();

bool always_true() { return true; }
bool always_false() { return false; }

// The two guards come from calls. Without descending, each call returns an
// unrelated unknown, so the checker cannot rule out "moved here, used there".
void guard_literal() {
  Buf a;
  if (always_true())
    sink(std::move(a));
  if (always_false())
    a.use();
}

// Same shape, but the callee is opaque (declared only): never descendable.
void guard_opaque() {
  Buf a;
  if (unknown())
    sink(std::move(a));
  if (!unknown())
    a.use();
}

// The move happens inside the helper.
void take(Buf &b) { sink(std::move(b)); }
void move_in_callee() {
  Buf a;
  take(a);
  a.use();
}

// The re-initialisation happens inside the helper.
void renew(Buf &b) { b.reset(); }
void reinit_in_callee() {
  Buf a;
  sink(std::move(a));
  renew(a);
  a.use();
}

// Not descendable: the callees touch a global, so their literal results stay hidden.
int counter;
bool bump() { ++counter; return true; }
bool unbump() { ++counter; return false; }
void global_callee() {
  Buf a;
  if (bump())
    sink(std::move(a));
  if (unbump())
    a.use();
}

// Not descendable: recursion.
bool rec(int n) { return n == 0 ? true : rec(n - 1); }
void recursive_callee() {
  Buf a;
  if (rec(3))
    sink(std::move(a));
  a.use();
}

// Depth: the literal is two calls down.
bool wrap_true() { return always_true(); }
bool wrap2_true() { return wrap_true(); }
void deep(bool) {
  Buf a;
  if (wrap2_true())
    sink(std::move(a));
  if (!wrap2_true())
    a.use();
}
