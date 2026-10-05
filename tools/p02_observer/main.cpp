// p02_observer -- CFG::BuildOptions::Observer, the CFGCallback hook (Part 2.3).
//
//   p02_observer <file> [--func=NAME]
//
// The CFG builder evaluates some conditions as it lowers them. When it finds
// a logical or comparison expression whose value is fixed, it calls back
// into BuildOptions::Observer. Sema's LogicalErrorHandler is the real client
// (-Wtautological-overlap-compare and friends).

#include "cfglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p02_observer options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::cat(Cat));

namespace {
class Printer : public CFGCallback {
public:
  Printer(ASTContext &C) : Ctx(C) {}

  void logicAlwaysTrue(const BinaryOperator *B, bool IsAlwaysTrue) override {
    report("logicAlwaysTrue", B, IsAlwaysTrue);
  }
  void compareAlwaysTrue(const BinaryOperator *B, bool IsAlwaysTrue) override {
    report("compareAlwaysTrue", B, IsAlwaysTrue);
  }
  void compareBitwiseEquality(const BinaryOperator *B, bool IsAlwaysTrue) override {
    report("compareBitwiseEquality", B, IsAlwaysTrue);
  }
  void compareBitwiseOr(const BinaryOperator *B) override {
    llvm::outs() << "  compareBitwiseOr        line "
                 << cfglab::lineOf(Ctx.getSourceManager(), B->getBeginLoc()) << "  `"
                 << cfglab::stmtText(B, Ctx) << "`\n";
  }

private:
  void report(const char *Hook, const BinaryOperator *B, bool IsAlwaysTrue) {
    llvm::outs() << "  " << llvm::format("%-23s", Hook) << " line "
                 << cfglab::lineOf(Ctx.getSourceManager(), B->getBeginLoc()) << "  `"
                 << cfglab::stmtText(B, Ctx) << "`  always "
                 << (IsAlwaysTrue ? "true" : "false") << "\n";
  }
  ASTContext &Ctx;
};
} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        Printer P(Ctx);
        CFG::BuildOptions BO;
        BO.Observer = &P; // callbacks fire *during* buildCFG, not afterwards
        CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
      });
}
