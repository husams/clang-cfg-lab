// p06_taint -- a taint analysis built from CFGMatchSwitch and a ValueModel (Part 6.6)
//
//   build/bin/p06_taint manifests/p06_taint.cpp
//   build/bin/p06_taint manifests/p06_taint.cpp --func=branch --no-join-model
//
// There is no lattice at all (NoopLattice). The fact "this int is tainted" is a
// *property* on the IntegerValue, holding a BoolValue:
//   source()        -> new IntegerValue, property tainted = true
//   a + b           -> new IntegerValue, property tainted = taint(a) | taint(b)
//   sanitize(x)     -> new IntegerValue, property tainted = false
//   merge of paths  -> ValueModel::join ties the new value's property to the path conditions
// sink(x) is checked after the fixpoint with proves() / allows().
#include "cfglab.h"

#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/CFGMatchSwitch.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/NoopLattice.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

#include <algorithm>

using namespace clang;
using namespace clang::dataflow;
using namespace clang::ast_matchers;

static llvm::cl::OptionCategory Cat("p06_taint options");
static llvm::cl::opt<std::string> OptFunc("func", llvm::cl::desc("only this function"),
                                          llvm::cl::init(""), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptNoJoin("no-join-model", llvm::cl::desc("do not override ValueModel::join"),
                                     llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptNaiveJoin("naive-join", llvm::cl::desc("trust flow-condition tokens to be disjoint"),
                                        llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptTrace("trace-cases", llvm::cl::desc("print each match-switch case that fires"),
                                    llvm::cl::cat(Cat));

namespace {
int JoinCalls = 0, WidenCalls = 0, CompareCalls = 0;
constexpr llvm::StringLiteral Prop = "tainted";

BoolValue &taintOf(const Value *V, Environment &Env) {
  if (V)
    if (auto *B = cast_or_null<BoolValue>(V->getProperty(Prop))) return *B;
  return Env.getBoolLiteralValue(false);
}
BoolValue &taintOf(const Value *V, const Environment &Env) {
  return taintOf(V, const_cast<Environment &>(Env)); // only builds literals/formulas
}

class TaintAnalysis : public DataflowAnalysis<TaintAnalysis, NoopLattice> {
  using State = TransferState<NoopLattice>;
  CFGMatchSwitch<State> Switch;

  static void trace(const char *What, const Stmt *S, ASTContext &C) {
    if (OptTrace)
      llvm::outs() << "    case: " << What << "  `" << cfglab::stmtText(S, C, 24) << "`\n";
  }

  static CFGMatchSwitch<State> build() {
    return CFGMatchSwitchBuilder<State>()
        .CaseOfCFGStmt<CallExpr>(
            callExpr(callee(functionDecl(hasName("source")))),
            [](const CallExpr *CE, const MatchFinder::MatchResult &R, State &S) {
              trace("source()", CE, *R.Context);
              auto &V = S.Env.create<IntegerValue>();
              V.setProperty(Prop, S.Env.getBoolLiteralValue(true));
              S.Env.setValue(*CE, V);
            })
        .CaseOfCFGStmt<CallExpr>(
            callExpr(callee(functionDecl(hasName("sanitize")))),
            [](const CallExpr *CE, const MatchFinder::MatchResult &R, State &S) {
              trace("sanitize()", CE, *R.Context);
              auto &V = S.Env.create<IntegerValue>();
              V.setProperty(Prop, S.Env.getBoolLiteralValue(false));
              S.Env.setValue(*CE, V);
            })
        .CaseOfCFGStmt<BinaryOperator>(
            binaryOperator(anyOf(hasOperatorName("+"), hasOperatorName("-"), hasOperatorName("*"))),
            [](const BinaryOperator *BO, const MatchFinder::MatchResult &R, State &S) {
              trace("arithmetic", BO, *R.Context);
              BoolValue &L = taintOf(S.Env.getValue(*BO->getLHS()), S.Env);
              BoolValue &Rr = taintOf(S.Env.getValue(*BO->getRHS()), S.Env);
              auto &V = S.Env.create<IntegerValue>();
              V.setProperty(Prop, S.Env.makeOr(L, Rr));
              S.Env.setValue(*BO, V);
            })
        .Build();
  }

public:
  explicit TaintAnalysis(ASTContext &C) : DataflowAnalysis<TaintAnalysis, NoopLattice>(C), Switch(build()) {}
  static NoopLattice initialElement() { return {}; }

  void transfer(const CFGElement &Elt, NoopLattice &L, Environment &Env) {
    State S(L, Env);
    Switch(Elt, getASTContext(), S);
  }

  // --- Environment::ValueModel -------------------------------------------------
  // Two different Values reached the same location from two paths. The joined
  // Value is fresh; say what its property is.
  void join(QualType Type, const Value &Val1, const Environment &Env1, const Value &Val2,
            const Environment &Env2, Value &JoinedVal, Environment &JoinedEnv) override {
    ++JoinCalls;
    if (OptNoJoin) return;
    Arena &A = JoinedEnv.arena();
    BoolValue &T1 = taintOf(&Val1, Env1), &T2 = taintOf(&Val2, Env2);
    BoolValue &B = JoinedEnv.makeAtomicBoolValue();
    const Formula &FC1 = A.makeAtomRef(Env1.getFlowConditionToken());
    const Formula &FC2 = A.makeAtomRef(Env2.getFlowConditionToken());
    // Flow-condition tokens mean "this point MAY have been reached". They are
    // mutually exclusive after an if/else, but NOT at a loop head (the entry
    // path is an ancestor of the back-edge path). Only trust "FC1 => B<=>t1,
    // FC2 => B<=>t2" when the two paths cannot both be taken.
    bool Disjoint = OptNaiveJoin || !JoinedEnv.allows(A.makeAnd(FC1, FC2));
    if (Disjoint) {
      JoinedEnv.assume(A.makeImplies(FC1, A.makeEquals(B.formula(), T1.formula())));
      JoinedEnv.assume(A.makeImplies(FC2, A.makeEquals(B.formula(), T2.formula())));
    } else {
      if (Env1.proves(T1.formula()) && Env2.proves(T2.formula())) JoinedEnv.assume(B.formula());
      if (!Env1.allows(T1.formula()) && !Env2.allows(T2.formula()))
        JoinedEnv.assume(A.makeNot(B.formula()));
      // otherwise B stays unconstrained: "may be tainted"
    }
    JoinedVal.setProperty(Prop, B);
  }

  // Loop back edges are WIDENED, not compared (see Section 6.3). The default
  // widen() forgets a Value that changed; here we collapse the property to the
  // two-point lattice {clean, may-be-tainted} with literal formulas, which are
  // stable and so reach a fixed point.
  std::optional<WidenResult> widen(QualType Type, Value &Prev, const Environment &PrevEnv,
                                   Value &Current, Environment &CurrentEnv) override {
    ++WidenCalls;
    if ((!Prev.getProperty(Prop) && !Current.getProperty(Prop))) return std::nullopt;
    bool PrevMay = PrevEnv.allows(taintOf(&Prev, PrevEnv).formula());
    bool CurMay = CurrentEnv.allows(taintOf(&Current, CurrentEnv).formula());
    bool Now = PrevMay || CurMay;
    Current.setProperty(Prop, CurrentEnv.getBoolLiteralValue(Now));
    return WidenResult{&Current, Now == PrevMay ? LatticeEffect::Unchanged : LatticeEffect::Changed};
  }

  // Interchangeable values: their taint formulas are the same if their taint formulas are the same.
  ComparisonResult compare(QualType Type, const Value &Val1, const Environment &Env1,
                           const Value &Val2, const Environment &Env2) override {
    ++CompareCalls;
    if (!Val1.getProperty(Prop) || !Val2.getProperty(Prop)) return ComparisonResult::Unknown;
    return &taintOf(&Val1, Env1).formula() == &taintOf(&Val2, Env2).formula()
               ? ComparisonResult::Same : ComparisonResult::Unknown;
  }
};
} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!OptFunc.empty() && FD->getNameAsString() != OptFunc) return;
        auto ACFG = AdornedCFG::build(*FD);
        if (!ACFG) { llvm::errs() << llvm::toString(ACFG.takeError()) << "\n"; return; }
        DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>());
        Environment Env(DACtx, *FD);
        TaintAnalysis A(Ctx);
        llvm::outs() << "== " << FD->getNameAsString() << "\n";

        std::vector<std::pair<unsigned, std::string>> Out;
        CFGEltCallbacks<TaintAnalysis> CB;
        CB.Before = [&](const CFGElement &E, const DataflowAnalysisState<NoopLattice> &St) {
          auto S = E.getAs<CFGStmt>();
          if (!S) return;
          auto *CE = dyn_cast<CallExpr>(S->getStmt());
          auto *Callee = CE ? CE->getDirectCallee() : nullptr;
          if (!Callee || Callee->getName() != "sink") return;
          unsigned Line = Ctx.getSourceManager().getSpellingLineNumber(CE->getBeginLoc());
          const Value *V = St.Env.getValue(*CE->getArg(0));
          std::string Verdict;
          if (!V) Verdict = "no value for the argument";
          else {
            const Formula &T = taintOf(V, St.Env).formula();
            if (St.Env.proves(T)) Verdict = "TAINTED";
            else if (St.Env.allows(T)) Verdict = "MAY BE TAINTED";
            else Verdict = "clean";
          }
          Out.push_back({Line, "  line " + std::to_string(Line) + ": " + cfglab::stmtText(CE, Ctx, 24) + "  " + Verdict});
        };
        JoinCalls = WidenCalls = CompareCalls = 0;
        auto Res = runDataflowAnalysis(*ACFG, A, Env, CB);
        if (!Res) { llvm::outs() << "  analysis failed: " << llvm::toString(Res.takeError()) << "\n"; return; }
        std::stable_sort(Out.begin(), Out.end(), [](auto &X, auto &Y) { return X.first < Y.first; });
        for (auto &L : Out) llvm::outs() << L.second << "\n";
        if (OptTrace)
          llvm::outs() << "  ValueModel calls: join=" << JoinCalls << " widen=" << WidenCalls << " compare=" << CompareCalls << "\n";
      });
}
