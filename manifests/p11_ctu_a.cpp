// Part 11.6 -- Clang's own cross-TU analysis, first translation unit: it calls into p11_ctu_b.cpp.
// Analysed alone, from_b and deref_b are opaque; with CTU the analyzer imports their definitions.
void clang_analyzer_eval(int); // debug.ExprInspection: reports TRUE, FALSE or UNKNOWN
int from_b(int x);             // defined in p11_ctu_b.cpp
int deref_b(int *p);           // defined in p11_ctu_b.cpp

int main() {
  clang_analyzer_eval(from_b(2) == 4);
  return deref_b(nullptr);
}
