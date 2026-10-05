// Part 3.5 -- new, delete, function-local statics, virtual bases.
struct T {
  T();
  T(int);
  ~T();
  int v;
};

T *new_one() {
  return new T(1);
}

T *new_array() {
  return new T[4];
}

int *new_int() {
  return new int(3);
}

// placement new
struct P {
  P();
};
void *operator new(unsigned long, void *p) noexcept;
P *placement(void *buf) {
  return new (buf) P;
}

// new expression whose constructor might throw: the allocation is separate
void del(T *p) {
  delete p;
}

// function-local statics are initialized once
int cached(int n) {
  static int cache = n;
  return cache;
}

int cached_obj() {
  static T t(2);
  return t.v;
}

// virtual bases are constructed only by the most-derived class
struct VB {
  VB();
  ~VB();
};
struct Mid : virtual VB {
  Mid();
};
Mid::Mid() {}

struct Most : Mid {
  Most();
};
Most::Most() {}
