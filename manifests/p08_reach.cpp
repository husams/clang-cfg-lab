// Part 8.4 / 8.5 -- reachability, dead functions, callers and orders

// leaf: called from several places
int leaf(int x) { return x + 1; }
int helper(int x) { return leaf(x) * 2; }

// nobody calls unused, and helper_of_unused is only called by unused: both dead
int helper_of_unused(int x) { return helper(x); }
int unused(int x) { return helper_of_unused(x) + leaf(x); }

// static: internal linkage, so --roots=external does not make it a root
static int file_local(int x) { return leaf(x); }
static int static_unused(int x) { return file_local(x); }

// a method that calls leaf
struct Base {
  virtual int run(int x) { return leaf(x); }
};

// address taken in main and called through the pointer: the graph has no edge to it
int callback_target(int x) { return helper(x); }

int main() {
  int (*cb)(int) = callback_target;
  return helper(1) + file_local(2) + cb(3);
}
