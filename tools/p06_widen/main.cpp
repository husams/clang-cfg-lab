// p06_widen -- an interval analysis: infinite-height lattice, widening, MaxBlockVisits (Part 6.3)
//
//   build/bin/p06_widen manifests/p06_widen.cpp --func=unbounded --widen
//
// Flags:
//   --widen            give the lattice a widen() member (otherwise only join + ==)
//   --refine           add transferBranch(): `i < 10` narrows i on each edge
//   --max-visits=N     runDataflowAnalysis's MaxBlockVisits argument
#include "cfglab.h"

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/MapLattice.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

#include <algorithm>
#include <climits>
#include <map>

using namespace clang;
using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p06_widen options");
static llvm::cl::opt<std::string> OptFunc("func", llvm::cl::desc("only this function"),
                                          llvm::cl::init(""), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptWiden("widen", llvm::cl::desc("lattice has widen()"),
                                    llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptRefine("refine", llvm::cl::desc("transferBranch narrowing"),
                                     llvm::cl::cat(Cat));
static llvm::cl::opt<int> OptMaxVisits("max-visits", llvm::cl::desc("MaxBlockVisits"),
                                       llvm::cl::init(kDefaultMaxBlockVisits), llvm::cl::cat(Cat));

namespace {
constexpr long long NegInf = LLONG_MIN, PosInf = LLONG_MAX;
int Joins = 0, Widens = 0;

// [Lo, Hi], with +-infinity as the extreme long long values. Bottom == "no value".
struct Interval {
  bool Bottom = true;
  long long Lo = 0, Hi = 0;

  static Interval of(long long L, long long H) { return {false, L, H}; }
  static Interval top() { return {false, NegInf, PosInf}; }

  LatticeEffect join(const Interval &O) {
    if (O.Bottom) return LatticeEffect::Unchanged;
    if (Bottom) { *this = O; return LatticeEffect::Changed; }
    Interval Old = *this;
    Lo = std::min(Lo, O.Lo);
    Hi = std::max(Hi, O.Hi);
    return *this == Old ? LatticeEffect::Unchanged : LatticeEffect::Changed;
  }
  // Classic interval widening: a bound that moved since Prev jumps to infinity.
  LatticeEffect widen(const Interval &Prev) {
    if (Prev.Bottom || Bottom) return LatticeEffect::Unchanged;
    Interval Old = *this;
    Lo = Lo < Prev.Lo ? NegInf : Prev.Lo;
    Hi = Hi > Prev.Hi ? PosInf : Prev.Hi;
    return *this == Prev ? LatticeEffect::Unchanged : LatticeEffect::Changed; (void)Old;
  }
  bool operator==(const Interval &O) const {
    return Bottom == O.Bottom && (Bottom || (Lo == O.Lo && Hi == O.Hi));
  }
  std::string str() const {
    if (Bottom) return "bottom";
    auto B = [](long long V) { return V == NegInf ? std::string("-inf") : V == PosInf ? std::string("+inf") : std::to_string(V); };
    return "[" + B(Lo) + "," + B(Hi) + "]";
  }
};

long long addSat(long long A, long long B) {
  if (A == NegInf || B == NegInf) return NegInf;
  if (A == PosInf || B == PosInf) return PosInf;
  return A + B;
}

// Lattice without widening: only join + ==.
struct PlainMap : VarMapLattice<Interval> {
  LatticeEffect join(const PlainMap &O) {
    ++Joins;
    return VarMapLattice<Interval>::join(O);
  }
};
// Lattice with widening: join + == + widen(Previous).
struct WidenMap : PlainMap {
  LatticeEffect widen(const WidenMap &Prev) {
    ++Widens;
    LatticeEffect E = LatticeEffect::Unchanged;
    for (auto &[V, I] : *this) {
      auto It = Prev.find(V);
      if (It == Prev.end()) { E = LatticeEffect::Changed; continue; }
      Interval Was = It->second;
      I.widen(It->second);
      if (!(I == Was)) E = LatticeEffect::Changed;
    }
    for (auto &[V, I] : Prev)
      if (!contains(V)) E = LatticeEffect::Changed;
    return E;
  }
};

template <typename L>
Interval evalI(const Expr *E, const L &S) {
  E = E->IgnoreParenImpCasts();
  if (auto *I = dyn_cast<IntegerLiteral>(E)) {
    long long V = I->getValue().getSExtValue();
    return Interval::of(V, V);
  }
  if (auto *D = dyn_cast<DeclRefExpr>(E))
    if (auto *V = dyn_cast<VarDecl>(D->getDecl())) {
      auto It = S.find(V);
      return It == S.end() ? Interval::top() : It->second;
    }
  if (auto *B = dyn_cast<BinaryOperator>(E); B && (B->getOpcode() == BO_Add || B->getOpcode() == BO_Sub)) {
    Interval A = evalI(B->getLHS(), S), C = evalI(B->getRHS(), S);
    if (A.Bottom || C.Bottom) return Interval();
    if (B->getOpcode() == BO_Add) return Interval::of(addSat(A.Lo, C.Lo), addSat(A.Hi, C.Hi));
    // a - c: [a.lo - c.hi, a.hi - c.lo]
    auto neg = [](long long V) { return V == NegInf ? PosInf : V == PosInf ? NegInf : -V; };
    return Interval::of(addSat(A.Lo, neg(C.Hi)), addSat(A.Hi, neg(C.Lo)));
  }
  return Interval::top();
}

template <typename L>
class IntervalAnalysis : public DataflowAnalysis<IntervalAnalysis<L>, L> {
public:
  explicit IntervalAnalysis(ASTContext &C) : DataflowAnalysis<IntervalAnalysis<L>, L>(C) {}
  static L initialElement() { return {}; }

  void transfer(const CFGElement &Elt, L &S, Environment &) {
    auto CS = Elt.getAs<CFGStmt>();
    if (!CS) return;
    const Stmt *St = CS->getStmt();
    if (auto *DS = dyn_cast<DeclStmt>(St)) {
      for (Decl *D : DS->decls())
        if (auto *V = dyn_cast<VarDecl>(D))
          if (V->getType()->isIntegerType()) S[V] = V->hasInit() ? evalI(V->getInit(), S) : Interval();
    } else if (auto *B = dyn_cast<BinaryOperator>(St); B && B->isAssignmentOp()) {
      if (auto *D = dyn_cast<DeclRefExpr>(B->getLHS()->IgnoreParenImpCasts()))
        if (auto *V = dyn_cast<VarDecl>(D->getDecl())) {
          if (B->getOpcode() == BO_Assign) S[V] = evalI(B->getRHS(), S);
          else S[V] = Interval::top();
        }
    } else if (auto *U = dyn_cast<UnaryOperator>(St); U && U->isIncrementDecrementOp()) {
      if (auto *D = dyn_cast<DeclRefExpr>(U->getSubExpr()->IgnoreParenImpCasts()))
        if (auto *V = dyn_cast<VarDecl>(D->getDecl())) {
          Interval A = S.contains(V) ? S[V] : Interval::top();
          long long Dlt = U->isIncrementOp() ? 1 : -1;
          S[V] = A.Bottom ? A : Interval::of(addSat(A.Lo, Dlt), addSat(A.Hi, Dlt));
        }
    }
  }

  // Narrow `var < const` (and friends) on each outgoing edge of the condition.
  void transferBranch(bool Branch, const Stmt *Cond, L &S, Environment &) {
    if (!OptRefine) return;
    auto *E = dyn_cast_or_null<Expr>(Cond);
    if (!E) return;
    auto *B = dyn_cast<BinaryOperator>(E->IgnoreParenImpCasts());
    if (!B || !B->isComparisonOp()) return;
    auto *D = dyn_cast<DeclRefExpr>(B->getLHS()->IgnoreParenImpCasts());
    auto *V = D ? dyn_cast<VarDecl>(D->getDecl()) : nullptr;
    auto *K = dyn_cast<IntegerLiteral>(B->getRHS()->IgnoreParenImpCasts());
    if (!V || !K || !S.contains(V)) return;
    long long C = K->getValue().getSExtValue();
    BinaryOperatorKind Op = B->getOpcode();
    if (!Branch) { // the false edge holds the negated comparison
      Op = Op == BO_LT ? BO_GE : Op == BO_LE ? BO_GT : Op == BO_GT ? BO_LE : Op == BO_GE ? BO_LT : Op;
    }
    Interval &I = S[V];
    if (I.Bottom) return;
    switch (Op) {
    case BO_LT: I.Hi = std::min(I.Hi, C - 1); break;
    case BO_LE: I.Hi = std::min(I.Hi, C); break;
    case BO_GT: I.Lo = std::max(I.Lo, C + 1); break;
    case BO_GE: I.Lo = std::max(I.Lo, C); break;
    default: return;
    }
    if (I.Lo > I.Hi) I = Interval(); // contradiction: this edge is infeasible
  }
};

template <typename L>
void run(const FunctionDecl *FD, ASTContext &Ctx, const AdornedCFG &ACFG) {
  const SourceManager &SM = Ctx.getSourceManager();
  DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>());
  Environment Env(DACtx, *FD);
  IntervalAnalysis<L> A(Ctx);
  std::string RetStr = "(not reached)";
  CFGEltCallbacks<IntervalAnalysis<L>> CB;
  CB.Before = [&](const CFGElement &E, const DataflowAnalysisState<L> &St) {
    if (auto S = E.getAs<CFGStmt>())
      if (auto *R = dyn_cast<ReturnStmt>(S->getStmt()); R && R->getRetValue())
        RetStr = evalI(R->getRetValue(), St.Lattice).str();
  };
  Joins = Widens = 0;
  auto Res = runDataflowAnalysis(ACFG, A, Env, CB, OptMaxVisits);
  if (!Res) {
    llvm::outs() << "  analysis failed: " << llvm::toString(Res.takeError()) << "\n";
    llvm::outs() << "  (joins so far: " << Joins << ", widens: " << Widens << ")\n";
    return;
  }
  for (size_t I = Res->size(); I-- > 0;) {
    llvm::outs() << "  B" << I << ": ";
    if (!(*Res)[I]) { llvm::outs() << "(none)\n"; continue; }
    std::vector<std::pair<const VarDecl *, Interval>> Items((*Res)[I]->Lattice.begin(), (*Res)[I]->Lattice.end());
    std::sort(Items.begin(), Items.end(), [&](auto &X, auto &Y) { return SM.isBeforeInTranslationUnit(X.first->getLocation(), Y.first->getLocation()); });
    bool First = true;
    for (auto &[V, Iv] : Items) { llvm::outs() << (First ? "" : "  ") << V->getName() << "=" << Iv.str(); First = false; }
    if (First) llvm::outs() << "(empty)";
    llvm::outs() << "\n";
  }
  llvm::outs() << "  return value: " << RetStr << "\n";
  llvm::outs() << "  joins=" << Joins << " widens=" << Widens << "\n";
}
} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!OptFunc.empty() && FD->getNameAsString() != OptFunc) return;
        auto ACFG = AdornedCFG::build(*FD);
        if (!ACFG) { llvm::errs() << llvm::toString(ACFG.takeError()) << "\n"; return; }
        llvm::outs() << "== " << FD->getNameAsString() << (OptWiden ? "  [widen]" : "  [no widen]")
                     << (OptRefine ? " [refine]" : "") << "\n";
        if (OptWiden) run<WidenMap>(FD, Ctx, *ACFG);
        else run<PlainMap>(FD, Ctx, *ACFG);
      });
}
