// Part 4.4 / 4.5 -- one function small enough for the Static Analyzer's debug.Dump*
// checkers, which print no function name and hang on a CFG with an unreachable block.
// The body is a copy of 'while_loop' in p04_graphs.cpp, so block numbers agree.
int while_loop(int n) {
  int s = 0;
  while (n > 0) {
    s += n;
    --n;
  }
  return s;
}
