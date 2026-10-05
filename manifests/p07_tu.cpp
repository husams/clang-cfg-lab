// Part 7.3 -- a translation unit with one function of every kind the driver must
// classify: a bug, clean code, a template pattern, a declaration, a heavy function.
namespace std {
template <class T> T &&move(T &t) noexcept { return static_cast<T &&>(t); }
} // namespace std

struct Buf {
  Buf();
  Buf(Buf &&);
  Buf &operator=(Buf &&);
  void use() const;
};
void sink(Buf &&);
bool pick(int);

// declaration only
void declared_only(Buf &);

// template pattern: dependent types, AdornedCFG::build refuses it
template <class T> void tmpl(T &t) { sink(std::move(t)); }

// no std::move anywhere: the prefilter skips it
int plain(int x) { return x + 1; }

// a real bug
void bug() {
  Buf a;
  sink(std::move(a));
  a.use();
}

// clean
void clean() {
  Buf a;
  sink(std::move(a));
}

// heavy: eight independent decisions (256 paths), a move and a use that depend on
// all of them -- the join points make the SAT solver work
void heavy(bool c0, bool c1, bool c2, bool c3, bool c4, bool c5, bool c6, bool c7) {
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

// loop: needs several visits per block to reach a fixpoint
void looped(int n) {
  Buf a;
  for (int i = 0; i < n; ++i) {
    if (pick(i))
      sink(std::move(a));
    else
      a = Buf();
  }
}
