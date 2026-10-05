// Part 5.7 -- conditions the CFG builder proves tautological while it lowers them.
#define IS_ONE(v) ((v) == 1)

// logicAlwaysTrue(true): a value or its negation.
int negation_or(int x) {
  if (!x || x)
    return 1;
  return 0;
}

// logicAlwaysTrue(false): a value and its negation.
int negation_and(int x) {
  if (!x && x)
    return 1;
  return 0;
}

// compareAlwaysTrue(true): two comparisons against different constants, joined by ||.
int overlap_or(int x) {
  if (x != 1 || x != 2)
    return 1;
  return 0;
}

// compareAlwaysTrue(false): two equalities against different constants, joined by &&.
int overlap_and(int x) {
  if (x == 1 && x == 2)
    return 1;
  return 0;
}

// compareBitwiseEquality: the mask can never produce the constant.
int bitwise_eq(int x) {
  if ((x & 4) == 3)
    return 1;
  return 0;
}

// compareBitwiseOr: a bitwise-or with a non-zero constant, used as a condition, is always true.
int bitwise_or(int x) {
  if (x | 4)
    return 1;
  return 0;
}

// The same overlap, hidden behind a macro: the builder still calls back, Sema stays quiet.
int in_macro(int x) {
  if (IS_ONE(x) && x == 2)
    return 1;
  return 0;
}

// Silent: ranges overlap legitimately.
int no_hook(int x) {
  if (x > 1 && x < 10)
    return 1;
  return 0;
}
