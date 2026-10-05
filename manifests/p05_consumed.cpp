// Part 5.5 -- consumed analysis (typestate): a File that must not be used after close().
// -Wconsumed understands the GNU attribute spelling; [[clang::consumes]] is rejected.
struct __attribute__((consumable(unconsumed))) File {
  File() __attribute__((return_typestate(unconsumed)));
  void use() __attribute__((callable_when("unconsumed")));
  void close() __attribute__((set_typestate(consumed)));
  bool is_open() const __attribute__((test_typestate(unconsumed)));
};

void take_open(File f __attribute__((param_typestate(unconsumed))));
File make_closed() __attribute__((return_typestate(consumed)));
File pass_through(File f __attribute__((param_typestate(unconsumed))),
                  File g __attribute__((return_typestate(unconsumed))));

// Correct.
void fine() {
  File f;
  f.use();
  f.close();
}

// Use after close.
void use_after_close() {
  File f;
  f.close();
  f.use();
}

// The branch on test_typestate refines the state on each side.
void tested(File &f) {
  if (f.is_open())
    f.use();
}

// Closed on one path only: the join is Unknown.
void maybe_closed(bool c) {
  File f;
  if (c)
    f.close();
  f.use();
}

// The state changes in the loop: it differs between loop entry and back edge.
void loop_close(int n) {
  File f;
  for (int i = 0; i < n; ++i)
    f.close();
}

// A callee wants an open file.
void wrong_argument() {
  File f;
  f.close();
  take_open(f);
}

// Return typestate says the result is consumed, but we return an open one.
File wrong_return() __attribute__((return_typestate(consumed)));
File wrong_return() {
  File f;
  return f;
}
