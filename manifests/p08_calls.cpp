// Part 8.7 -- one call of every AnyCall kind (compile with: -- -std=c++17 -fblocks)
// No standard headers: operator new and operator delete are the compiler's implicit declarations.

int twice(int x) { return x * 2; }

struct Counter {
  int n = 0;
  int bump(int by) { n += by; return n; }
  int operator()(int by) const { return n + by; }
};

// a constructor with an initialiser call, and a destructor nobody calls by name
struct Holder {
  Holder(int v) : v(twice(v)) {}
  ~Holder() {}
  int v;
};

// using Base::Base: the derived class gets an inheriting constructor, defined when it is used
struct Base {
  Base(int x) : b(x) {}
  int b;
};
struct Derived : Base {
  using Base::Base;
};

int (*fp)(int) = twice;
int through_block(int (^blk)(int)) { return blk(3); }

int main() {
  Counter c;
  int r = twice(1);                       // Function: a plain call
  r += c.bump(2);                         // Function: a member call
  r += c(3);                              // Function: operator() is a call to a member
  r += fp(4);                             // Function with no declaration: a pointer call
  Holder h(5);                            // Constructor
  Holder *p = new Holder(6);              // Allocator (operator new), then Constructor
  delete p;                               // Deallocator (operator delete); the destructor is not an expression
  Derived d(7);                           // Constructor: the inheriting constructor
  auto lam = [](int x) { return twice(x); };
  r += lam(8);                            // Function: the lambda's operator()
  r += through_block(^(int x) { return twice(x); });  // Block literal passed on, then called through a variable
  r += ^(int x) { return twice(x); }(9);  // Block: an immediately invoked block literal
  return r + h.v + d.b;                   // the destructors of h and c run here: no expression, no AnyCall
}
