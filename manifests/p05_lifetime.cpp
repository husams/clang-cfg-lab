// Part 5.6 -- experimental lifetime safety: loans (borrows) and origins (pointers) over the CFG.

// A pointer to a local escapes through return.
int *return_local() {
  int x = 1;
  int *p = &x;
  return p;
}

// A pointer outlives the scope of what it points at.
void dangling_scope() {
  int *p;
  {
    int y = 2;
    p = &y;
  }
  *p = 3;
}

// Fine: the pointee is a parameter owned by the caller.
int *pass_through(int *a) { return a; }

// Fine: the pointer is only used while the local is alive.
int use_in_scope() {
  int z = 5;
  int *p = &z;
  return *p;
}

// Flow-sensitivity: only one branch lets the pointer escape.
int *one_branch(bool c, int *other) {
  int w = 7;
  int *p = other;
  if (c)
    p = &w;
  return p;
}
