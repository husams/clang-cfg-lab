// p05_pipeline -- AnalysisBasedWarnings::IssueWarnings in miniature (Part 5.8).
//
//   p05_pipeline <file> [--func=NAME] [--enable=unreachable,tsa,consumed,uninit,lifetime,logical]
//
// Picks CFG::BuildOptions for the UNION of the enabled analyses exactly as Sema
// does, builds one AnalysisDeclContext (so one CFG), then runs the analyses in
// Sema's order and prints how many findings each produced.

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Analyses/Consumed.h"
#include "clang/Analysis/Analyses/LifetimeSafety/LifetimeSafety.h"
#include "clang/Analysis/Analyses/ReachableCode.h"
#include "clang/Analysis/Analyses/ThreadSafety.h"
#include "clang/Analysis/Analyses/UninitializedValues.h"

#include <set>

using namespace clang;

static llvm::cl::OptionCategory Cat("p05_pipeline options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::desc("only this function"),
                                           llvm::cl::cat(Cat));
static llvm::cl::opt<std::string> EnableFlag("enable", llvm::cl::desc("comma-separated analyses; default: all"),
                                             llvm::cl::init("unreachable,tsa,consumed,uninit,lifetime,logical"),
                                             llvm::cl::cat(Cat));

namespace {

struct CountUnreachable : reachable_code::Callback {
  unsigned N = 0;
  void HandleUnreachable(reachable_code::UnreachableKind, SourceLocation, SourceRange, SourceRange, SourceRange,
                         bool) override { ++N; }
};

struct CountTsa : threadSafety::ThreadSafetyHandler {
  unsigned N = 0;
  using LK = threadSafety::LockKind;
  using POK = threadSafety::ProtectedOperationKind;
  using AK = threadSafety::AccessKind;
  void handleUnmatchedUnlock(StringRef, Name, SourceLocation, SourceLocation) override { ++N; }
  void handleIncorrectUnlockKind(StringRef, Name, LK, LK, SourceLocation, SourceLocation) override { ++N; }
  void handleDoubleLock(StringRef, Name, SourceLocation, SourceLocation) override { ++N; }
  void handleMutexHeldEndOfScope(StringRef, Name, SourceLocation, SourceLocation, threadSafety::LockErrorKind,
                                 bool) override { ++N; }
  void handleNoMutexHeld(const NamedDecl *, POK, AK, SourceLocation) override { ++N; }
  void handleMutexNotHeld(StringRef, const NamedDecl *, POK, Name, LK, SourceLocation, Name *) override { ++N; }
  void handleFunExcludesLock(StringRef, Name, Name, SourceLocation) override { ++N; }
  void handleLockAcquiredBefore(StringRef, Name, Name, SourceLocation) override { ++N; }
};

struct CountConsumed : consumed::ConsumedWarningsHandlerBase {
  unsigned N = 0;
  void warnLoopStateMismatch(SourceLocation, StringRef) override { ++N; }
  void warnParamReturnTypestateMismatch(SourceLocation, StringRef, StringRef, StringRef) override { ++N; }
  void warnParamTypestateMismatch(SourceLocation, StringRef, StringRef) override { ++N; }
  void warnReturnTypestateMismatch(SourceLocation, StringRef, StringRef) override { ++N; }
  void warnUseOfTempInInvalidState(StringRef, StringRef, SourceLocation) override { ++N; }
  void warnUseInInvalidState(StringRef, StringRef, StringRef, SourceLocation) override { ++N; }
};

struct CountUninit : UninitVariablesHandler {
  unsigned N = 0;
  void handleUseOfUninitVariable(const VarDecl *, const UninitUse &) override { ++N; }
  void handleSelfInit(const VarDecl *) override { ++N; }
};

struct CountLifetime : lifetimes::LifetimeSafetyReporter {
  unsigned N = 0;
  void reportUseAfterFree(const Expr *, const Expr *, SourceLocation, lifetimes::Confidence) override { ++N; }
  void reportUseAfterReturn(const Expr *, const Expr *, SourceLocation, lifetimes::Confidence) override { ++N; }
};

struct CountLogic : CFGCallback {
  unsigned N = 0;
  void logicAlwaysTrue(const BinaryOperator *, bool) override { ++N; }
  void compareAlwaysTrue(const BinaryOperator *, bool) override { ++N; }
  void compareBitwiseEquality(const BinaryOperator *, bool) override { ++N; }
  void compareBitwiseOr(const BinaryOperator *) override { ++N; }
};

} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &PP) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        std::set<std::string> On;
        {
          llvm::SmallVector<llvm::StringRef> Parts;
          llvm::StringRef(EnableFlag).split(Parts, ',', -1, false);
          for (auto P : Parts) On.insert(P.str());
        }
        auto has = [&](const char *N) { return On.count(N) != 0; };

        // --- 1. choose BuildOptions for the union of the enabled analyses ---
        AnalysisDeclContext AC(nullptr, FD);
        CFG::BuildOptions &BO = AC.getCFGBuildOptions();
        BO.PruneTriviallyFalseEdges = true;
        BO.AddEHEdges = false;
        BO.AddInitializers = true;
        BO.AddImplicitDtors = true;
        BO.AddTemporaryDtors = true;
        BO.AddCXXNewAllocator = false;
        BO.AddCXXDefaultInitExprInCtors = true;
        if (has("lifetime")) BO.AddLifetime = true;
        bool Linear = has("unreachable") || has("tsa") || has("consumed") || has("lifetime");
        if (Linear) {
          BO.setAllAlwaysAdd();
        } else {
          BO.setAlwaysAdd(Stmt::BinaryOperatorClass).setAlwaysAdd(Stmt::CompoundAssignOperatorClass)
              .setAlwaysAdd(Stmt::BlockExprClass).setAlwaysAdd(Stmt::CStyleCastExprClass)
              .setAlwaysAdd(Stmt::DeclRefExprClass).setAlwaysAdd(Stmt::ImplicitCastExprClass)
              .setAlwaysAdd(Stmt::UnaryOperatorClass);
        }
        CountLogic Logic;
        if (has("logical")) BO.Observer = &Logic;

        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        llvm::outs() << "  cfg options: alwaysAdd=" << (Linear ? "all" : "7 classes")
                     << " addLifetime=" << (BO.AddLifetime ? "yes" : "no")
                     << " observer=" << (BO.Observer ? "yes" : "no") << "\n";

        // --- 2. run in Sema's order; the first getCFG() builds the CFG, later ones reuse it ---
        CFG *G = AC.getCFG();
        if (!G) return;
        unsigned Elements = 0;
        for (const CFGBlock *B : *G) Elements += B->size();
        llvm::outs() << "  cfg: " << G->getNumBlockIDs() << " blocks, " << Elements << " elements\n";

        // The Observer already fired inside the getCFG() call above.
        if (has("logical"))
          llvm::outs() << "  0 logical        " << Logic.N << "   (fired while the CFG was built)\n";

        if (has("unreachable")) {
          CountUnreachable C;
          reachable_code::FindUnreachableCode(AC, PP, C);
          llvm::outs() << "  1 unreachable    " << C.N << "\n";
        }
        if (has("tsa")) {
          CountTsa C;
          threadSafety::BeforeSet *Cache = nullptr;
          threadSafety::runThreadSafetyAnalysis(AC, C, &Cache);
          threadSafety::threadSafetyCleanup(Cache);
          llvm::outs() << "  2 thread-safety  " << C.N << "\n";
        }
        if (has("consumed")) {
          CountConsumed C;
          consumed::ConsumedAnalyzer A(C);
          A.run(AC);
          llvm::outs() << "  3 consumed       " << C.N << "\n";
        }
        if (has("uninit")) {
          CountUninit C;
          UninitVariablesAnalysisStats S{};
          runUninitializedVariablesAnalysis(*FD, *G, AC, C, S);
          llvm::outs() << "  4 uninitialized  " << C.N << "\n";
        }
        if (has("lifetime")) {
          CountLifetime C;
          lifetimes::LifetimeSafetyStats S;
          lifetimes::runLifetimeSafetyAnalysis(AC, &C, S, false);
          llvm::outs() << "  5 lifetime       " << C.N << "\n";
        }
      });
}
