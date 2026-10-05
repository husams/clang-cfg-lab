// p06_log -- watching an analysis: the Logger interface, -dataflow-log, Environment::dump (Part 6.8)
//
//   build/bin/p06_log manifests/p06_constprop.cpp --func=diverge --logger=custom
//   build/bin/p06_log manifests/p06_constprop.cpp --func=diverge --dump-env
//   build/bin/p06_log manifests/p06_constprop.cpp --func=diverge -dataflow-log          (framework's text logger)
//   build/bin/p06_log manifests/p06_constprop.cpp --func=diverge -dataflow-log=out/html (HTML logger)
#include "cfglab.h"
#include "../p06_constprop/constprop.h"

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/Logger.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

using namespace clang;
using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p06_log options");
static llvm::cl::opt<std::string> OptFunc("func", llvm::cl::desc("only this function"),
                                          llvm::cl::init(""), llvm::cl::cat(Cat));
static llvm::cl::opt<std::string> OptLogger("logger", llvm::cl::desc("none | custom | textual"),
                                            llvm::cl::init("none"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptDumpEnv("dump-env", llvm::cl::desc("Environment::dump() before each return"),
                                      llvm::cl::cat(Cat));

namespace {
// A Logger is told about the structure of the run (blocks, elements, convergence);
// the analysis adds its own messages with Logger::log().
class TraceLogger : public Logger {
  ASTContext *Ctx;

public:
  explicit TraceLogger(ASTContext &C) : Logger(/*ShouldLogText=*/true), Ctx(&C) {}
  void beginAnalysis(const AdornedCFG &, TypeErasedDataflowAnalysis &) override {
    llvm::outs() << "[logger] beginAnalysis\n";
  }
  void endAnalysis() override { llvm::outs() << "[logger] endAnalysis\n"; }
  void enterBlock(const CFGBlock &B, bool PostVisit) override {
    llvm::outs() << "[logger] enterBlock B" << B.getBlockID() << (PostVisit ? "  (post-visit pass)" : "") << "\n";
  }
  void enterElement(const CFGElement &E) override {
    llvm::outs() << "[logger]   enterElement " << cfglab::kindName(E.getKind());
    if (auto S = E.getAs<CFGStmt>()) llvm::outs() << "  `" << cfglab::stmtText(S->getStmt(), *Ctx, 24) << "`";
    llvm::outs() << "\n";
  }
  void recordState(TypeErasedDataflowAnalysisState &S) override {
    llvm::outs() << "[logger]   recordState, flow condition token " << S.Env.getFlowConditionToken() << "\n";
  }
  void blockConverged() override { llvm::outs() << "[logger]   blockConverged\n"; }
  void logText(llvm::StringRef Text) override {
    llvm::outs() << "[logger]   log: " << Text.trim() << "\n";
  }
};

// Wrap the Part 6.1 analysis and add one log() line per statement it changes.
class LoggedConstProp : public DataflowAnalysis<LoggedConstProp, p06::Lat> {
  p06::ConstProp Inner;

public:
  explicit LoggedConstProp(ASTContext &C) : DataflowAnalysis<LoggedConstProp, p06::Lat>(C), Inner(C) {}
  static p06::Lat initialElement() { return {}; }
  void transfer(const CFGElement &E, p06::Lat &L, Environment &Env) {
    p06::Lat Before = L;
    Inner.transfer(E, L, Env);
    if (!(Before == L))
      Env.getDataflowAnalysisContext().getOptions().Log->log([&](llvm::raw_ostream &OS) {
        OS << "constprop changed the lattice at "
           << (E.getAs<CFGStmt>() ? cfglab::stmtText(E.getAs<CFGStmt>()->getStmt(), getASTContext(), 24) : "?");
      });
  }
};
} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!OptFunc.empty() && FD->getNameAsString() != OptFunc) return;
        auto ACFG = AdornedCFG::build(*FD);
        if (!ACFG) { llvm::errs() << llvm::toString(ACFG.takeError()) << "\n"; return; }

        DataflowAnalysisContext::Options Opts;
        std::unique_ptr<Logger> Owned;
        if (OptLogger == "custom") Owned = std::make_unique<TraceLogger>(Ctx);
        else if (OptLogger == "textual") Owned = Logger::textual(llvm::outs());
        Opts.Log = Owned.get(); // null => the -dataflow-log flags (or the no-op logger) decide

        DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>(), Opts);
        Environment Env(DACtx, *FD);
        LoggedConstProp A(Ctx);
        CFGEltCallbacks<LoggedConstProp> CB;
        if (OptDumpEnv)
          CB.Before = [&](const CFGElement &E, const DataflowAnalysisState<p06::Lat> &St) {
            auto S = E.getAs<CFGStmt>();
            if (S && isa<ReturnStmt>(S->getStmt())) St.Env.dump(llvm::outs());
          };
        llvm::outs() << "== " << FD->getNameAsString() << "\n";
        auto Res = runDataflowAnalysis(*ACFG, A, Env, CB);
        if (!Res) llvm::outs() << "failed: " << llvm::toString(Res.takeError()) << "\n";
      });
}
