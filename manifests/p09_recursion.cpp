// Part 9.2 -- the shapes of recursion, as the call graph sees them

// self recursion: one node with a self edge
int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

// mutual recursion, two functions
int is_odd(int n);
int is_even(int n) { return n == 0 ? 1 : is_odd(n - 1); }
int is_odd(int n) { return n == 0 ? 0 : is_even(n - 1); }

// a three-function cycle: ping -> pong -> pang -> ping
int pong(int n);
int pang(int n);
int ping(int n) { return n == 0 ? 0 : pong(n - 1); }
int pong(int n) { return pang(n); }
int pang(int n) { return ping(n); }

// two cycles sharing a node (a <-> b and b <-> c): one SCC of three
int b(int n);
int c(int n);
int a(int n) { return n == 0 ? 0 : b(n - 1); }
int b(int n) { return n == 0 ? 0 : a(n - 1) + c(n - 1); }
int c(int n) { return n == 0 ? 0 : b(n - 1); }

// recursion only through a function pointer: the graph has no cycle
int step(int n);
int (*next_step)(int) = step;
int step(int n) { return n == 0 ? 0 : next_step(n - 1); }

// recursion only through a virtual call: Grid::area -> measure -> Shape::area (static
// callee), while at run time Shape::area on a Grid is Grid::area again
struct Shape {
  virtual int area(int n) { return n; }
};
int measure(Shape &s, int n) { return s.area(n); }
struct Grid : Shape {
  int area(int n) override { return n == 0 ? 0 : measure(*this, n - 1); }
};

// a caller above the cycles; is_odd is called here after its definition
int main() { return fact(3) + is_even(4) + is_odd(3) + ping(2) + a(2) + step(2); }
