// p07_movecheck -- Part 7.1/7.2: a use-after-move checker on the dataflow framework.
//
//   build/bin/p07_movecheck manifests/p07_move.cpp [--func=NAME] [--max-sat=N] [--max-visits=N]

#include "cfglab.h"
#include "MoveModel.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p07_movecheck options");
static llvm::cl::opt<std::string> MvFunc("func", llvm::cl::desc("only this function"), llvm::cl::cat(Cat));
static llvm::cl::opt<long> MvMaxSat("max-sat", llvm::cl::desc("MaxSATIterations"),
                                    llvm::cl::init(clang::dataflow::kDefaultMaxSATIterations), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> MvBefore("before", llvm::cl::desc("run the diagnoser on the state BEFORE each element (shows why After is used)"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> MvDump("dump-env", llvm::cl::desc("print the Environment after each use of a tracked object (addresses masked by the caller)"), llvm::cl::cat(Cat));
static llvm::cl::opt<int> MvMaxVisits("max-visits", llvm::cl::desc("MaxBlockVisits"),
                                      llvm::cl::init(clang::dataflow::kDefaultMaxBlockVisits), llvm::cl::cat(Cat));

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!MvFunc.empty() && FD->getNameAsString() != MvFunc) return;
        using namespace clang::dataflow;
        p07::MoveDiagnoser Diag;
        auto Fn = [&](const CFGElement &E, ASTContext &C,
                      const TransferStateForDiagnostics<NoopLattice> &S) {
          if (MvDump)
            if (auto CS = E.getAs<CFGStmt>())
              if (const auto *DR = dyn_cast<DeclRefExpr>(CS->getStmt()); DR && p07::isTrackable(DR->getType())) {
                llvm::outs() << "-- Environment after `" << DR->getDecl()->getName() << "` at line "
                             << C.getSourceManager().getSpellingLineNumber(DR->getBeginLoc()) << "\n";
                S.Env.dump(llvm::outs());
              }
          return Diag(E, C, S);
        };
        DiagnosisCallbacks<p07::MoveAnalysis, p07::MoveDiag> CBs = MvBefore
            ? DiagnosisCallbacks<p07::MoveAnalysis, p07::MoveDiag>{Fn, nullptr}
            : DiagnosisCallbacks<p07::MoveAnalysis, p07::MoveDiag>{nullptr, Fn};
        auto R = diagnoseFunction<p07::MoveAnalysis, p07::MoveDiag>(*FD, Ctx, CBs, MvMaxSat, MvMaxVisits);
        if (!R) {
          llvm::outs() << FD->getQualifiedNameAsString() << ": analysis error: "
                       << llvm::toString(R.takeError()) << "\n";
          return;
        }
        for (const p07::MoveDiag &D : *R)
          llvm::outs() << p07::formatDiag(Ctx.getSourceManager(), D) << "\n";
      });
}
