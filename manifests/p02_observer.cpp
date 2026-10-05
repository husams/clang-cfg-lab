// Part 2.3 -- conditions the CFG builder itself notices are tautologies.
// CFGCallback (BuildOptions::Observer) is told while the CFG is being built.

// !x || x : always true            -> logicAlwaysTrue
int logic_true(int x) {
  if (!x || x)
    return 1;
  return 0;
}

// !x && x : always false           -> logicAlwaysTrue(false)
int logic_false(int x) {
  if (!x && x)
    return 1;
  return 0;
}

// x != 1 || x != 2 : always true   -> compareAlwaysTrue
int pair_true(int x) {
  if (x != 1 || x != 2)
    return 1;
  return 0;
}

// (x & 4) == 3 : the mask can never produce 3  -> compareBitwiseEquality
int bitwise_eq(int x) {
  if ((x & 4) == 3)
    return 1;
  return 0;
}

// x | 4 as a condition: non-zero constant, always true  -> compareBitwiseOr
int bitwise_or(int x) {
  if (x | 4)
    return 1;
  return 0;
}

// nothing suspicious
int clean(int x) {
  if (x > 0 && x < 10)
    return 1;
  return 0;
}
