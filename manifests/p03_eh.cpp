// Part 3.6 -- exceptions.
struct T {
  T();
  ~T();
};

int may_throw(int);
int nothrow(int) noexcept;
[[noreturn]] void die();

// the simplest try/catch
int simple_try(int n) {
  try {
    return may_throw(n);
  } catch (int e) {
    return e;
  }
}

// two handlers and a catch-all
int three_handlers(int n) {
  try {
    may_throw(n);
  } catch (int e) {
    return 1;
  } catch (const char *) {
    return 2;
  } catch (...) {
    return 3;
  }
  return 0;
}

// throw inside a try, and rethrow
int throwing(int n) {
  try {
    if (n < 0)
      throw 1;
    may_throw(n);
  } catch (int) {
    throw;
  }
  return n;
}

// throw outside any try: leaves the function
int escapes(int n) {
  if (n < 0)
    throw 1;
  return n;
}

// a local with a destructor, and a call that may throw
int unwinding(int n) {
  T t;
  return may_throw(n);
}

// nested try
int nested_try(int n) {
  try {
    try {
      may_throw(n);
    } catch (int) {
      die();
    }
  } catch (...) {
    return -1;
  }
  return n;
}

// noreturn call inside try
int noreturn_in_try(int n) {
  try {
    if (n)
      die();
  } catch (...) {
  }
  return n;
}

// calls that cannot throw are not given EH edges
int nothrow_try(int n) {
  try {
    return nothrow(n);
  } catch (...) {
    return 0;
  }
}

// function-try-block
int ftb(int n) try {
  return may_throw(n);
} catch (...) {
  return -1;
}
