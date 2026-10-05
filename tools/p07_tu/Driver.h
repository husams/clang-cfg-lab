// Driver.h -- the whole-translation-unit driver of Part 7.3, reusable.
//
//   classify(FD, ...)  -> why a function is skipped, or Analyze
//   analyze(FD, ...)   -> FnResult: status + diagnostics + cost counters
//
// Never throws, never exits, never lets one function stop the run.

#ifndef CFGLAB_P07_DRIVER_H
#define CFGLAB_P07_DRIVER_H

#include "../p07_movecheck/MoveModel.h"

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/Error.h"

#include <chrono>

namespace p07 {

enum class Status {
  Analyzed,       // ran to a fixpoint; diagnostics (possibly none) are trustworthy
  SkipNoBody,     // declaration only
  SkipTemplated,  // template pattern: dependent types, AdornedCFG::build refuses
  SkipFile,       // not in the main file
  SkipNoMove,     // cheap AST prefilter: no std::move call, so nothing to find
  ErrVisits,      // MaxBlockVisits exceeded       (std::errc::timed_out)
  ErrSat,         // MaxSATIterations exceeded     (llvm::errc::interrupted)
  ErrOther,       // any other llvm::Error (e.g. unsupported language)
  Crashed,        // the analysis crashed; recovered with CrashRecoveryContext
};

inline const char *statusName(Status S) {
  switch (S) {
  case Status::Analyzed: return "ok";
  case Status::SkipNoBody: return "skip:no-body";
  case Status::SkipTemplated: return "skip:templated";
  case Status::SkipFile: return "skip:other-file";
  case Status::SkipNoMove: return "skip:no-move";
  case Status::ErrVisits: return "error:max-visits";
  case Status::ErrSat: return "error:max-sat";
  case Status::ErrOther: return "error:other";
  case Status::Crashed: return "error:crashed";
  }
  return "?";
}
inline bool isSkip(Status S) { return S >= Status::SkipNoBody && S <= Status::SkipNoMove; }
inline bool isError(Status S) { return S >= Status::ErrVisits; }

struct Budget {
  std::int64_t MaxSat = clang::dataflow::kDefaultMaxSATIterations;
  std::int32_t MaxVisits = clang::dataflow::kDefaultMaxBlockVisits;
};

struct Options {
  Budget B;
  bool Prefilter = true;      // AST scan for std::move before building anything
  bool AllFiles = false;      // include functions from non-main files (non-system)
  bool Recover = false;       // run each analysis under CrashRecoveryContext
  std::string CrashIn;        // test hook: deliberately crash in this function
};

struct FnResult {
  const FunctionDecl *FD = nullptr;
  Status St = Status::Analyzed;
  std::string Message;                 // llvm::Error text for error statuses
  llvm::SmallVector<MoveDiag> Diags;
  unsigned Blocks = 0;                 // AdornedCFG size
  unsigned long Transfers = 0;         // MoveAnalysis::transfer calls
  double Micros = 0;                   // wall clock for the whole function
};

// "Does the body contain std::move(...)?" -- the cheapest possible prefilter.
struct MoveFinder : RecursiveASTVisitor<MoveFinder> {
  bool Found = false;
  bool VisitCallExpr(CallExpr *CE) {
    if (isStdMoveCall(CE)) { Found = true; return false; }
    return true;
  }
};

inline Status classify(const FunctionDecl *FD, ASTContext &Ctx, const Options &O) {
  if (!FD->doesThisDeclarationHaveABody()) return Status::SkipNoBody;
  if (FD->isTemplated()) return Status::SkipTemplated;
  if (!O.AllFiles && !Ctx.getSourceManager().isInMainFile(FD->getLocation())) return Status::SkipFile;
  if (Ctx.getSourceManager().isInSystemHeader(FD->getLocation())) return Status::SkipFile;
  if (O.Prefilter) {
    MoveFinder F;
    F.TraverseStmt(FD->getBody());
    if (!F.Found) return Status::SkipNoMove;
  }
  return Status::Analyzed;
}

// Classify an llvm::Error into a Status without losing its message.
inline Status classifyError(llvm::Error E, std::string &Msg) {
  std::error_code EC;
  llvm::handleAllErrors(std::move(E), [&](const llvm::ErrorInfoBase &EIB) {
    EC = EIB.convertToErrorCode();
    Msg = EIB.message();
  });
  if (EC == std::errc::timed_out) return Status::ErrVisits;
  if (EC == llvm::errc::interrupted) return Status::ErrSat;
  return Status::ErrOther;
}

inline void analyzeBody(const FunctionDecl *FD, ASTContext &Ctx, const Options &O, FnResult &R) {
  using namespace clang::dataflow;
  if (!O.CrashIn.empty() && FD->getNameAsString() == O.CrashIn)
    *static_cast<volatile int *>(nullptr) = 1;               // test hook

  // AdornedCFG::build is the gate: it refuses C/ObjC and templated decls.
  llvm::Expected<AdornedCFG> ACFG = AdornedCFG::build(*FD);
  if (!ACFG) {
    R.St = classifyError(ACFG.takeError(), R.Message);
    return;
  }
  R.Blocks = ACFG->getCFG().size();

  MoveDiagnoser Diag;
  auto Fn = [&](const CFGElement &E, ASTContext &C,
                const TransferStateForDiagnostics<NoopLattice> &S) { return Diag(E, C, S); };
  DiagnosisCallbacks<MoveAnalysis, MoveDiag> CBs{nullptr, Fn};
  TransferCalls = 0;
  auto Res = diagnoseFunction<MoveAnalysis, MoveDiag>(*FD, Ctx, CBs, O.B.MaxSat, O.B.MaxVisits);
  R.Transfers = TransferCalls;
  if (!Res) {
    R.St = classifyError(Res.takeError(), R.Message);   // NOTE: partial diagnostics are dropped
    return;
  }
  R.Diags = std::move(*Res);
  llvm::sort(R.Diags, [&](const MoveDiag &A, const MoveDiag &B) {
    return Ctx.getSourceManager().isBeforeInTranslationUnit(A.Loc, B.Loc);
  });
  R.St = Status::Analyzed;
}

inline FnResult analyze(const FunctionDecl *FD, ASTContext &Ctx, const Options &O) {
  FnResult R;
  R.FD = FD;
  R.St = classify(FD, Ctx, O);
  if (R.St != Status::Analyzed) return R;
  auto T0 = std::chrono::steady_clock::now();
  if (O.Recover) {
    llvm::CrashRecoveryContext CRC;
    if (!CRC.RunSafely([&] { analyzeBody(FD, Ctx, O, R); })) {
      R.St = Status::Crashed;
      R.Message = "crash recovered";
    }
  } else {
    analyzeBody(FD, Ctx, O, R);
  }
  R.Micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - T0).count();
  return R;
}

} // namespace p07
#endif
