// Part 8.1 -- the smallest call graph: main -> f -> g, one self-recursive function, one dead one
int g(int x) { return x + 1; }
int f(int x) { return g(x) * 2; }

// fact calls itself: a self edge
int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }

// nobody calls unused
int unused(int x) { return x; }

int main() { return f(1) + fact(3); }
