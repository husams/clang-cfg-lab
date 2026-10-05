// p05_tsa -- a ThreadSafetyHandler that prints every event (Part 5.5).
//
//   p05_tsa <file> [--func=NAME] [--negative]
//
// Runs clang::threadSafety::runThreadSafetyAnalysis the way
// AnalysisBasedWarnings does: Sema's CFG options with setAllAlwaysAdd().

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Analyses/ThreadSafety.h"

using namespace clang;
using namespace clang::threadSafety;

static llvm::cl::OptionCategory Cat("p05_tsa options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::desc("only this function"),
                                           llvm::cl::cat(Cat));
static llvm::cl::opt<bool> NegFlag("negative", llvm::cl::desc("also print handleNegativeNotHeld events (Sema hides them behind -Wthread-safety-negative)"),
                                   llvm::cl::cat(Cat));

namespace {

const char *pok(ProtectedOperationKind K) {
  switch (K) {
  case POK_VarDereference: return "VarDereference";
  case POK_VarAccess: return "VarAccess";
  case POK_FunctionCall: return "FunctionCall";
  case POK_PassByRef: return "PassByRef";
  case POK_PtPassByRef: return "PtPassByRef";
  case POK_ReturnByRef: return "ReturnByRef";
  case POK_PtReturnByRef: return "PtReturnByRef";
  case POK_PassPointer: return "PassPointer";
  case POK_PtPassPointer: return "PtPassPointer";
  case POK_ReturnPointer: return "ReturnPointer";
  case POK_PtReturnPointer: return "PtReturnPointer";
  }
  return "?";
}
const char *lk(LockKind K) {
  return K == LK_Shared ? "Shared" : K == LK_Exclusive ? "Exclusive" : "Generic";
}
const char *ak(AccessKind K) { return K == AK_Read ? "Read" : "Written"; }
const char *lek(LockErrorKind K) {
  switch (K) {
  case LEK_LockedSomeLoopIterations: return "LockedSomeLoopIterations";
  case LEK_LockedSomePredecessors: return "LockedSomePredecessors";
  case LEK_LockedAtEndOfFunction: return "LockedAtEndOfFunction";
  case LEK_NotLockedAtEndOfFunction: return "NotLockedAtEndOfFunction";
  }
  return "?";
}

class Printer : public ThreadSafetyHandler {
public:
  explicit Printer(ASTContext &C) : SM(C.getSourceManager()) {}

  void handleInvalidLockExp(SourceLocation L) override { say("handleInvalidLockExp", L, ""); }
  void handleUnmatchedUnlock(StringRef, Name Lock, SourceLocation L, SourceLocation) override {
    say("handleUnmatchedUnlock", L, "lock=" + Lock.str());
  }
  void handleIncorrectUnlockKind(StringRef, Name Lock, LockKind Exp, LockKind Got, SourceLocation,
                                 SourceLocation L) override {
    say("handleIncorrectUnlockKind", L, "lock=" + Lock.str() + " expected=" + lk(Exp) + " got=" + lk(Got));
  }
  void handleDoubleLock(StringRef, Name Lock, SourceLocation, SourceLocation L) override {
    say("handleDoubleLock", L, "lock=" + Lock.str());
  }
  void handleMutexHeldEndOfScope(StringRef, Name Lock, SourceLocation Locked, SourceLocation End,
                                 LockErrorKind K, bool) override {
    say("handleMutexHeldEndOfScope", End,
        "lock=" + Lock.str() + " locked-at-line=" + std::to_string(SM.getSpellingLineNumber(Locked)) + " " + lek(K));
  }
  void handleExclusiveAndShared(StringRef, Name Lock, SourceLocation L, SourceLocation) override {
    say("handleExclusiveAndShared", L, "lock=" + Lock.str());
  }
  void handleNoMutexHeld(const NamedDecl *D, ProtectedOperationKind P, AccessKind A, SourceLocation L) override {
    say("handleNoMutexHeld", L, "decl=" + D->getNameAsString() + " " + pok(P) + " " + ak(A));
  }
  void handleMutexNotHeld(StringRef, const NamedDecl *D, ProtectedOperationKind P, Name Lock, LockKind K,
                          SourceLocation L, Name *) override {
    say("handleMutexNotHeld", L, "decl=" + D->getNameAsString() + " " + pok(P) + " needs " + lk(K) + " lock=" + Lock.str());
  }
  void handleNegativeNotHeld(StringRef, Name Lock, Name Neg, SourceLocation L) override {
    if (!NegFlag) { ++Hidden; return; }
    say("handleNegativeNotHeld", L, "lock=" + Lock.str() + " neg=" + Neg.str());
  }
  void handleNegativeNotHeld(const NamedDecl *D, Name Lock, SourceLocation L) override {
    if (!NegFlag) { ++Hidden; return; }
    say("handleNegativeNotHeld", L, "decl=" + D->getNameAsString() + " lock=" + Lock.str());
  }
  void handleFunExcludesLock(StringRef, Name Fun, Name Lock, SourceLocation L) override {
    say("handleFunExcludesLock", L, "function=" + Fun.str() + " lock=" + Lock.str());
  }
  void handleLockAcquiredBefore(StringRef, Name L1, Name L2, SourceLocation L) override {
    say("handleLockAcquiredBefore", L, "acquired=" + L1.str() + " but must come before=" + L2.str());
  }
  void handleBeforeAfterCycle(Name L1, SourceLocation L) override {
    say("handleBeforeAfterCycle", L, "lock=" + L1.str());
  }
  unsigned Events = 0;
  unsigned Hidden = 0;

private:
  void say(const char *Hook, SourceLocation L, const std::string &Detail) {
    ++Events;
    llvm::outs() << "  " << llvm::format("%-26s", Hook);
    if (L.isValid()) llvm::outs() << " line " << SM.getSpellingLineNumber(L);
    else llvm::outs() << " (invalid loc: Sema substitutes the end of the function)";
    if (!Detail.empty()) llvm::outs() << "  " << Detail;
    llvm::outs() << "\n";
  }
  const SourceManager &SM;
};

} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        // Only definitions that have an annotated environment to talk about; skip the
        // constructors/destructors declared in the sample's helper structs.
        AnalysisDeclContext AC(nullptr, FD);
        CFG::BuildOptions &BO = AC.getCFGBuildOptions();
        BO.PruneTriviallyFalseEdges = true;
        BO.AddEHEdges = false;
        BO.AddInitializers = true;
        BO.AddImplicitDtors = true;
        BO.AddTemporaryDtors = true;
        BO.AddCXXNewAllocator = false;
        BO.AddCXXDefaultInitExprInCtors = true;
        BO.setAllAlwaysAdd();
        if (!AC.getCFG()) return;

        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        Printer P(Ctx);
        BeforeSet *Cache = nullptr;
        runThreadSafetyAnalysis(AC, P, &Cache);
        threadSafetyCleanup(Cache);
        if (P.Events == 0) llvm::outs() << "  (no events)\n";
        if (P.Hidden) llvm::outs() << "  (" << P.Hidden << " handleNegativeNotHeld hidden; use --negative)\n";
      });
}
