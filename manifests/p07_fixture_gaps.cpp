// Part 7.4 -- known gaps, recorded as strict markers instead of being forgotten.
//   xfail:    a diagnostic that SHOULD appear and does not (false negative)
//   known-fp: a diagnostic that appears and should not   (false positive)
// When a gap closes, the marker itself fails the run (XPASS) until it is deleted.
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

// FN: the move happens inside a callee; the engine runs callees without our transfer
void take(Buf &b) { sink(std::move(b)); }
void move_in_callee() {
  Buf a;
  take(a);
  a.use(); // xfail: certain
}

// FP: the re-initialisation happens inside a callee
void renew(Buf &b) { b.reset(); }
void reinit_in_callee() {
  Buf a;
  sink(std::move(a));
  renew(a); // known-fp: certain
  a.use(); // known-fp: certain
}

// FN: a cast to Buf&& is a move too, but we only recognise std::move
void cast_move() {
  Buf a;
  sink(static_cast<Buf &&>(a));
  a.use(); // xfail: certain
}

// FP: c cannot change, so "moved" implies c, but the loop-head join forgets that
void loop_correlated(bool c, int n) {
  Buf a;
  for (int i = 0; i < n; ++i) {
    if (c)
      sink(std::move(a)); // expect: possible
    if (!c)
      a.use(); // known-fp: possible
  }
}
