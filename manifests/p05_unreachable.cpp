// Part 5.4 -- unreachable code, one reason per function.
#define DEBUG_LOG 0
void use(int);
[[noreturn]] void die(const char *);

// UK_Return: a return that can never run.
int after_return(int a) {
  return a;
  use(a);
}

// UK_Return again, after a noreturn call.
int after_noreturn(int a) {
  die("boom");
  return a;
}

// UK_Break: break after return inside a switch.
int dead_break(int a) {
  switch (a) {
  case 1:
    return 1;
    break;
  }
  return 0;
}

// UK_Loop_Increment: the loop body always returns.
int dead_increment(int n) {
  for (int i = 0; i < n; ++i)
    return i;
  return -1;
}

// A configuration constant: the CFG prunes the edge; Sema suppresses the warning
// because the condition came from a macro (SilenceableCondVal).
void config_macro(int a) {
  if (DEBUG_LOG)
    use(a);
}

// A literal condition: reported, with a "silence" range.
void literal_false(int a) {
  if (0)
    use(a);
}

// Code after an endless loop.
int after_infinite(int a) {
  while (1)
    use(a);
  return 0;
}

// Nothing unreachable here.
int clean(int a) {
  if (a)
    return 1;
  return 0;
}
