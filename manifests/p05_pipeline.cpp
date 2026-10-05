// Part 5.8 -- one function that every classic analysis has something to say about.
struct __attribute__((capability("mutex"))) Mutex {
  void Lock() __attribute__((acquire_capability()));
  void Unlock() __attribute__((release_capability()));
};

struct __attribute__((consumable(unconsumed))) File {
  File() __attribute__((return_typestate(unconsumed)));
  void use() __attribute__((callable_when("unconsumed")));
  void close() __attribute__((set_typestate(consumed)));
};

Mutex mu;
int shared __attribute__((guarded_by(mu)));
int *escape(int n);

int everything(int a) {
  int x;                // uninitialised when a == 0
  if (a)
    x = 1;
  shared = x;           // thread safety: mu is not held
  File f;
  f.close();
  f.use();              // consumed: used after close
  int *p;
  {
    int local = 3;
    p = &local;
  }
  if (a != 1 || a != 2) // logical: always true
    return *p;          // lifetime: p dangles
  return 0;             // unreachable
}

// A function with nothing to report: the options still apply.
int quiet(int a) { return a + 1; }
