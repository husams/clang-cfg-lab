// Part 3.1 -- every kind of implicit destructor element.
struct T {
  T();
  ~T();
  bool ok() const;
  int v;
};

T mk();

// automatic object: t is destroyed on every path out of its scope
int auto_dtor(int n) {
  T t;
  if (t.ok())
    return n;
  return n + t.v;
}

// two objects: destroyed in reverse order of construction
int two_objects() {
  T a;
  T b;
  return a.v + b.v;
}

// an array of objects: ONE dtor element for the whole array
int array_dtor() {
  T arr[3];
  return arr[0].v;
}

// a temporary: destroyed at the end of the full expression
int temp_dtor() {
  return mk().v;
}

// a temporary created on one side of && only: the dtor is conditional
int cond_temp(int n) {
  return n && mk().ok();
}

// a temporary bound to a const reference lives as long as the reference
int bound_temp() {
  const T &r = mk();
  return r.v;
}

// base and member destructors appear in the destructor's own CFG
struct M {
  ~M();
};
struct Base {
  ~Base();
};
struct VBase {
  ~VBase();
};
struct Derived : Base, virtual VBase {
  int plain; // trivially destructible: no element
  M m1;
  T m2;
  ~Derived();
};
Derived::~Derived() {}

// delete of a class pointer: the dtor call is a CFGDeleteDtor
void delete_dtor(T *p) {
  delete p;
}

// delete of a pointer to an array of class objects
void delete_array_dtor(T *p) {
  delete[] p;
}

// a destructor that never returns
struct Fatal {
  Fatal();
  ~Fatal() __attribute__((noreturn));
};
int noreturn_dtor(int n) {
  Fatal f;
  return n;
}

// the same, as a temporary
int noreturn_temp(int n) {
  Fatal();
  return n;
}
