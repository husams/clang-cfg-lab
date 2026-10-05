// Part 10.7 -- BodyFarm in C++: std::call_once, and why the sample declares its own std::once_flag
//
// This sample includes no standard header, so it declares std::call_once itself and gives
// std::once_flag the data member BodyFarm looks for: a `__state_` field as in libc++ (libstdc++
// has `_M_once`). Without that field BodyFarm refuses the function and the analyzer sees no body.
// The body it synthesizes is
//   if (!flag.__state_) { f(args); flag.__state_ = 1; }
// so set() runs and x == 7 is TRUE. call_once is a template: its body exists only for an
// instantiation, so a tool has to visit template instantiations to see it (p10_farm does).

void clang_analyzer_eval(int);

namespace std {
struct once_flag {
  unsigned __state_ = 0;
};
template <class Callable, class... Args> void call_once(once_flag &flag, Callable &&f, Args &&...args);
} // namespace std

void set(int &v) { v = 7; }

int main() {
  static std::once_flag flag;
  int x = 0;
  std::call_once(flag, set, x);
  clang_analyzer_eval(x == 7);
  return 0;
}
