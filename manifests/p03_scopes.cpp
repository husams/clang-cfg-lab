// Part 3.2 -- lifetimes, scopes, loop exits and cleanup attributes.
struct T {
  T();
  ~T();
  int v;
};

void cleanup(int *);
void use(int);

// the same function the Part 2 tour used
int scopes(int n) {
  int total = 0;
  while (n > 0) {
    int sq = n * n;
    total += sq;
    --n;
  }
  return total;
}

// nested blocks: scopes end innermost-first
int nested(int n) {
  int a = n;
  {
    int b = a + 1;
    {
      int c = b + 1;
      use(c);
    }
    use(b);
  }
  return a;
}

// break leaves the loop body scope early: lifetime/dtor on the break edge
int early_exit(int n) {
  int r = 0;
  for (int i = 0; i < n; ++i) {
    T t;
    if (t.v)
      break;
    r += i;
  }
  return r;
}

// a variable declared in an if-condition and one in a for-init
int cond_decl(int n) {
  if (int k = n * 2)
    return k;
  for (int i = 0; i < n; ++i)
    use(i);
  return 0;
}

// a goto jumps out of nested scopes
int goto_out(int n) {
  {
    T t;
    if (n)
      goto done;
    use(t.v);
  }
done:
  return n;
}

// __attribute__((cleanup)) runs a function at scope exit
int with_cleanup(int n) {
  __attribute__((cleanup(cleanup))) int c = 1;
  if (n)
    return n;
  return c;
}

// do-while and range-for are loops too
int loops(int n) {
  int s = 0;
  do {
    s += n;
  } while (--n);
  return s;
}
