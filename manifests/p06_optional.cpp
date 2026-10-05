// Part 6.7 -- std::optional accesses (p06_optional).
#include <optional>

// ---- safe ------------------------------------------------------------------
int ok_has_value(std::optional<int> o) {
  if (o.has_value())
    return *o;
  return 0;
}

int ok_negated(std::optional<int> o) {
  if (!o)
    return -1;
  return o.value();
}

int ok_initialized() {
  std::optional<int> o = 3;
  return *o;
}

int ok_emplace() {
  std::optional<int> o;
  o.emplace(1);
  return *o;
}

// ---- unsafe ----------------------------------------------------------------
int bad_unchecked(std::optional<int> o) {
  return *o;
}

int bad_wrong_branch(std::optional<int> o) {
  if (!o)
    return *o;
  return 0;
}

int bad_default() {
  std::optional<int> o;
  return *o;
}

int bad_reset(std::optional<int> o) {
  if (o) {
    o.reset();
    return *o;
  }
  return 0;
}

// value() on an unchecked optional: flagged unless IgnoreValueCalls is set.
int bad_value(std::optional<int> o) {
  return o.value();
}

// Only one path checked: after the merge the optional MAY be empty.
int bad_merge(std::optional<int> o, bool c) {
  if (c) {
    if (!o)
      return 0;
  }
  return *o;
}

// ---- a const accessor returning an optional --------------------------------
struct Box {
  const std::optional<int> &get() const;
  void clear();
};

// Two calls to the same const accessor on the same object return "the same" optional.
int cached_ok(Box &b) {
  if (b.get().has_value())
    return *b.get();
  return 0;
}

// A non-const call (clear) invalidates the cache.
int cached_invalidated(Box &b) {
  if (b.get().has_value()) {
    b.clear();
    return *b.get();
  }
  return 0;
}

// ---- Chromium-style CHECK ----------------------------------------------------
namespace logging {
struct CheckError {
  static CheckError Check(const char *);
  ~CheckError();
};
struct Voidify {
  void operator&(const CheckError &);
};
} // namespace logging
#define CHECK(c) (c) ? (void)0 : logging::Voidify() & logging::CheckError::Check(#c)

int chromium_check(std::optional<int> o) {
  CHECK(o.has_value());
  return *o;
}
