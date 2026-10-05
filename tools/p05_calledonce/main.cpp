// p05_calledonce -- checkCalledOnceParameters with a printing handler (Part 5.5).
//
//   p05_calledonce <file> [--func=NAME] [--no-conventions] -- -std=c++17 -fblocks
//
// Sema calls the checker only for Objective-C; nothing stops a tool from calling it on C++.

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Analyses/CalledOnceCheck.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p05_calledonce options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::desc("only this function"),
                                           llvm::cl::cat(Cat));
static llvm::cl::opt<bool> NoConvFlag("no-conventions", llvm::cl::desc("CheckConventionalParameters=false"),
                                      llvm::cl::cat(Cat));

namespace {

const char *reason(NeverCalledReason R) {
  switch (R) {
  case NeverCalledReason::IfThen: return "IfThen";
  case NeverCalledReason::IfElse: return "IfElse";
  case NeverCalledReason::Switch: return "Switch";
  case NeverCalledReason::SwitchSkipped: return "SwitchSkipped";
  case NeverCalledReason::LoopEntered: return "LoopEntered";
  case NeverCalledReason::LoopSkipped: return "LoopSkipped";
  case NeverCalledReason::FallbackReason: return "FallbackReason";
  }
  return "?";
}

class Printer : public CalledOnceCheckHandler {
public:
  explicit Printer(ASTContext &C) : SM(C.getSourceManager()) {}
  void handleDoubleCall(const ParmVarDecl *P, const Expr *Call, const Expr *Prev, bool CH, bool Poised) override {
    ++Events;
    llvm::outs() << "  handleDoubleCall param=" << P->getName() << " call-line=" << line(Call->getBeginLoc())
                 << " previous-line=" << line(Prev->getBeginLoc()) << " completion-handler=" << CH << "\n";
  }
  void handleNeverCalled(const ParmVarDecl *P, bool CH) override {
    ++Events;
    llvm::outs() << "  handleNeverCalled(param) param=" << P->getName() << " completion-handler=" << CH << "\n";
  }
  void handleNeverCalled(const ParmVarDecl *P, const Decl *Fn, const Stmt *Where, NeverCalledReason R,
                         bool Direct, bool CH) override {
    ++Events;
    llvm::outs() << "  handleNeverCalled(branch) param=" << P->getName() << " at line " << line(Where->getBeginLoc())
                 << " reason=" << reason(R) << " called-directly=" << Direct << " completion-handler=" << CH << "\n";
  }
  void handleCapturedNeverCalled(const ParmVarDecl *P, const Decl *, bool CH) override {
    ++Events;
    llvm::outs() << "  handleCapturedNeverCalled param=" << P->getName() << "\n";
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
        cfglab::applyPreset(AC.getCFGBuildOptions(), cfglab::semaPreset());
        if (!AC.getCFG()) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        Printer P(Ctx);
        checkCalledOnceParameters(AC, P, !NoConvFlag);
        if (P.Events == 0) llvm::outs() << "  (no events)\n";
      });
}
