// Part 6.4 -- samples for inspecting the Environment (p06_env).

struct Point { int x; int y; };
struct Line { Point a; Point b; };
struct Counter { int hits; };

// Scalars: copies share a Value; equal literals share a Value.
int scalars(int a, bool flag) {
  int x = 3;
  int y = x;
  int z = 3;
  bool b = flag;
  return y;
}

// Pointers: &v creates a PointerValue whose pointee is v's location.
int pointers(int *p) {
  int v = 1;
  int *q = &v;
  int *n = nullptr;
  return *q;
}

// Records: a RecordStorageLocation with one child per field.
int records(Point pt, Line ln) {
  int s = pt.x + ln.a.y;
  return s;
}

// A record that gets a synthetic field ("count") in --mode=synthetic.
int synthetic(Counter c, Point p) {
  return c.hits;
}

int id(int v) { return v; }

// Inlining: does the caller's `b` stay connected to `a`?
int calls(int a) {
  int b = id(a);
  return b;
}
