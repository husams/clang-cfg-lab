// Part 7.1-7.2 -- use-after-move samples. One construct per function.
// Trailing `// expect: KIND` comments are the test expectations read by p07_verify (Part 7.4).
// `std::move` is declared by hand so no standard header is needed.
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

// fine: moved but never touched again
void ok_simple() {
  Buf a;
  Buf b(std::move(a));
  b.use();
}

// bug: plain use after the move
void use_after() {
  Buf a;
  Buf b(std::move(a));
  a.use(); // expect: certain
}

// fine: reset() re-initialises
void reinit_reset() {
  Buf a;
  sink(std::move(a));
  a.reset();
  a.use();
}

// fine: assignment re-initialises
void reinit_assign() {
  Buf a;
  sink(std::move(a));
  a = Buf();
  a.use();
}

// bug on one path only
void maybe_moved(bool c) {
  Buf a;
  if (c)
    sink(std::move(a));
  a.use(); // expect: possible
}

// fine: the use is guarded by the same condition that guards the move's absence
void correlated_ok(bool c) {
  Buf a;
  if (c)
    sink(std::move(a));
  if (!c)
    a.use();
}

// bug: correlated the other way -- certain under the branch
void correlated_bad(bool c) {
  Buf a;
  if (c)
    sink(std::move(a));
  if (c)
    a.use(); // expect: certain
}

// bug: second iteration uses a moved object
void loop_move() {
  Buf a;
  for (int i = 0; i < 3; ++i)
    sink(std::move(a)); // expect: possible
}

// fine: fresh object each iteration
void loop_ok() {
  for (int i = 0; i < 3; ++i) {
    Buf a;
    sink(std::move(a));
  }
}

// bug: parameter, moved by a move-assignment source
void param_bug(Buf p, Buf &dst) {
  dst = std::move(p);
  p.use(); // expect: certain
}
