// p05_uninit -- UninitializedValues with our own handler (Part 5.3).
//
//   p05_uninit <file> [--func=NAME] [--minimal-cfg]
//
// Calls clang::runUninitializedVariablesAnalysis the way Sema does and prints
// what the UninitVariablesHandler is told.

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Analyses/UninitializedValues.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p05_uninit options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::desc("only this function"),
                                           llvm::cl::cat(Cat));
static llvm::cl::opt<bool> MinimalFlag("minimal-cfg",
                                       llvm::cl::desc("use only the seven alwaysAdd classes Sema sets when no linearized analysis is on"),
                                       llvm::cl::cat(Cat));
static llvm::cl::opt<bool> DefaultFlag("default-cfg", llvm::cl::desc("do not set any alwaysAdd class at all"),
                                       llvm::cl::cat(Cat));

namespace {

const char *kindName(UninitUse::Kind K) {
  switch (K) {
  case UninitUse::Maybe: return "Maybe";
  case UninitUse::Sometimes: return "Sometimes";
  case UninitUse::AfterDecl: return "AfterDecl";
  case UninitUse::AfterCall: return "AfterCall";
  case UninitUse::Always: return "Always";
  }
  return "?";
}

class Printer : public UninitVariablesHandler {
public:
  explicit Printer(ASTContext &C) : Ctx(C) {}

  void handleUseOfUninitVariable(const VarDecl *VD, const UninitUse &Use) override {
    const SourceManager &SM = Ctx.getSourceManager();
    llvm::outs() << "  use of '" << VD->getName() << "' kind=" << kindName(Use.getKind()) << " line "
                 << cfglab::lineOf(SM, Use.getUser()->getBeginLoc());
    if (Use.isConstRefUse()) llvm::outs() << " [const-ref]";
    if (Use.isConstPtrUse()) llvm::outs() << " [const-ptr]";
    for (auto I = Use.branch_begin(), E = Use.branch_end(); I != E; ++I)
      llvm::outs() << "\n    because " << I->Terminator->getStmtClassName() << " at line "
                   << cfglab::lineOf(SM, I->Terminator->getBeginLoc()) << " takes output " << I->Output;
    llvm::outs() << "\n";
  }
  void handleSelfInit(const VarDecl *VD) override {
    llvm::outs() << "  self-init of '" << VD->getName() << "' line "
                 << cfglab::lineOf(Ctx.getSourceManager(), VD->getLocation()) << "\n";
  }

private:
  ASTContext &Ctx;
};

} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        AnalysisDeclContext AC(nullptr, FD);
        CFG::BuildOptions &BO = AC.getCFGBuildOptions();
        // Sema's common options (AnalysisBasedWarnings::IssueWarnings).
        BO.PruneTriviallyFalseEdges = true;
        BO.AddEHEdges = false;
        BO.AddInitializers = true;
        BO.AddImplicitDtors = true;
        BO.AddTemporaryDtors = true;
        BO.AddCXXNewAllocator = false;
        BO.AddCXXDefaultInitExprInCtors = true;
        if (DefaultFlag) {
          // nothing
        } else if (MinimalFlag) {
          BO.setAlwaysAdd(Stmt::BinaryOperatorClass).setAlwaysAdd(Stmt::CompoundAssignOperatorClass)
              .setAlwaysAdd(Stmt::BlockExprClass).setAlwaysAdd(Stmt::CStyleCastExprClass)
              .setAlwaysAdd(Stmt::DeclRefExprClass).setAlwaysAdd(Stmt::ImplicitCastExprClass)
              .setAlwaysAdd(Stmt::UnaryOperatorClass);
        } else {
          BO.setAllAlwaysAdd();
        }
        CFG *G = AC.getCFG();
        if (!G) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        Printer P(Ctx);
        UninitVariablesAnalysisStats Stats{};
        runUninitializedVariablesAnalysis(*FD, *G, AC, P, Stats);
        llvm::outs() << "  stats: variables=" << Stats.NumVariablesAnalyzed
                     << " blockVisits=" << Stats.NumBlockVisits << "\n";
      });
}
