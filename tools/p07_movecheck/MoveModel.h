// MoveModel.h -- the Part 7 use-after-move checker, as a reusable header.
//
//   MoveAnalysis  : DataflowAnalysis<MoveAnalysis, NoopLattice>
//                   state lives in the Environment: one synthetic bool field
//                   "moved" on every trackable record object.
//   MoveDiagnoser : functor for DiagnosisCallbacks::Before -> MoveDiag list.
//
// Shared by p07_movecheck, p07_tu, p07_verify, p07_ctx, p07_combined and
// p07_plugin (they #include "../p07_movecheck/MoveModel.h").

#ifndef CFGLAB_P07_MOVEMODEL_H
#define CFGLAB_P07_MOVEMODEL_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/NoopLattice.h"
#include "clang/Analysis/FlowSensitive/StorageLocation.h"
#include "clang/Analysis/FlowSensitive/Value.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"

namespace p07 {
using namespace clang;
using namespace clang::dataflow;

inline constexpr const char *MovedField = "moved";

// Environment::get<T>() is cast_or_null: with assertions off (Homebrew) it silently
// reinterprets a ScalarStorageLocation as a record. Always dyn_cast.
template <typename D> inline RecordStorageLocation *recLoc(const Environment &Env, const D &X) {
  return dyn_cast_or_null<RecordStorageLocation>(Env.getStorageLocation(X));
}

// Cost proxy: how many times the engine called MoveAnalysis::transfer (deterministic,
// unlike wall-clock time). Reset by the caller.
inline unsigned long TransferCalls = 0;

// Section 7.5 shows why this guard exists; tools expose it as --no-dead-guard.
inline bool DeadPathGuard = true;

// ---- Which types are tracked ---------------------------------------------
// A class is "trackable" when it declares a move constructor or move
// assignment operator: those are the types where "moved-from" means something.
inline bool isTrackable(QualType Ty) {
  Ty = Ty.getNonReferenceType();
  const CXXRecordDecl *RD = Ty->getAsCXXRecordDecl();
  if (!RD || !RD->hasDefinition()) return false;
  for (const CXXConstructorDecl *C : RD->ctors())
    if (C->isMoveConstructor() && !C->isDeleted()) return true;
  for (const CXXMethodDecl *M : RD->methods())
    if (M->isMoveAssignmentOperator() && !M->isDeleted()) return true;
  return false;
}

// ---- Recognisers -------------------------------------------------------------
// `std::move(x)`: a call to a function named `move` in namespace std with one argument.
inline bool isStdMoveCall(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  const auto *CE = dyn_cast<CallExpr>(E);
  if (!CE || CE->getNumArgs() != 1) return false;
  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD || !FD->getIdentifier() || FD->getName() != "move") return false;
  return FD->isInStdNamespace();
}

// The object expression inside `std::move(<obj>)`, or null.
inline const Expr *movedOperand(const Expr *E) {
  if (!isStdMoveCall(E)) return nullptr;
  return cast<CallExpr>(E->IgnoreParenImpCasts())->getArg(0)->IgnoreParenImpCasts();
}

inline bool isReinitMethod(const CXXMethodDecl *M) {
  if (!M || !M->getIdentifier()) return false;
  StringRef N = M->getName();
  return N == "reset" || N == "clear" || N == "assign" || N == "emplace" || N == "init";
}

// ---- Result ------------------------------------------------------------------
struct MoveDiag {
  SourceLocation Loc;     // the offending use
  std::string Var;        // spelling of the object expression
  bool Certain = false;   // env.proves(moved) -- otherwise only env.allows(moved)
  const Stmt *At = nullptr; // the use expression (lets callers map it to a CFG block)
};

// ---- The analysis --------------------------------------------------------------
class MoveAnalysis : public DataflowAnalysis<MoveAnalysis, NoopLattice> {
public:
  // diagnoseFunction() builds the analysis as AnalysisT(ASTContext&, Environment&)
  // when that constructor exists: the only hook that runs before locations are made.
  MoveAnalysis(ASTContext &Ctx, Environment &Env)
      : DataflowAnalysis<MoveAnalysis, NoopLattice>(Ctx) {
    Env.getDataflowAnalysisContext().setSyntheticFieldCallback(
        [&Ctx](QualType Ty) -> llvm::StringMap<QualType> {
          llvm::StringMap<QualType> M;
          if (isTrackable(Ty)) M.try_emplace(MovedField, Ctx.BoolTy);
          return M;
        });
  }

  static NoopLattice initialElement() { return {}; }

  void transfer(const CFGElement &Elt, NoopLattice &, Environment &Env) {
    ++TransferCalls;
    if (!PinnedEntry) { PinnedEntry = true; pinEntryState(Env); }
    auto S = Elt.getAs<CFGStmt>();
    if (!S) return;
    const Stmt *St = S->getStmt();

    // (1) declarations: a fresh object is not moved-from
    if (const auto *DS = dyn_cast<DeclStmt>(St)) {
      for (const Decl *D : DS->decls())
        if (const auto *VD = dyn_cast<VarDecl>(D))
          if (isTrackable(VD->getType()))
            if (auto *RL = recLoc(Env, *VD)) setMoved(Env, RL, false);
      return;
    }

    // (2) re-initialisation: x = ..., x.reset()
    if (const auto *OC = dyn_cast<CXXOperatorCallExpr>(St)) {
      const auto *MD = dyn_cast_or_null<CXXMethodDecl>(OC->getDirectCallee());
      if (OC->getOperator() == OO_Equal && MD && OC->getNumArgs() == 2) {
        if (const Expr *Rhs = movedOperand(OC->getArg(1)))  // x = std::move(y): y moved
          markMoved(Env, Rhs);
        if (auto *RL = recLoc(Env, *OC->getArg(0))) setMoved(Env, RL, false);
      }
      return;
    }
    if (const auto *MC = dyn_cast<CXXMemberCallExpr>(St)) {
      if (isReinitMethod(MC->getMethodDecl()))
        if (const Expr *Obj = MC->getImplicitObjectArgument())
          if (auto *RL = recLoc(Env, *Obj)) setMoved(Env, RL, false);
    }

    // (3) move sites: constructor / call that consumes std::move(x)
    if (const auto *CE = dyn_cast<CXXConstructExpr>(St)) {
      if (CE->getConstructor()->isMoveConstructor() && CE->getNumArgs() == 1)
        if (const Expr *Op = movedOperand(CE->getArg(0))) markMoved(Env, Op);
      return;
    }
    if (const auto *Call = dyn_cast<CallExpr>(St)) {
      if (isa<CXXOperatorCallExpr>(Call)) return;
      const FunctionDecl *FD = Call->getDirectCallee();
      if (!FD || isStdMoveCall(Call)) return;
      unsigned First = isa<CXXMemberCallExpr>(Call) ? 0 : 0;
      for (unsigned I = First; I < Call->getNumArgs() && I < FD->getNumParams(); ++I)
        if (FD->getParamDecl(I)->getType()->isRValueReferenceType())
          if (const Expr *Op = movedOperand(Call->getArg(I))) markMoved(Env, Op);
    }
  }

  // Parameters, `*this` and their sub-objects get their locations -- and fresh,
  // unconstrained "moved" atoms -- when the engine initialises the starting
  // Environment, which is AFTER our constructor ran. Left alone, every first use
  // would be "possibly moved". The first transfer call sees exactly that starting
  // state, so we add the invariant "initial atom is false" (flow-insensitive, true
  // on every path: callers pass live objects).
  void pinEntryState(Environment &Env) {
    if (const FunctionDecl *FD = Env.getCurrentFunc())
      for (const ParmVarDecl *P : FD->parameters())
        if (auto *RL = recLoc(Env, *P)) pinTree(Env, RL);
    if (auto *This = Env.getThisPointeeStorageLocation()) pinTree(Env, This);
  }
  static void pinTree(Environment &Env, RecordStorageLocation *RL) {
    if (StorageLocation *F = flagLoc(RL))
      if (auto *BV = dyn_cast_or_null<BoolValue>(Env.getValue(*F)))
        Env.getDataflowAnalysisContext().addInvariant(Env.arena().makeNot(BV->formula()));
    for (auto &[Decl, Child] : RL->children())
      if (auto *CR = dyn_cast_or_null<RecordStorageLocation>(Child)) pinTree(Env, CR);
  }

  // ---- helpers shared with the diagnoser -------------------------------------------------
  static StorageLocation *flagLoc(RecordStorageLocation *RL) {
    if (!RL) return nullptr;
    for (auto &KV : RL->synthetic_fields())
      if (KV.first() == MovedField) return KV.second;
    return nullptr;
  }
  static void setMoved(Environment &Env, RecordStorageLocation *RL, bool V) {
    if (StorageLocation *F = flagLoc(RL)) Env.setValue(*F, Env.getBoolLiteralValue(V));
  }
  static void markMoved(Environment &Env, const Expr *Obj) {
    if (auto *RL = recLoc(Env, *Obj)) setMoved(Env, RL, true);
  }

private:
  bool PinnedEntry = false;
};

// ---- The diagnoser ---------------------------------------------------------------------
// Called by diagnoseFunction for each CFG element with the state AFTER the element
// (an expression's own storage location exists only after its built-in transfer ran).
struct MoveDiagnoser {
  mutable llvm::DenseSet<const Stmt *> Reported;  // one report per use site (loops revisit)

  llvm::SmallVector<MoveDiag>
  operator()(const CFGElement &Elt, ASTContext &Ctx,
             const TransferStateForDiagnostics<NoopLattice> &State) const {
    llvm::SmallVector<MoveDiag> Out;
    auto S = Elt.getAs<CFGStmt>();
    if (!S) return Out;
    const auto *E = dyn_cast<Expr>(S->getStmt());
    if (!E || !(isa<DeclRefExpr>(E) || isa<MemberExpr>(E)) || !E->isGLValue()) return Out;
    if (!isTrackable(E->getType())) return Out;
    if (isReinitContext(E, Ctx)) return Out;
    auto *RL = recLoc(State.Env, *E);
    StorageLocation *F = MoveAnalysis::flagLoc(RL);
    auto *BV = F ? dyn_cast_or_null<BoolValue>(State.Env.getValue(*F)) : nullptr;
    if (!BV) return Out;
    const Formula &Moved = BV->formula();
    bool Certain = State.Env.proves(Moved);
    if (!Certain && !State.Env.allows(Moved)) return Out;
    // An unsatisfiable flow condition means the SAT solver proved this point dead
    // (e.g. `if (false_returning_call())`); there `proves(anything)` holds vacuously.
    if (DeadPathGuard && !State.Env.allows(State.Env.arena().makeLiteral(true))) return Out;
    if (!Reported.insert(E).second) return Out;
    std::string Name;
    if (const auto *DR = dyn_cast<DeclRefExpr>(E)) Name = DR->getDecl()->getNameAsString();
    else Name = cast<MemberExpr>(E)->getMemberDecl()->getNameAsString();
    Out.push_back({E->getExprLoc(), Name, Certain, E});
    return Out;
  }

private:
  // `x = ...` (x is the destination), `x.reset()`: not a use.
  static bool isReinitContext(const Expr *E, ASTContext &Ctx) {
    for (const DynTypedNode &P : Ctx.getParents(*E)) {
      if (const auto *OC = P.get<CXXOperatorCallExpr>())
        if (OC->getOperator() == OO_Equal && OC->getNumArgs() == 2 && OC->getArg(0) == E) return true;
      if (const auto *ME = P.get<MemberExpr>())
        if (isReinitMethod(dyn_cast<CXXMethodDecl>(ME->getMemberDecl()))) return true;
    }
    return false;
  }
};

inline std::string formatDiag(const SourceManager &SM, const MoveDiag &D) {
  PresumedLoc PL = SM.getPresumedLoc(D.Loc);
  std::string S;
  llvm::raw_string_ostream OS(S);
  OS << PL.getFilename() << ":" << PL.getLine() << ":" << PL.getColumn()
     << ": warning: '" << D.Var << "' used after move (" << (D.Certain ? "certain" : "possible") << ")";
  return OS.str();
}

} // namespace p07
#endif
