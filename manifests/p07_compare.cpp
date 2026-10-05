// Part 7.6 -- the same shapes, written so that all three tools model the move
// (a move constructor, which cplusplus.Move and bugprone-use-after-move both understand).
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

// bug
void straight() {
  Buf a;
  Buf b(std::move(a));
  a.use();
}

// bug on one path
void one_path(bool c) {
  Buf a;
  if (c) {
    Buf b(std::move(a));
  }
  a.use();
}

// fine: the use is on the other side of the same condition
void correlated(bool c) {
  Buf a;
  if (c) {
    Buf b(std::move(a));
  }
  if (!c)
    a.use();
}

// fine: reset() brings it back
void reinit_reset() {
  Buf a;
  Buf b(std::move(a));
  a.reset();
  a.use();
}

// fine: assignment brings it back
void reinit_assign() {
  Buf a;
  Buf b(std::move(a));
  a = Buf();
  a.use();
}

// bug: the second iteration moves a moved object
void in_loop(int n) {
  Buf a;
  for (int i = 0; i < n; ++i) {
    Buf b(std::move(a));
  }
}
