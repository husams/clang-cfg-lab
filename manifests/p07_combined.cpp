// Part 7.7 -- shapes for the "classic analyses + dataflow" tool.
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

// straight line
void line() {
  Buf a;
  sink(std::move(a));
  a.use();
}

// three uses after one move: only the first is news
void noisy() {
  Buf a;
  sink(std::move(a));
  a.use();
  a.use();
  if (pick(1))
    a.use();
}

// a diamond, then a use after the join
void diamond(bool c) {
  Buf a;
  if (c)
    sink(std::move(a));
  else
    a.use();
  a.use();
}

// one loop
void loop1(int n) {
  Buf a;
  for (int i = 0; i < n; ++i)
    a.use();
  sink(std::move(a));
}

// nested loops
void loop2(int n) {
  Buf a;
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
      if (pick(j))
        a.use();
  sink(std::move(a));
  a.use();
}

// goto into a loop body: the CFG is irreducible
void irreducible(int n) {
  Buf a;
  int i = 0;
  if (n > 5)
    goto inside;
  for (; i < n; ++i) {
    a.use();
  inside:
    a.use();
  }
  sink(std::move(a));
}
