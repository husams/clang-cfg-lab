// Part 3.7 -- lambdas, templates.
int outer(int n) {
  auto l = [n](int k) {
    if (k > n)
      return k;
    return n;
  };
  return l(3);
}

// a generic lambda: only its instantiation has a runnable body
int generic_user() {
  auto g = [](auto x) { return x + 1; };
  return g(1);
}

// a function template and its instantiation
template <typename U>
U twice(U x) {
  if (x > U())
    return x + x;
  return x;
}
int use_twice() {
  return twice(2);
}

// a class template with a member function
template <typename U>
struct Box {
  U v;
  U get() const {
    return v;
  }
};
int use_box() {
  Box<int> b{1};
  return b.get();
}

// a non-template function with a template-dependent-looking body: fine
struct Plain {
  int f(int n) {
    return n ? 1 : 2;
  }
};
