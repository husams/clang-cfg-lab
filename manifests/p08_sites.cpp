// Part 8.7 -- call sites in the CFG: where each call happens, and which ones the call graph has no edge for
//
// main() contains every kind of site: a direct call in a branch and in a loop,
// a local object (constructor now, destructor at the closing brace), a
// temporary, new and delete, a function-pointer call (inside apply), a virtual
// call (inside dispatch) and a call to a function with no body.

int leaf(int x) { return x + 1; }

// A callee with a call of its own: the interprocedural walk descends into it.
int helper(int x) { return leaf(x) * 2; }

// Declared, never defined.
int external(int x);

struct Holder {
  Holder(int v) : value(leaf(v)) {}
  ~Holder() { helper(value); }
  int value;
};

struct Base {
  virtual int run(int x) { return leaf(x); }
  virtual ~Base() {}
};
struct Derived : Base {
  int run(int x) override { return helper(x); }
};

// A virtual call: the graph records only the static callee, Base::run.
int dispatch(Base &b) { return b.run(3); }

// A call through a function pointer: no callee is known.
int apply(int (*fn)(int), int x) { return fn(x); }

// Calls in the branches and in the body of a loop.
int branches(int x) {
  int total = 0;
  if (x > 0)
    total += helper(x);
  else
    total += leaf(x);
  for (int i = 0; i < 3; ++i)
    total += leaf(i);
  return total;
}

int main() {
  Holder h(1);
  Derived d;
  int total = branches(h.value) + dispatch(d);
  total += apply(helper, total);
  total += external(total);
  Holder *p = new Holder(total);
  delete p;
  return total + Holder(3).value;
}
