// p05_tautology -- CFGCallback the way Sema's LogicalErrorHandler uses it (Part 5.7).
//
//   p05_tautology <file> [--func=NAME]
//
// For each callback we print the hook, the expression, the argument, and
// whether LogicalErrorHandler would stay silent because the expression
// involves a macro expansion (its HasMacroID test).

#include "cfglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p05_tautology options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::desc("only this function"),
                                           llvm::cl::cat(Cat));

namespace {

// Copied from AnalysisBasedWarnings.cpp (LogicalErrorHandler::HasMacroID).
bool hasMacroID(const Expr *E) {
  if (E->getExprLoc().isMacroID()) return true;
  for (const Stmt *Sub : E->children())
    if (const Expr *SubE = dyn_cast_or_null<Expr>(Sub))
      if (hasMacroID(SubE)) return true;
  return false;
}

class Handler : public CFGCallback {
public:
  explicit Handler(ASTContext &C) : Ctx(C) {}
  void logicAlwaysTrue(const BinaryOperator *B, bool T) override { report("logicAlwaysTrue", B, &T); }
  void compareAlwaysTrue(const BinaryOperator *B, bool T) override { report("compareAlwaysTrue", B, &T); }
  void compareBitwiseEquality(const BinaryOperator *B, bool T) override { report("compareBitwiseEquality", B, &T); }
  void compareBitwiseOr(const BinaryOperator *B) override { report("compareBitwiseOr", B, nullptr); }
  unsigned Events = 0;

private:
  void report(const char *Hook, const BinaryOperator *B, const bool *T) {
    ++Events;
    llvm::outs() << "  " << llvm::format("%-22s", Hook) << " line "
                 << cfglab::lineOf(Ctx.getSourceManager(), B->getBeginLoc());
    if (T) llvm::outs() << " arg=" << (*T ? "true " : "false");
    else llvm::outs() << "          ";
    llvm::outs() << " sema=" << (hasMacroID(B) ? "silent (macro)" : "warns") << "  `" << cfglab::stmtText(B, Ctx) << "`\n";
  }
  ASTContext &Ctx;
};

} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        Handler H(Ctx);
        // The Observer is read while the CFG is built: no analysis runs afterwards.
        CFG::BuildOptions BO = cfglab::semaPreset();
        BO.Observer = &H;
        CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (H.Events == 0) llvm::outs() << "  (no callbacks)\n";
      });
}
