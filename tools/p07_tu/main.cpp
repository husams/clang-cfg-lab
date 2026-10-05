// p07_tu -- Part 7.3 / 7.7: run the use-after-move checker over a whole translation unit.
//
//   build/bin/p07_tu FILE [--max-sat=N] [--max-visits=N] [--no-prefilter] [--recover]
//                         [--summary-only] [--quiet] [--time] [--emit=tsv]
//
// One line per function, then a summary. Exit code: 0 clean, 1 diagnostics, 2 errors.

#include "cfglab.h"
#include "Driver.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p07_tu options");
static llvm::cl::opt<long> TuMaxSat("max-sat", llvm::cl::desc("MaxSATIterations"),
                                    llvm::cl::init(clang::dataflow::kDefaultMaxSATIterations), llvm::cl::cat(Cat));
static llvm::cl::opt<int> TuMaxVisits("max-visits", llvm::cl::desc("MaxBlockVisits"),
                                      llvm::cl::init(clang::dataflow::kDefaultMaxBlockVisits), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> TuNoPrefilter("no-prefilter", llvm::cl::desc("analyze functions without std::move too"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> TuRecover("recover", llvm::cl::desc("survive crashes with CrashRecoveryContext"), llvm::cl::cat(Cat));
static llvm::cl::opt<std::string> TuCrashIn("crash-in", llvm::cl::desc("(test hook) crash inside this function"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> TuSummaryOnly("summary-only", llvm::cl::desc("print only the summary"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> TuQuiet("quiet", llvm::cl::desc("do not print skipped functions"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> TuTime("time", llvm::cl::desc("append wall-clock microseconds to each line"), llvm::cl::cat(Cat));
static llvm::cl::opt<std::string> TuEmit("emit", llvm::cl::desc("tsv: machine-readable per-function rows"), llvm::cl::cat(Cat));

namespace {
struct Totals {
  unsigned Functions = 0, Analyzed = 0, Skipped = 0, Errors = 0, Diags = 0;
  unsigned long Transfers = 0;
  double Micros = 0;
  std::map<std::string, unsigned> ByStatus;
};
Totals T;

void report(const p07::FnResult &R, ASTContext &Ctx) {
  const SourceManager &SM = Ctx.getSourceManager();
  ++T.Functions;
  ++T.ByStatus[p07::statusName(R.St)];
  if (p07::isSkip(R.St)) ++T.Skipped;
  else if (p07::isError(R.St)) ++T.Errors;
  else ++T.Analyzed;
  T.Diags += R.Diags.size();
  T.Transfers += R.Transfers;
  T.Micros += R.Micros;

  if (TuEmit == "tsv") {
    llvm::outs() << R.FD->getQualifiedNameAsString() << "\t" << p07::statusName(R.St) << "\t"
                 << R.Blocks << "\t" << R.Transfers << "\t" << R.Diags.size() << "\n";
    return;
  }
  if (TuSummaryOnly) return;
  if (TuQuiet && p07::isSkip(R.St)) return;
  llvm::outs() << llvm::format("%-18s", R.FD->getQualifiedNameAsString().c_str())
               << llvm::format(" %-16s", p07::statusName(R.St));
  if (R.St == p07::Status::Analyzed) llvm::outs() << " diags=" << R.Diags.size();
  if (!R.Message.empty()) llvm::outs() << " -- " << R.Message;
  if (TuTime) llvm::outs() << llvm::format("  [%.0f us]", R.Micros);
  llvm::outs() << "\n";
  for (const p07::MoveDiag &D : R.Diags) llvm::outs() << "  " << p07::formatDiag(SM, D) << "\n";
}

struct Visitor : RecursiveASTVisitor<Visitor> {
  ASTContext &Ctx;
  p07::Options O;
  explicit Visitor(ASTContext &C) : Ctx(C) {
    O.B.MaxSat = TuMaxSat;
    O.B.MaxVisits = TuMaxVisits;
    O.Prefilter = !TuNoPrefilter;
    O.Recover = TuRecover;
    O.CrashIn = TuCrashIn;
  }
  bool VisitFunctionDecl(FunctionDecl *FD) {
    // A redeclaration with no body is also visited; we want one row per definition
    // or per bodiless declaration, never twice: skip a declaration whose
    // definition exists elsewhere.
    if (!FD->doesThisDeclarationHaveABody() && FD->getDefinition()) return true;
    // implicit declarations (builtins, implicit members) are not user code
    if (FD->isImplicit()) return true;
    report(p07::analyze(FD, Ctx, O), Ctx);
    return true;
  }
};

struct Consumer : ASTConsumer {
  void HandleTranslationUnit(ASTContext &Ctx) override { Visitor(Ctx).TraverseDecl(Ctx.getTranslationUnitDecl()); }
};
struct Act : ASTFrontendAction {
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef) override {
    return std::make_unique<Consumer>();
  }
};
} // namespace

int main(int argc, const char **argv) {
  llvm::CrashRecoveryContext::Enable();
  int RC = cfglab::runTool<Act>(argc, argv, Cat);
  if (RC) return RC;
  if (TuEmit == "tsv") return 0;
  llvm::outs() << "summary: " << T.Functions << " functions, " << T.Analyzed << " analyzed, " << T.Skipped
               << " skipped, " << T.Errors << " errors, " << T.Diags << " diagnostics\n";
  for (auto &[K, V] : T.ByStatus) llvm::outs() << "  " << llvm::format("%-16s", K.c_str()) << " " << V << "\n";
  if (TuTime) llvm::outs() << llvm::format("total: %.0f us, %lu transfers\n", T.Micros, T.Transfers);
  return T.Errors ? 2 : (T.Diags ? 1 : 0);
}
