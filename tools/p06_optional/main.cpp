// p06_optional -- run the built-in UncheckedOptionalAccessModel (Part 6.7)
//
//   build/bin/p06_optional manifests/p06_optional.cpp
//   build/bin/p06_optional manifests/p06_optional.cpp --func=chromium_check --chromium
//
// diagnoseFunction<Analysis, Diagnostic>() does everything Section 6.2 did by
// hand (AdornedCFG, solver, context, environment, runDataflowAnalysis) and
// hands you a diagnoser callback instead of CFGEltCallbacks.
#include "cfglab.h"

#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/Models/ChromiumCheckModel.h"
#include "clang/Analysis/FlowSensitive/Models/UncheckedOptionalAccessModel.h"
#include "clang/Lex/Lexer.h"

#include <algorithm>

using namespace clang;
using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p06_optional options");
static llvm::cl::opt<std::string> OptFunc("func", llvm::cl::desc("only this function"),
                                          llvm::cl::init(""), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptChromium("chromium", llvm::cl::desc("also apply ChromiumCheckModel"),
                                       llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptIgnoreValue("ignore-value-calls",
                                          llvm::cl::desc("UncheckedOptionalAccessModelOptions::IgnoreValueCalls"),
                                          llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptIgnoreSmart("ignore-smart-pointer",
                                          llvm::cl::desc("UncheckedOptionalAccessModelOptions::IgnoreSmartPointerDereference"),
                                          llvm::cl::cat(Cat));
static llvm::cl::opt<int> OptMaxVisits("max-visits", llvm::cl::init(kDefaultMaxBlockVisits), llvm::cl::cat(Cat));
static llvm::cl::opt<long long> OptMaxSat("max-sat", llvm::cl::init(kDefaultMaxSATIterations), llvm::cl::cat(Cat));

namespace {
// The built-in model plus the Chromium CHECK() model, run one after the other.
// A DataflowModel's transfer() returns true when it recognised the element.
class OptionalWithChromium : public DataflowAnalysis<OptionalWithChromium, UncheckedOptionalAccessLattice> {
  UncheckedOptionalAccessModel Optional;
  ChromiumCheckModel Chromium;

public:
  // diagnoseFunction constructs the analysis through createAnalysis<T>(ASTContext&, Environment&).
  OptionalWithChromium(ASTContext &C, Environment &Env)
      : DataflowAnalysis<OptionalWithChromium, UncheckedOptionalAccessLattice>(C), Optional(C, Env) {}
  static UncheckedOptionalAccessLattice initialElement() { return {}; }
  void transfer(const CFGElement &E, UncheckedOptionalAccessLattice &L, Environment &Env) {
    Chromium.transfer(E, Env);
    Optional.transfer(E, L, Env);
  }
};

template <typename AnalysisT>
void runOne(const FunctionDecl *FD, ASTContext &Ctx) {
  UncheckedOptionalAccessModelOptions Opts;
  Opts.IgnoreValueCalls = OptIgnoreValue;
  Opts.IgnoreSmartPointerDereference = OptIgnoreSmart;
  UncheckedOptionalAccessDiagnoser Diagnoser(Opts);
  auto Fn = [&](const CFGElement &E, ASTContext &C,
                const TransferStateForDiagnostics<UncheckedOptionalAccessLattice> &S) {
    return Diagnoser(E, C, S);
  };
  DiagnosisCallbacks<AnalysisT, UncheckedOptionalAccessDiagnostic> Callbacks{nullptr, Fn};
  auto R = diagnoseFunction<AnalysisT, UncheckedOptionalAccessDiagnostic>(*FD, Ctx, Callbacks, OptMaxSat, OptMaxVisits);
  llvm::outs() << "== " << FD->getNameAsString() << "\n";
  if (!R) {
    llvm::outs() << "  failed: " << llvm::toString(R.takeError()) << "\n";
    return;
  }
  const SourceManager &SM = Ctx.getSourceManager();
  std::vector<std::pair<unsigned, std::string>> Lines;
  for (auto &D : *R) {
    unsigned Line = SM.getSpellingLineNumber(D.Range.getBegin());
    std::string Text = Lexer::getSourceText(D.Range, SM, Ctx.getLangOpts()).str();
    Lines.push_back({Line, "  line " + std::to_string(Line) + ": unchecked optional access: `" + Text + "`"});
  }
  std::sort(Lines.begin(), Lines.end());
  for (auto &L : Lines) llvm::outs() << L.second << "\n";
  if (Lines.empty()) llvm::outs() << "  (no diagnostics)\n";
}
} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!OptFunc.empty() && FD->getNameAsString() != OptFunc) return;
        if (OptChromium) runOne<OptionalWithChromium>(FD, Ctx);
        else runOne<UncheckedOptionalAccessModel>(FD, Ctx);
      });
}
