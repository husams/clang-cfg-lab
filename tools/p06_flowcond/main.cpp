// p06_flowcond -- a SAT-backed redundant-condition detector (Part 6.5)
//
//   build/bin/p06_flowcond manifests/p06_flowcond.cpp
//   build/bin/p06_flowcond manifests/p06_flowcond.cpp --func=nested_same --show-fc
//
// For every `if` condition, ask the Environment at the condition:
//     proves(cond)  -> always true here      proves(!cond) -> always false here
// The framework did the rest: the built-in transfer functions gave the
// condition a BoolValue, and every branch edge added that value (or its
// negation) to the flow condition of the successor.
#include "cfglab.h"

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/NoopAnalysis.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

#include "llvm/ADT/DenseSet.h"

#include <algorithm>

using namespace clang;
using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p06_flowcond options");
static llvm::cl::opt<std::string> OptFunc("func", llvm::cl::desc("only this function"),
                                          llvm::cl::init(""), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptShowFC("show-fc", llvm::cl::desc("print the formula, token and queries"),
                                     llvm::cl::cat(Cat));

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!OptFunc.empty() && FD->getNameAsString() != OptFunc) return;
        auto ACFG = AdornedCFG::build(*FD);
        if (!ACFG) { llvm::errs() << llvm::toString(ACFG.takeError()) << "\n"; return; }

        // Which expressions are `if` conditions?
        llvm::DenseSet<const Stmt *> Conds;
        for (const CFGBlock *B : ACFG->getCFG())
          if (auto *IS = dyn_cast_or_null<IfStmt>(B->getTerminatorStmt())) Conds.insert(IS->getCond());

        DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>());
        Environment Env(DACtx, *FD);
        NoopAnalysis A(Ctx);
        llvm::outs() << "== " << FD->getNameAsString() << "\n";

        // Callbacks arrive in block-ID order; collect and print in source order.
        std::vector<std::pair<unsigned, std::string>> Out;
        CFGEltCallbacks<NoopAnalysis> CB;
        CB.After = [&](const CFGElement &E, const DataflowAnalysisState<NoopLattice> &St) {
          auto S = E.getAs<CFGStmt>();
          if (!S || !Conds.contains(S->getStmt())) return;
          auto *Cond = cast<Expr>(S->getStmt());
          unsigned Line = Ctx.getSourceManager().getSpellingLineNumber(Cond->getBeginLoc());
          std::string Text;
          llvm::raw_string_ostream OS(Text);
          OS << "  line " << Line << ": `" << cfglab::stmtText(Cond, Ctx, 30) << "`  ";
          auto *BV = dyn_cast_or_null<BoolValue>(St.Env.getValue(*Cond));
          if (!BV) {
            OS << "no BoolValue (not modelled)";
          } else {
            const Formula &F = BV->formula();
            bool T = St.Env.proves(F);
            bool Fa = St.Env.proves(St.Env.arena().makeNot(F));
            OS << (T && Fa ? "BOTH PROVEN (unreachable path)" : T ? "ALWAYS TRUE" : Fa ? "ALWAYS FALSE" : "unknown");
            if (OptShowFC)
              OS << "   formula=" << F << " token=" << St.Env.getFlowConditionToken()
                 << " allows(f)=" << St.Env.allows(F)
                 << " allows(!f)=" << St.Env.allows(St.Env.arena().makeNot(F));
          }
          Out.push_back({Line, Text});
        };
        auto Res = runDataflowAnalysis(*ACFG, A, Env, CB);
        if (!Res) llvm::outs() << "  analysis failed: " << llvm::toString(Res.takeError()) << "\n";
        std::stable_sort(Out.begin(), Out.end(), [](auto &X, auto &Y) { return X.first < Y.first; });
        for (auto &L : Out) llvm::outs() << L.second << "\n";
      });
}
