// constprop.h -- the constant-propagation lattice and analysis used by Part 6.
//
//   Undef  <  Const(v)  <  Top        (a flat lattice: height 3)
//
// "Undef" is bottom (no value assigned on any path seen so far), "Top" means
// "not a single known constant". VarMapLattice (= MapLattice<const VarDecl*, E>)
// adds the map part: a variable that is absent from the map is bottom.
#ifndef P06_CONSTPROP_H
#define P06_CONSTPROP_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/MapLattice.h"
#include "llvm/ADT/StringExtras.h"

#include <algorithm>
#include <string>
#include <vector>

namespace p06 {
using namespace clang;
using namespace clang::dataflow;

// ---- the element lattice ---------------------------------------------------
struct Const {
  enum Kind { Undef, Val, Top } K = Undef;
  long long V = 0;

  static Const val(long long X) { return {Val, X}; }
  static Const top() { return {Top, 0}; }

  // join = least upper bound, in place; returns whether *this changed.
  LatticeEffect join(const Const &O) {
    if (O.K == Undef || K == Top) return LatticeEffect::Unchanged;
    if (K == Undef) { *this = O; return LatticeEffect::Changed; }
    if (O.K == Top || O.V != V) { *this = top(); return LatticeEffect::Changed; }
    return LatticeEffect::Unchanged;
  }
  bool operator==(const Const &O) const { return K == O.K && (K != Val || V == O.V); }
  std::string str() const { return K == Undef ? "undef" : K == Top ? "TOP" : std::to_string(V); }
};

using Lat = VarMapLattice<Const>;

// Abstract evaluation of an expression in the state L. Parameters and anything
// we do not understand evaluate to Top.
inline Const eval(const Expr *E, const Lat &L) {
  E = E->IgnoreParenImpCasts();
  if (auto *I = dyn_cast<IntegerLiteral>(E)) return Const::val(I->getValue().getSExtValue());
  if (auto *D = dyn_cast<DeclRefExpr>(E))
    if (auto *V = dyn_cast<VarDecl>(D->getDecl())) {
      auto It = L.find(V);
      return It == L.end() ? Const::top() : It->second;
    }
  if (auto *U = dyn_cast<UnaryOperator>(E); U && U->getOpcode() == UO_Minus) {
    Const A = eval(U->getSubExpr(), L);
    return A.K == Const::Val ? Const::val(-A.V) : A.K == Const::Undef ? A : Const::top();
  }
  if (auto *B = dyn_cast<BinaryOperator>(E); B && !B->isAssignmentOp()) {
    Const A = eval(B->getLHS(), L), C = eval(B->getRHS(), L);
    if (A.K == Const::Val && C.K == Const::Val)
      switch (B->getOpcode()) {
      case BO_Add: return Const::val(A.V + C.V);
      case BO_Sub: return Const::val(A.V - C.V);
      case BO_Mul: return Const::val(A.V * C.V);
      default: break;
      }
  }
  return Const::top();
}

inline const VarDecl *lhsVar(const Expr *E) {
  if (auto *D = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts()))
    return dyn_cast<VarDecl>(D->getDecl());
  return nullptr;
}

// ---- the analysis ----------------------------------------------------------
class ConstProp : public DataflowAnalysis<ConstProp, Lat> {
public:
  explicit ConstProp(ASTContext &C) : DataflowAnalysis<ConstProp, Lat>(C) {}

  // The state at the entry of the function: nothing known (every var is bottom).
  static Lat initialElement() { return {}; }

  void transfer(const CFGElement &Elt, Lat &L, Environment &) {
    auto S = Elt.getAs<CFGStmt>();
    if (!S) return;
    const Stmt *St = S->getStmt();
    if (auto *DS = dyn_cast<DeclStmt>(St)) {
      for (Decl *D : DS->decls())
        if (auto *V = dyn_cast<VarDecl>(D))
          L[V] = V->hasInit() ? eval(V->getInit(), L) : Const{};
    } else if (auto *B = dyn_cast<BinaryOperator>(St); B && B->isAssignmentOp()) {
      if (const VarDecl *V = lhsVar(B->getLHS())) {
        switch (B->getOpcode()) {
        case BO_Assign: L[V] = eval(B->getRHS(), L); break;
        case BO_AddAssign: case BO_SubAssign: case BO_MulAssign: {
          Const A = L.contains(V) ? L[V] : Const::top(), R = eval(B->getRHS(), L);
          if (A.K == Const::Val && R.K == Const::Val) {
            long long X = B->getOpcode() == BO_AddAssign ? A.V + R.V
                        : B->getOpcode() == BO_SubAssign ? A.V - R.V : A.V * R.V;
            L[V] = Const::val(X);
          } else L[V] = Const::top();
          break;
        }
        default: L[V] = Const::top();
        }
      }
    } else if (auto *U = dyn_cast<UnaryOperator>(St); U && U->isIncrementDecrementOp()) {
      if (const VarDecl *V = lhsVar(U->getSubExpr())) {
        Const A = L.contains(V) ? L[V] : Const::top();
        L[V] = A.K == Const::Val ? Const::val(A.V + (U->isIncrementOp() ? 1 : -1)) : Const::top();
      }
    }
  }
};

// Deterministic printing: a DenseMap iterates in pointer-hash order, so sort by
// source position.
inline std::string show(const Lat &L, const SourceManager &SM) {
  std::vector<std::pair<const VarDecl *, Const>> Items(L.begin(), L.end());
  std::sort(Items.begin(), Items.end(), [&](auto &A, auto &B) {
    return SM.isBeforeInTranslationUnit(A.first->getLocation(), B.first->getLocation());
  });
  std::string Out;
  for (auto &[V, C] : Items) {
    if (!Out.empty()) Out += ' ';
    Out += V->getNameAsString() + "=" + C.str();
  }
  return Out.empty() ? "(empty)" : Out;
}
} // namespace p06
#endif
