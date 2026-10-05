// Part 11.6 -- Clang's own cross-TU analysis, second translation unit: the definitions p11_ctu_a.cpp imports.
int from_b(int x) { return x * 2; }
int deref_b(int *p) { return *p; } // a null dereference only visible from p11_ctu_a.cpp's call
