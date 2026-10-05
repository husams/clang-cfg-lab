// Part 7.4 -- budgets are part of the contract: this fixture runs with a small
// MaxBlockVisits and asserts exactly which functions fail.
// budget: max-visits=20
namespace std {
template <class T> T &&move(T &t) noexcept { return static_cast<T &&>(t); }
} // namespace std

struct Buf {
  Buf();
  Buf(Buf &&);
  void use() const;
};
void sink(Buf &&);

void small() {
  Buf a;
  sink(std::move(a));
  a.use(); // expect: certain
}

void big(bool c0, bool c1, bool c2, bool c3, bool c4, bool c5, bool c6, bool c7) { // expect-error: max-visits
  Buf a;
  bool m = false;
  if (c0) m = !m;
  if (c1) m = !m;
  if (c2) m = !m;
  if (c3) m = !m;
  if (c4) m = !m;
  if (c5) m = !m;
  if (c6) m = !m;
  if (c7) m = !m;
  if (m)
    sink(std::move(a));
  if (!m)
    a.use();
}
