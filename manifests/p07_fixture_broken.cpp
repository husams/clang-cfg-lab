// Part 7.4 -- a deliberately wrong fixture: every kind of failure the harness reports.
namespace std {
template <class T> T &&move(T &t) noexcept { return static_cast<T &&>(t); }
} // namespace std

struct Buf {
  Buf();
  Buf(Buf &&);
  void use() const;
};
void sink(Buf &&);

// wrong kind: the checker is certain here, not possible
void wrong_kind() {
  Buf a;
  sink(std::move(a));
  a.use(); // expect: possible
}

// missing: nothing is wrong here, but the fixture claims there is
void missing() {
  Buf a;
  sink(std::move(a));
  Buf b;
  b.use(); // expect: certain
}

// unexpected: a real bug without a marker
void unexpected() {
  Buf a;
  sink(std::move(a));
  a.use();
}

// stale xfail: this gap does not exist (the bug IS found)
void stale_xfail() {
  Buf a;
  sink(std::move(a));
  a.use(); // xfail: certain
}
