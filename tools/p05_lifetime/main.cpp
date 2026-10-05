// p05_lifetime -- drive clang's experimental lifetime-safety analysis directly (Part 5.6).
//
//   p05_lifetime <file> [--func=NAME] [--facts]
//
// Default mode: runLifetimeSafetyAnalysis with a LifetimeSafetyReporter that
// prints reportUseAfterFree / reportUseAfterReturn / suggestAnnotation.
// --facts: use lifetimes::internal::LifetimeSafetyAnalysis and dump the facts
// (Issue / Expire / OriginFlow / Use / OriginEscapes) per CFG block.

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Analyses/LifetimeSafety/LifetimeSafety.h"

using namespace clang;
using namespace clang::lifetimes;

static llvm::cl::OptionCategory Cat("p05_lifetime options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::desc("only this function"),
                                           llvm::cl::cat(Cat));
static llvm::cl::opt<bool> FactsFlag("facts", llvm::cl::desc("dump the generated facts instead of running the checker"),
                                     llvm::cl::cat(Cat));

namespace {

const char *conf(Confidence C) {
  switch (C) {
  case Confidence::None: return "None";
  case Confidence::Maybe: return "Maybe";
  case Confidence::Definite: return "Definite";
  }
  return "?";
}

class Printer : public LifetimeSafetyReporter {
public:
  explicit Printer(ASTContext &C) : SM(C.getSourceManager()) {}
  void reportUseAfterFree(const Expr *Issue, const Expr *Use, SourceLocation Free, Confidence C) override {
    ++Events;
    llvm::outs() << "  reportUseAfterFree   borrowed-at=" << line(Issue->getExprLoc()) << " expired-at=" << line(Free)
                 << " used-at=" << line(Use->getExprLoc()) << " confidence=" << conf(C) << "\n";
  }
  void reportUseAfterReturn(const Expr *Issue, const Expr *Escape, SourceLocation Expiry, Confidence C) override {
    ++Events;
    llvm::outs() << "  reportUseAfterReturn borrowed-at=" << line(Issue->getExprLoc())
                 << " returned-at=" << line(Escape->getExprLoc()) << " confidence=" << conf(C) << "\n";
  }
  void suggestAnnotation(SuggestionScope S, const ParmVarDecl *P, const Expr *Escape) override {
    ++Events;
    llvm::outs() << "  suggestAnnotation    param=" << P->getName() << "\n";
  }
  unsigned Events = 0;

private:
  unsigned line(SourceLocation L) { return SM.getSpellingLineNumber(L); }
  const SourceManager &SM;
};

} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        AnalysisDeclContext AC(nullptr, FD);
        CFG::BuildOptions &BO = AC.getCFGBuildOptions();
        BO.PruneTriviallyFalseEdges = true;
        BO.AddEHEdges = false;
        BO.AddInitializers = true;
        BO.AddImplicitDtors = true;
        BO.AddTemporaryDtors = true;
        BO.AddCXXNewAllocator = false;
        BO.AddCXXDefaultInitExprInCtors = true;
        BO.AddLifetime = true; // LifetimeEnds elements are the "Expire" facts
        BO.setAllAlwaysAdd();
        if (!AC.getCFG()) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        Printer P(Ctx);
        if (FactsFlag) {
          internal::LifetimeSafetyAnalysis A(AC, &P);
          A.run();
          llvm::outs().flush(); // FactManager::dump writes to stderr
          A.getFactManager().dump(*AC.getCFG(), AC);
          llvm::errs().flush();
          return;
        }
        LifetimeSafetyStats Stats;
        runLifetimeSafetyAnalysis(AC, &P, Stats, /*CollectStats=*/false);
        if (P.Events == 0) llvm::outs() << "  (no events)\n";
      });
}
