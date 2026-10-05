// p06_sat -- Arena, Formula and the SAT solver, with no C++ input at all (Part 6.5)
//
//   build/bin/p06_sat --demo=arena     interning and simplification
//   build/bin/p06_sat --demo=solver    solve(), proves, allows
//   build/bin/p06_sat --demo=flow      flow conditions: assume, fork, join
//   build/bin/p06_sat --demo=limit     the solver's work limit
#include "clang/Analysis/FlowSensitive/Arena.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysisContext.h"
#include "clang/Analysis/FlowSensitive/Formula.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p06_sat options");
static llvm::cl::opt<std::string> OptDemo("demo", llvm::cl::init("arena"), llvm::cl::cat(Cat));

static const char *yn(bool B) { return B ? "true" : "false"; }

static const char *statusName(Solver::Result::Status S) {
  switch (S) {
  case Solver::Result::Status::Satisfiable: return "Satisfiable";
  case Solver::Result::Status::Unsatisfiable: return "Unsatisfiable";
  case Solver::Result::Status::TimedOut: return "TimedOut";
  }
  return "?";
}

// Is (F1 & F2 & ...) satisfiable? Print the formulas and the verdict.
static Solver::Result check(Solver &S, llvm::ArrayRef<const Formula *> Fs, const char *What) {
  Solver::Result R = S.solve(Fs);
  llvm::outs() << "  " << What << ": " << statusName(R.getStatus());
  if (auto Model = R.getSolution()) { // keep the optional alive: getSolution() returns by value
    std::vector<std::pair<unsigned, bool>> Sol;
    for (auto &[A, V] : *Model) Sol.push_back({(unsigned)A, V == Solver::Result::Assignment::AssignedTrue});
    std::sort(Sol.begin(), Sol.end());
    llvm::outs() << "  model:";
    for (auto &[A, V] : Sol) llvm::outs() << " V" << A << "=" << (V ? "T" : "F");
  }
  llvm::outs() << "\n";
  return R;
}

int main(int argc, const char **argv) {
  llvm::cl::HideUnrelatedOptions(Cat);
  llvm::cl::ParseCommandLineOptions(argc, argv, "p06_sat\n");

  if (OptDemo == "arena") {
    Arena A;
    const Formula &P = A.makeAtomRef(A.makeAtom()); // V0
    const Formula &Q = A.makeAtomRef(A.makeAtom()); // V1
    llvm::outs() << "  atoms:           P = " << P << ", Q = " << Q << "\n";
    llvm::outs() << "  P & Q            " << A.makeAnd(P, Q) << "\n";
    llvm::outs() << "  P | !Q           " << A.makeOr(P, A.makeNot(Q)) << "\n";
    llvm::outs() << "  P => Q           " << A.makeImplies(P, Q) << "\n";
    llvm::outs() << "  P <=> Q          " << A.makeEquals(P, Q) << "\n";
    llvm::outs() << "  interned:        And(P,Q) == And(Q,P)  : "
                 << (&A.makeAnd(P, Q) == &A.makeAnd(Q, P) ? "same object" : "different") << "\n";
    llvm::outs() << "  simplified:      Or(P,P) is P          : " << (&A.makeOr(P, P) == &P ? "yes" : "no") << "\n";
    llvm::outs() << "  simplified:      Not(Not(P)) is P      : " << (&A.makeNot(A.makeNot(P)) == &P ? "yes" : "no") << "\n";
    llvm::outs() << "  simplified:      And(P, true) is P     : " << (&A.makeAnd(P, A.makeLiteral(true)) == &P ? "yes" : "no") << "\n";
    llvm::outs() << "  simplified:      And(P, false)         : " << A.makeAnd(P, A.makeLiteral(false)) << "\n";
    auto Parsed = A.parseFormula("(V0 | !V1)");
    llvm::outs() << "  parseFormula:    \"(V0 | !V1)\" -> " << (Parsed ? (&*Parsed == &A.makeOr(P, A.makeNot(Q)) ? "the same interned formula" : "a different formula") : "error") << "\n";
    llvm::outs() << "  kinds:           ";
    for (const Formula *F : {&P, &A.makeLiteral(true), &A.makeNot(P), &A.makeAnd(P, Q), &A.makeOr(P, Q), &A.makeImplies(P, Q), &A.makeEquals(P, Q)}) {
      static const char *N[] = {"AtomRef", "Literal", "Not", "And", "Or", "Implies", "Equal"};
      llvm::outs() << N[F->kind()] << "(" << F->numOperands(F->kind()) << ") ";
    }
    llvm::outs() << "\n";
    Formula::AtomNames Names;
    Names[P.getAtom()] = "is_null";
    Names[Q.getAtom()] = "is_error";
    llvm::outs() << "  named atoms:     ";
    A.makeOr(P, Q).print(llvm::outs(), &Names);
    llvm::outs() << "\n";
  } else if (OptDemo == "solver") {
    Arena A;
    WatchedLiteralsSolver S;
    const Formula &P = A.makeAtomRef(A.makeAtom());
    const Formula &Q = A.makeAtomRef(A.makeAtom());
    check(S, {&A.makeAnd(P, Q)}, "solve(P & Q)           ");
    check(S, {&A.makeAnd(P, A.makeNot(P))}, "solve(P & !P)          ");
    check(S, {&A.makeImplies(P, Q), &P}, "solve(P => Q, P)       ");
    check(S, {&A.makeImplies(P, Q), &P, &A.makeNot(Q)}, "solve(P => Q, P, !Q)   ");
    llvm::outs() << "  proves/allows are built on solve():\n";
    llvm::outs() << "    proves(F)  == unsat( FC & !F )\n    allows(F)  == sat( FC & F )\n";
    // FC = (P => Q) & P. Does it prove Q?  FC & !Q must be unsat.
    const Formula &FC = A.makeAnd(A.makeImplies(P, Q), P);
    llvm::outs() << "  with FC = (P => Q) & P:\n";
    Solver::Result R1 = S.solve({&FC, &A.makeNot(Q)});
    llvm::outs() << "    proves(Q)  : " << (R1.getStatus() == Solver::Result::Status::Unsatisfiable ? "true" : "false") << "\n";
    Solver::Result R2 = S.solve({&FC, &Q});
    llvm::outs() << "    allows(Q)  : " << (R2.getStatus() == Solver::Result::Status::Satisfiable ? "true" : "false") << "\n";
    Solver::Result R3 = S.solve({&FC, &A.makeNot(P)});
    llvm::outs() << "    allows(!P) : " << (R3.getStatus() == Solver::Result::Status::Satisfiable ? "true" : "false") << "\n";
  } else if (OptDemo == "flow") {
    DataflowAnalysisContext Ctx(std::make_unique<WatchedLiteralsSolver>());
    Arena &A = Ctx.arena();
    const Formula &P = A.makeAtomRef(A.makeAtom());
    const Formula &Q = A.makeAtomRef(A.makeAtom());
    llvm::outs() << "  P = " << P << ", Q = " << Q << "\n";
    Atom FC0 = A.makeFlowConditionToken();
    llvm::outs() << "  FC0 = " << FC0 << " (a fresh token: nothing known)\n";
    llvm::outs() << "  FC0 implies P?  " << yn(Ctx.flowConditionImplies(FC0, P)) << "   allows P? " << yn(Ctx.flowConditionAllows(FC0, P)) << "\n";
    // branch 'if (P)': the true edge forks the flow condition and assumes P.
    Atom T = Ctx.forkFlowCondition(FC0);
    Ctx.addFlowConditionConstraint(T, P);
    Atom F = Ctx.forkFlowCondition(FC0);
    Ctx.addFlowConditionConstraint(F, A.makeNot(P));
    llvm::outs() << "  true  edge " << T << ": implies P? " << yn(Ctx.flowConditionImplies(T, P)) << "  allows !P? " << yn(Ctx.flowConditionAllows(T, A.makeNot(P))) << "\n";
    llvm::outs() << "  false edge " << F << ": implies P? " << yn(Ctx.flowConditionImplies(F, P)) << "  allows P?  " << yn(Ctx.flowConditionAllows(F, P)) << "\n";
    Ctx.addFlowConditionConstraint(T, Q);
    Atom J = Ctx.joinFlowConditions(T, F);
    llvm::outs() << "  join " << J << " = " << T << " | " << F << ":\n";
    llvm::outs() << "     implies Q? " << yn(Ctx.flowConditionImplies(J, Q)) << "   (only the true path knew Q)\n";
    llvm::outs() << "     implies P | !P? " << yn(Ctx.flowConditionImplies(J, A.makeOr(P, A.makeNot(P)))) << "\n";
    llvm::outs() << "     implies P? " << yn(Ctx.flowConditionImplies(J, P)) << "   allows P? " << yn(Ctx.flowConditionAllows(J, P)) << "   allows !P? " << yn(Ctx.flowConditionAllows(J, A.makeNot(P))) << "\n";
    llvm::outs() << "  contradictory path: assume P on the false edge too\n";
    Ctx.addFlowConditionConstraint(F, P);
    llvm::outs() << "     allows anything? allows(true): " << yn(Ctx.flowConditionAllows(F, A.makeLiteral(true))) << "   (unsatisfiable FC allows nothing)\n";
    llvm::outs() << "     implies false:   " << yn(Ctx.flowConditionImplies(F, A.makeLiteral(false))) << "   (and so implies everything)\n";
  } else if (OptDemo == "limit") {
    Arena A;
    // A pigeonhole-style formula: hard for DPLL, tiny to write.
    std::vector<const Formula *> Clauses;
    const int Pigeons = 6, Holes = 5;
    std::vector<std::vector<const Formula *>> X(Pigeons);
    for (int p = 0; p < Pigeons; ++p)
      for (int h = 0; h < Holes; ++h) X[p].push_back(&A.makeAtomRef(A.makeAtom()));
    for (int p = 0; p < Pigeons; ++p) { // every pigeon sits somewhere
      const Formula *Or = X[p][0];
      for (int h = 1; h < Holes; ++h) Or = &A.makeOr(*Or, *X[p][h]);
      Clauses.push_back(Or);
    }
    for (int h = 0; h < Holes; ++h)     // no two pigeons share a hole
      for (int p = 0; p < Pigeons; ++p)
        for (int q = p + 1; q < Pigeons; ++q)
          Clauses.push_back(&A.makeOr(A.makeNot(*X[p][h]), A.makeNot(*X[q][h])));
    llvm::outs() << "  " << Pigeons << " pigeons, " << Holes << " holes: " << Clauses.size() << " clauses (unsatisfiable)\n";
    for (long long Limit : {10LL, 100000LL}) {
      WatchedLiteralsSolver S(Limit);
      Solver::Result R = S.solve(Clauses);
      llvm::outs() << "  WatchedLiteralsSolver(" << Limit << "): " << statusName(R.getStatus())
                   << ", reachedLimit() = " << S.reachedLimit() << "\n";
    }
    llvm::outs() << "  (a timed-out query makes proves() AND allows() return false)\n";
  } else {
    llvm::errs() << "unknown --demo\n";
    return 2;
  }
  return 0;
}
