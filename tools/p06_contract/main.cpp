// p06_contract -- trace every call the framework makes into your analysis (Part 6.1, 6.2)
//
//   build/bin/p06_contract manifests/p06_contract.cpp --func=branch
//
// The lattice is a powerset: the set of variables that MAY have been assigned.
// Join is set union. Every member the DataflowAnalysis CRTP contract talks
// about prints a line when the framework calls it.
#include "cfglab.h"

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

#include <set>

using namespace clang;
using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p06_contract options");
static llvm::cl::opt<std::string> OptFunc("func", llvm::cl::desc("only this function"),
                                          llvm::cl::init(""), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptQuiet("no-transfer",
                                    llvm::cl::desc("do not print transfer() lines"),
                                    llvm::cl::cat(Cat));

namespace {
std::string setStr(const std::set<std::string> &S) {
  std::string Out = "{";
  for (auto &X : S) Out += (Out.size() > 1 ? "," : "") + X;
  return Out + "}";
}

// The lattice: a bounded join-semilattice must provide join() and operator==.
struct MayAssigned {
  std::set<std::string> Vars;

  LatticeEffect join(const MayAssigned &O) {
    auto Before = Vars.size();
    Vars.insert(O.Vars.begin(), O.Vars.end());
    LatticeEffect E = Vars.size() == Before ? LatticeEffect::Unchanged : LatticeEffect::Changed;
    llvm::outs() << "    join " << setStr(Vars) << " <- " << setStr(O.Vars) << "  => "
                 << (E == LatticeEffect::Changed ? "Changed" : "Unchanged") << "\n";
    return E;
  }
  bool operator==(const MayAssigned &O) const { return Vars == O.Vars; }
};

class Tracer : public DataflowAnalysis<Tracer, MayAssigned> {
public:
  explicit Tracer(ASTContext &C) : DataflowAnalysis<Tracer, MayAssigned>(C) {}

  MayAssigned initialElement() {
    llvm::outs() << "  initialElement()\n";
    return {};
  }

  void transfer(const CFGElement &E, MayAssigned &L, Environment &) {
    auto S = E.getAs<CFGStmt>();
    if (!S) return;
    const Stmt *St = S->getStmt();
    auto *B = dyn_cast<BinaryOperator>(St);
    if (B && B->isAssignmentOp())
      if (auto *D = dyn_cast<DeclRefExpr>(B->getLHS()->IgnoreParenImpCasts())) {
        L.Vars.insert(D->getDecl()->getNameAsString());
        if (!OptQuiet)
          llvm::outs() << "    transfer  " << cfglab::stmtText(St, getASTContext(), 20)
                       << "  => " << setStr(L.Vars) << "\n";
      }
  }

  // Optional member: found by SFINAE (Rank0/Rank1 overloads in DataflowAnalysis.h).
  void transferBranch(bool Branch, const Stmt *Cond, MayAssigned &L, Environment &) {
    llvm::outs() << "    transferBranch(" << (Branch ? "true " : "false") << ", `"
                 << cfglab::stmtText(Cond, getASTContext(), 16) << "`)\n";
  }
};
} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!OptFunc.empty() && FD->getNameAsString() != OptFunc) return;
        auto ACFG = AdornedCFG::build(*FD);
        if (!ACFG) {
          llvm::errs() << llvm::toString(ACFG.takeError()) << "\n";
          return;
        }
        DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>());
        Environment Env(DACtx, *FD);
        Tracer A(Ctx);
        llvm::outs() << "== " << FD->getNameAsString() << "  (analysis starts)\n";
        auto Res = runDataflowAnalysis(*ACFG, A, Env);
        if (!Res) {
          llvm::outs() << "failed: " << llvm::toString(Res.takeError()) << "\n";
          return;
        }
        llvm::outs() << "== done; state at end of each block:\n";
        for (size_t I = Res->size(); I-- > 0;)
          llvm::outs() << "  B" << I << " "
                       << ((*Res)[I] ? setStr((*Res)[I]->Lattice.Vars) : std::string("(none)"))
                       << "\n";
      });
}
