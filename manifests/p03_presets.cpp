// Part 3.8 -- one C++ function that exercises most of what the presets differ on.
struct T {
  T();
  T(int);
  T(const T &);
  ~T();
  bool ok() const;
  int v;
};

T mk();
void cleanup(int *);
int may_throw(int);

struct W {
  T a = T(1);
  T b;
  W();
  ~W();
};
W::W() {}
W::~W() {}

// destructors, a temporary, a loop, a new, a static, a cleanup variable
int kitchen(int n) {
  static int calls = n;
  T t = mk();
  __attribute__((cleanup(cleanup))) int c = 0;
  for (int i = 0; i < n; ++i) {
    T u(i);
    if (mk().ok() && u.ok())
      c += i;
  }
  T *p = new T(2);
  delete p;
  return t.v + c + calls;
}

// try/catch
int guarded(int n) {
  T t;
  try {
    return may_throw(n);
  } catch (int e) {
    return e;
  }
}
