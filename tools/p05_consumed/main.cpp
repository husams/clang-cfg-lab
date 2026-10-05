// p05_consumed -- ConsumedAnalyzer with a handler that prints every warning hook (Part 5.5).
//
//   p05_consumed <file> [--func=NAME]
//
// Sema's ConsumedWarningsHandler collects delayed diagnostics and emits them
// in emitDiagnostics(); ours prints in each hook.

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Analyses/Consumed.h"

using namespace clang;
using namespace clang::consumed;

static llvm::cl::OptionCategory Cat("p05_consumed options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::desc("only this function"),
                                           llvm::cl::cat(Cat));

namespace {

class Printer : public ConsumedWarningsHandlerBase {
public:
  explicit Printer(ASTContext &C) : SM(C.getSourceManager()) {}
  void warnLoopStateMismatch(SourceLocation L, StringRef Var) override {
    say("warnLoopStateMismatch", L, "var=" + Var.str());
  }
  void warnParamReturnTypestateMismatch(SourceLocation L, StringRef Var, StringRef Exp, StringRef Obs) override {
    say("warnParamReturnTypestateMismatch", L, "var=" + Var.str() + " expected=" + Exp.str() + " observed=" + Obs.str());
  }
  void warnParamTypestateMismatch(SourceLocation L, StringRef Exp, StringRef Obs) override {
    say("warnParamTypestateMismatch", L, "expected=" + Exp.str() + " observed=" + Obs.str());
  }
  void warnReturnTypestateForUnconsumableType(SourceLocation L, StringRef Ty) override {
    say("warnReturnTypestateForUnconsumableType", L, "type=" + Ty.str());
  }
  void warnReturnTypestateMismatch(SourceLocation L, StringRef Exp, StringRef Obs) override {
    say("warnReturnTypestateMismatch", L, "expected=" + Exp.str() + " observed=" + Obs.str());
  }
  void warnUseOfTempInInvalidState(StringRef Method, StringRef State, SourceLocation L) override {
    say("warnUseOfTempInInvalidState", L, "method=" + Method.str() + " state=" + State.str());
  }
  void warnUseInInvalidState(StringRef Method, StringRef Var, StringRef State, SourceLocation L) override {
    say("warnUseInInvalidState", L, "method=" + Method.str() + " var=" + Var.str() + " state=" + State.str());
  }
  unsigned Events = 0;

private:
  void say(const char *Hook, SourceLocation L, const std::string &Detail) {
    ++Events;
    llvm::outs() << "  " << Hook << " line " << SM.getSpellingLineNumber(L) << "  " << Detail << "\n";
  }
  const SourceManager &SM;
};

} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        AnalysisDeclContext AC(nullptr, FD);
        cfglab::applyPreset(AC.getCFGBuildOptions(), cfglab::semaPreset()); // includes setAllAlwaysAdd()
        if (!AC.getCFG()) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        Printer P(Ctx);
        ConsumedAnalyzer A(P);
        A.run(AC);
        if (P.Events == 0) llvm::outs() << "  (no events)\n";
      });
}
