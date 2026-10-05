// p05_liveness -- hand-written backward liveness vs clang::LiveVariables (Part 5.1).
//
//   p05_liveness <file> [--func=NAME] [--relaxed] [--dump]
//
// The hand-written analysis works on the same CFG that LiveVariables uses
// (Sema preset, setAllAlwaysAdd). For every block it prints the set of
// variables live at the END of the block (live-out) from both analyses.

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Analyses/LiveVariables.h"

#include <cstdint>
#include <set>

using namespace clang;

static llvm::cl::OptionCategory Cat("p05_liveness options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::desc("only this function"),
                                           llvm::cl::cat(Cat));
static llvm::cl::opt<bool> DumpFlag("dump", llvm::cl::desc("also call LiveVariables::dumpBlockLiveness (writes to stderr)"),
                                    llvm::cl::cat(Cat));
static llvm::cl::opt<bool> RelaxedFlag("relaxed",
                                       llvm::cl::desc("killAtAssign=false: RelaxedLiveVariables, and no kill in the hand-written pass"),
                                       llvm::cl::cat(Cat));

namespace {

// Variables we track: parameters and locals referenced in the function.
struct VarCollector : RecursiveASTVisitor<VarCollector> {
  std::vector<const VarDecl *> Vars;
  std::set<const VarDecl *> Seen;
  void add(const VarDecl *VD) {
    if (!VD->hasLocalStorage() || VD->getType()->isReferenceType()) return;
    if (Seen.insert(VD).second) Vars.push_back(VD);
  }
  bool VisitVarDecl(VarDecl *VD) { add(VD); return true; }
  bool VisitDeclRefExpr(DeclRefExpr *DR) {
    if (auto *VD = dyn_cast<VarDecl>(DR->getDecl())) add(VD);
    return true;
  }
};

// DeclRefExprs that are the left-hand side of a plain '='.
struct LhsCollector : RecursiveASTVisitor<LhsCollector> {
  std::set<const DeclRefExpr *> Lhs;
  bool VisitBinaryOperator(BinaryOperator *B) {
    if (B->getOpcode() == BO_Assign)
      if (auto *DR = dyn_cast<DeclRefExpr>(B->getLHS()->IgnoreParens())) Lhs.insert(DR);
    return true;
  }
};

using Bits = uint64_t;

struct HandLiveness {
  const std::vector<const VarDecl *> &Vars;
  const std::set<const DeclRefExpr *> &Lhs;
  bool Kill;
  std::map<const CFGBlock *, Bits> Out;
  unsigned Rounds = 0;

  int indexOf(const VarDecl *VD) const {
    for (size_t I = 0; I < Vars.size(); ++I)
      if (Vars[I] == VD) return static_cast<int>(I);
    return -1;
  }

  // Backward transfer of one block: in = f(out), walking elements in reverse.
  Bits transfer(const CFGBlock *B, Bits Live) const {
    for (auto I = B->rbegin(), E = B->rend(); I != E; ++I) {
      auto CS = I->getAs<CFGStmt>();
      if (!CS) continue;
      const Stmt *S = CS->getStmt();
      if (auto *BO = dyn_cast<BinaryOperator>(S)) {
        if (Kill && BO->getOpcode() == BO_Assign)
          if (auto *DR = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParens()))
            if (auto *VD = dyn_cast<VarDecl>(DR->getDecl()))
              if (int K = indexOf(VD); K >= 0) Live &= ~(Bits(1) << K);
      } else if (auto *DS = dyn_cast<DeclStmt>(S)) {
        for (const Decl *D : DS->decls())
          if (auto *VD = dyn_cast<VarDecl>(D))
            if (int K = indexOf(VD); K >= 0) Live &= ~(Bits(1) << K);
      } else if (auto *DR = dyn_cast<DeclRefExpr>(S)) {
        // The left-hand side of '=' is a write, not a read -- but only when we kill.
        if (Kill && Lhs.count(DR)) continue;
        if (auto *VD = dyn_cast<VarDecl>(DR->getDecl()))
          if (int K = indexOf(VD); K >= 0) Live |= Bits(1) << K;
      }
    }
    return Live;
  }

  void solve(const CFG &G) {
    bool Changed = true;
    while (Changed) {
      Changed = false;
      ++Rounds;
      for (const CFGBlock *B : llvm::reverse(G)) { // exit has the lowest id: go from high id to low
        Bits O = 0;
        for (const CFGBlock::AdjacentBlock &S : B->succs())
          if (const CFGBlock *SB = S) O |= transfer(SB, Out[SB]);
        if (O != Out[B]) { Out[B] = O; Changed = true; }
      }
    }
  }
};

std::string setText(Bits M, const std::vector<const VarDecl *> &Vars) {
  std::string S = "{";
  bool First = true;
  for (size_t I = 0; I < Vars.size(); ++I)
    if (M & (Bits(1) << I)) {
      if (!First) S += " ";
      S += Vars[I]->getNameAsString();
      First = false;
    }
  return S + "}";
}

} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;

        AnalysisDeclContext AC(nullptr, FD);
        cfglab::applyPreset(AC.getCFGBuildOptions(), cfglab::semaPreset());
        const CFG *G = AC.getCFG();
        if (!G) return;

        VarCollector VC;
        VC.TraverseDecl(const_cast<FunctionDecl *>(FD));
        LhsCollector LC;
        LC.TraverseStmt(const_cast<Stmt *>(FD->getBody()));
        if (VC.Vars.size() > 64) return;

        HandLiveness H{VC.Vars, LC.Lhs, !RelaxedFlag, {}, 0};
        H.solve(*G);

        std::unique_ptr<LiveVariables> LV = RelaxedFlag ? RelaxedLiveVariables::create(AC)
                                                        : LiveVariables::create(AC);
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "  ("
                     << (RelaxedFlag ? "RelaxedLiveVariables" : "LiveVariables")
                     << ", hand-written rounds=" << H.Rounds << ")\n";
        unsigned Mismatch = 0;
        for (const CFGBlock *B : llvm::reverse(*G)) {
          Bits LVBits = 0;
          for (size_t I = 0; I < VC.Vars.size(); ++I)
            if (LV->isLive(B, VC.Vars[I])) LVBits |= Bits(1) << I;
          bool Same = LVBits == H.Out[B];
          Mismatch += !Same;
          llvm::outs() << "  " << llvm::format("%-4s", cfglab::blockName(B).c_str())
                       << " live-out hand=" << llvm::format("%-12s", setText(H.Out[B], VC.Vars).c_str())
                       << " LV=" << llvm::format("%-12s", setText(LVBits, VC.Vars).c_str())
                       << (Same ? " same" : " DIFFERENT") << "\n";
        }
        llvm::outs() << "  blocks that differ: " << Mismatch << "\n";
        if (DumpFlag) {
          llvm::outs().flush();
          LV->dumpBlockLiveness(Ctx.getSourceManager());
          llvm::errs().flush();
        }
      });
}
