// p07_ctx -- Part 7.5: the same checker, with and without context-sensitive analysis.
//
//   build/bin/p07_ctx manifests/p07_ctx.cpp --depth=0|1|2.. [--func=NAME] [--explain]
//   --explain lists every non-member call and whether Environment::pushCall can take it
//
// diagnoseFunction() cannot turn context sensitivity on: it builds its own
// DataflowAnalysisContext with default Options. So this tool does what
// diagnoseFunction does, by hand, with Options{ContextSensitiveOpts{Depth}}.

#include "cfglab.h"
#include "../p07_movecheck/MoveModel.h"

#include "clang/Analysis/FlowSensitive/ASTOps.h"
#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

using namespace clang;
using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p07_ctx options");
static llvm::cl::opt<std::string> CxFunc("func", llvm::cl::desc("only this function"), llvm::cl::cat(Cat));
static llvm::cl::opt<int> CxDepth("depth", llvm::cl::desc("ContextSensitiveOptions::Depth (0 = off)"),
                                  llvm::cl::init(0), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> CxNoGuard("no-dead-guard", llvm::cl::desc("do not suppress reports on SAT-dead paths"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> CxExplain("explain", llvm::cl::desc("list each call and whether pushCall is allowed"),
                                     llvm::cl::cat(Cat));

// Re-implementation of the preconditions of Environment::pushCall, as a report.
static const char *whyNot(const CallExpr *CE, const FunctionDecl *Caller, unsigned Depth) {
  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD) return "no direct callee";
  if (!FD->doesThisDeclarationHaveABody()) {
    const FunctionDecl *Def = nullptr;
    if (!FD->hasBody(Def)) return "no body";
  }
  if (FD == Caller) return "recursion (callee is the caller)";
  if (Depth == 0) return "depth 0";
  if (CE->getNumArgs() > FD->getNumParams() && !isa<CXXOperatorCallExpr>(CE)) return "args != params";
  // Header (DataflowEnvironment.h) also demands "no globals in the callee". Experiment
  // 7.5 shows the 22.1.8 engine descends anyway, so it is NOT a refusal here.
  return nullptr;
}

struct CallLister : RecursiveASTVisitor<CallLister> {
  const FunctionDecl *Caller;
  ASTContext &Ctx;
  CallLister(const FunctionDecl *C, ASTContext &X) : Caller(C), Ctx(X) {}
  bool VisitCallExpr(CallExpr *CE) {
    const FunctionDecl *FD = CE->getDirectCallee();
    if (!FD || (FD->getIdentifier() && FD->isInStdNamespace())) return true;
    if (isa<CXXMemberCallExpr>(CE) || isa<CXXOperatorCallExpr>(CE)) return true;
    const char *Why = whyNot(CE, Caller, CxDepth);
    llvm::outs() << "  call " << FD->getName() << " at line "
                 << Ctx.getSourceManager().getSpellingLineNumber(CE->getBeginLoc()) << ": "
                 << (Why ? std::string("not descended (") + Why + ")" : std::string("descendable"));
    if (!Why) {
      const FunctionDecl *Def = FD->getDefinition();
      if (Def && !getReferencedDecls(*Def).Globals.empty()) llvm::outs() << " [callee references a global]";
    }
    llvm::outs() << "\n";
    return true;
  }
};

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!CxFunc.empty() && FD->getNameAsString() != CxFunc) return;
        if (CxNoGuard) p07::DeadPathGuard = false;
        if (CxExplain) {
          llvm::outs() << FD->getNameAsString() << ":\n";
          CallLister(FD, Ctx).TraverseStmt(FD->getBody());
          return;
        }
        auto ACFG = AdornedCFG::build(*FD);
        if (!ACFG) {
          llvm::outs() << FD->getNameAsString() << ": " << llvm::toString(ACFG.takeError()) << "\n";
          return;
        }
        WatchedLiteralsSolver Solver;
        DataflowAnalysisContext::Options Opts;
        if (CxDepth > 0) Opts.ContextSensitiveOpts = ContextSensitiveOptions{(unsigned)CxDepth};
        DataflowAnalysisContext DACtx(Solver, Opts);
        Environment Env(DACtx, *FD);
        p07::MoveAnalysis Analysis(Ctx, Env);
        p07::MoveDiagnoser Diag;
        llvm::SmallVector<p07::MoveDiag> Out;
        CFGEltCallbacks<p07::MoveAnalysis> CB;
        CB.After = [&](const CFGElement &E, const DataflowAnalysisState<NoopLattice> &St) {
          llvm::move(Diag(E, Ctx, TransferStateForDiagnostics<NoopLattice>(St.Lattice, St.Env)),
                     std::back_inserter(Out));
        };
        p07::TransferCalls = 0;
        auto R = runDataflowAnalysis(*ACFG, Analysis, Env, CB);
        llvm::outs() << FD->getNameAsString() << " (depth " << CxDepth << ", " << p07::TransferCalls << " transfers): ";
        if (!R) {
          llvm::outs() << "error: " << llvm::toString(R.takeError()) << "\n";
          return;
        }
        if (Out.empty()) llvm::outs() << "clean\n";
        else {
          llvm::outs() << "\n";
          for (auto &D : Out) llvm::outs() << "  " << p07::formatDiag(Ctx.getSourceManager(), D) << "\n";
        }
      });
}
