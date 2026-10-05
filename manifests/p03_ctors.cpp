// Part 3.3 -- constructors and ConstructionContext.
struct V {
  V();
  V(int);
  V(const V &);
  ~V();
  int x;
};

V make();
void take(V);
void take_ref(const V &);

// a plain local variable
void ctx_variable() {
  V v(1);
}

// a temporary that is destroyed at the end of the statement
int ctx_temporary() {
  return V(2).x;
}

// a temporary bound to a const reference (materialized)
void ctx_materialized() {
  const V &r = V(3);
  (void)r;
}

// the constructed object is the return value
V ctx_return() {
  return V(4);
}

// the object is constructed into a function argument
void ctx_argument() {
  take(V(5));
}

// the object is constructed on the heap
void ctx_new() {
  V *p = new V(6);
  delete p;
}

// the object initializes a member
struct Holder {
  V a;
  V b;
  Holder();
};
Holder::Holder() : a(7), b() {}

// the object initializes a lambda capture
void ctx_lambda() {
  auto l = [v = V(8)] { return v.x; };
  (void)l;
}

// a call returning a class by value
void typed_call() {
  V v = make();
  (void)v;
}

// copy-initialization from a temporary: elided in C++17, elidable before
void elision() {
  V v = V(9);
  (void)v;
}

// a returned call: nothing is constructed, make() constructs the result
V return_call() {
  return make();
}

// a temporary passed by const reference
void arg_ref() {
  take_ref(V(10));
}

// a member initialized from a call that returns by value
struct Holder2 {
  V a;
  Holder2();
};
Holder2::Holder2() : a(make()) {}
