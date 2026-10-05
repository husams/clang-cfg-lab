// Part 8.3 -- what the call graph records and what it leaves out
// (compile with: -- -std=c++17 -fblocks)

int leaf(int x) { return x + 1; }
int helper(int x) { return leaf(x) * 2; }

// excluded by name: the graph skips identifiers that start with __inline
int __inline_helper(int x) { return x - 1; }
int uses_inline_helper() { return __inline_helper(1); }

// declaration-only callee: a node of its own, with no callees
int declared_only(int x);
int calls_decl() { return declared_only(1); }

// a [[noreturn]] declaration (the sink of Section 8.6)
[[noreturn]] void fail();
void die() { fail(); }

// a template: the pattern is not a node, each instantiation is
template <typename T> T twice(T t) { return helper(t) + helper(t); }
int use_tpl() { return twice(1) + twice(2.0); }

// a static function: internal linkage
static int file_local() { return leaf(0); }
int use_static() { return file_local(); }

// implicit members: Derived declares no constructor and no destructor
struct Base {
  virtual int run(int x) { return leaf(x); }
  virtual ~Base() {}
};
struct Derived : Base {
  int run(int x) override { return helper(x); }
};
int use_derived() { Derived d; return d.run(2); }

// a virtual call through a base reference: only the static callee Base::run
int dispatch(Base &b) { return b.run(3); }

// a constructor with an initialiser call, and a destructor
struct Holder {
  Holder() : v(leaf(1)) {}
  ~Holder() { helper(v); }
  int v;
};
// the destructor call at the closing brace is implicit: no edge to Holder::~Holder
int with_obj() { Holder h; return h.v; }
// new -> operator new and the constructor; delete adds no edge at all
int with_new() { Holder *p = new Holder; int v = p->v; delete p; return v; }

// default arguments and default member initialisers belong to the user of the default
int dflt(int x = leaf(2)) { return x; }
int use_default() { return dflt(); }
struct Member { int m = helper(3); };
int use_member() { Member m; return m.m; }

// two lambdas in one function print the same qualified name: the tool adds @L<line>
int two_lambdas() {
  auto a = [](int x) { return leaf(x); };
  auto b = [](int x) { return helper(x); };
  return a(1) + b(2);
}

// a block called through a variable has no edge; an immediately invoked one does
int blk_var() {
  int (^b)(int) = ^(int x) { return leaf(x); };
  return b(1);
}
int blk_now() {
  return ^(int x) { return helper(x); }(2);
}

// a call through a function pointer has no edge
int (*fp)(int) = leaf;
int indirect() { return fp(2); }
