// Part 11.1 -- function pointers: address-taken functions, signature matching and recursion through a callback.
// The call graph has no edge for a call through a pointer; every such site is an "indirect site".
int add_one(int x) { return x + 1; }
int sub_one(int x) { return x - 1; }
// same signature as add_one, but its address is never taken: never a candidate
int never_taken(int x) { return x * 2; }
// a different signature: address taken, matches only long(long) sites
long widen(long x) { return x * 2; }

// indirect site 1: any address-taken int(int) is a candidate
int apply(int (*f)(int), int x) { return f(x); }
// indirect site 2: a table of pointers; its initialiser takes the addresses of add_one and sub_one
int (*table[])(int) = {add_one, sub_one};
int via_table(int i) { return table[i](1); }
// indirect site 3: only widen has the type long(long)
long call_wide(long (*g)(long)) { return g(3); }
// indirect site 4: no function in this TU has the type void(int, int)
void fire(void (*h)(int, int)) { h(1, 2); }

// recursion that only exists through the pointer: apply calls f, f may be recurse_via_ptr
int recurse_via_ptr(int n) { return n <= 0 ? 0 : apply(recurse_via_ptr, n - 1); }

int main() { return apply(add_one, 1) + via_table(1) + (int)call_wide(widen) + recurse_via_ptr(3); }
