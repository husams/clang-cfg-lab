// Part 11.2 -- rapid type analysis (RTA): CHA narrowed to the classes this translation unit instantiates.
// A virtual call can only reach the override of a class somebody creates an object of.
struct Sensor {
  virtual int read() = 0;
};
// created with new in make_heap
struct Heap : Sensor {
  int read() override { return 1; }
};
// created as a local variable in use_local
struct Local : Sensor {
  int read() override { return 2; }
};
// never created in this translation unit (another one may create it): CHA adds it, RTA does not
struct Remote : Sensor {
  int read() override { return 3; }
};

int poll(Sensor &s) { return s.read(); }
int make_heap() {
  Sensor *s = new Heap;
  return poll(*s);
}
int use_local() {
  Local l;
  return poll(l);
}

// a hierarchy whose only concrete class is never created here: CHA finds a target, RTA none
struct Gauge {
  virtual int level() = 0;
};
struct Tank : Gauge {
  int level() override { return 4; }
};
int read_gauge(Gauge &g) { return g.level(); }

int main() { return make_heap() + use_local(); }
