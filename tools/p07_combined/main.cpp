// p07_combined -- Part 7.7: classic CFG analyses next to the dataflow checker.
//
//   build/bin/p07_combined FILE [--func=NAME] [--first-use-only] [--auto-budget=PCT]
//
// Per function, in one row: CFG shape (blocks, edges, cyclomatic number, loops,
// reducible?), dataflow cost (transfer calls), diagnostics. Classic results then
// serve the dataflow results three ways:
//   1. budget:     MaxBlockVisits = PCT% * blocks * (1 + loops)   (--auto-budget=PCT)
//   2. cross-check each diagnostic's use must be reachable from some std::move  (xcheck)
//   3. noise:      --first-use-only drops a report dominated by an earlier one on the same object

#include "cfglab.h"
#include "../p07_tu/Driver.h"

#include "clang/AST/ParentMap.h"
#include "clang/Analysis/Analyses/CFGReachabilityAnalysis.h"
#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/Analyses/IntervalPartition.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFGStmtMap.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p07_combined options");
static llvm::cl::opt<std::string> CbFunc("func", llvm::cl::desc("only this function"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> CbFirst("first-use-only", llvm::cl::desc("drop reports dominated by an earlier report on the same object"), llvm::cl::cat(Cat));
static llvm::cl::opt<int> CbAuto("auto-budget", llvm::cl::desc("MaxBlockVisits = PCT% of blocks * (1 + loops); 0 = default"), llvm::cl::init(0), llvm::cl::cat(Cat));

namespace {
// back edges by DFS colouring: an edge to a node on the current DFS stack
unsigned countBackEdges(const CFG &G) {
  enum { White, Grey, Black };
  std::vector<int> Col(G.getNumBlockIDs(), White);
  unsigned Back = 0;
  std::function<void(const CFGBlock *)> Dfs = [&](const CFGBlock *B) {
    Col[B->getBlockID()] = Grey;
    for (const CFGBlock *S : B->succs()) {
      if (!S) continue;
      if (Col[S->getBlockID()] == Grey) ++Back;
      else if (Col[S->getBlockID()] == White) Dfs(S);
    }
    Col[B->getBlockID()] = Black;
  };
  Dfs(&G.getEntry());
  return Back;
}

struct MoveSites : RecursiveASTVisitor<MoveSites> {
  std::vector<const Stmt *> Sites;
  bool VisitCallExpr(CallExpr *CE) {
    if (p07::isStdMoveCall(CE)) Sites.push_back(CE);
    return true;
  }
};
} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!CbFunc.empty() && FD->getNameAsString() != CbFunc) return;
        // ---- classic side: a CFG built the way AdornedCFG builds it ----------------------------
        AnalysisDeclContextManager Mgr(Ctx);
        AnalysisDeclContext *AC = Mgr.getContext(FD);
        cfglab::applyPreset(AC->getCFGBuildOptions(), cfglab::adornedPreset());
        CFG *G = AC->getCFG();
        if (!G) return;
        unsigned N = G->size(), E = 0;
        for (const CFGBlock *B : *G)
          for (const CFGBlock *S : B->succs())
            if (S) ++E;
        unsigned Loops = countBackEdges(*G);
        bool Reducible = getIntervalWTO(*G).has_value();

        // ---- budget from the shape ---------------------------------------------------------------
        p07::Options O;
        if (CbAuto > 0) O.B.MaxVisits = std::max(1u, CbAuto * N * (1 + Loops) / 100);

        // ---- dataflow side -------------------------------------------------------------------------
        p07::FnResult R = p07::analyze(FD, Ctx, O);
        llvm::outs() << llvm::format("%-12s blocks=%-3u edges=%-3u cyclo=%-2d loops=%u %s", FD->getNameAsString().c_str(), N, E,
                                     (int)E - (int)N + 2, Loops, Reducible ? "reducible" : "irreducible");
        if (CbAuto > 0) llvm::outs() << " budget=" << O.B.MaxVisits;
        llvm::outs() << " -> " << p07::statusName(R.St);
        if (R.St != p07::Status::Analyzed) { llvm::outs() << "\n"; return; }
        llvm::outs() << " transfers=" << R.Transfers << " diags=" << R.Diags.size() << "\n";
        if (R.Diags.empty()) return;

        // ---- combine ---------------------------------------------------------------------------------
        ParentMap PM(const_cast<Stmt *>(FD->getBody()));
        CFGStmtMap SMap(*G, PM);
        CFGReverseBlockReachabilityAnalysis RA(*G);
        CFGDomTree DT(G);
        MoveSites MS;
        MS.TraverseStmt(FD->getBody());

        std::vector<const CFGBlock *> Kept;   // blocks of reports already emitted, per variable name
        std::map<std::string, std::vector<const p07::MoveDiag *>> Emitted;
        for (const p07::MoveDiag &D : R.Diags) {
          const CFGBlock *UseB = SMap.getBlock(D.At);
          bool Reach = false;
          for (const Stmt *M : MS.Sites)
            if (const CFGBlock *MB = SMap.getBlock(M))
              if (MB == UseB || RA.isReachable(MB, UseB)) Reach = true;
          bool Dominated = false;
          if (CbFirst)
            for (const p07::MoveDiag *Prev : Emitted[D.Var]) {
              const CFGBlock *PB = SMap.getBlock(Prev->At);
              if (PB == UseB ? Ctx.getSourceManager().isBeforeInTranslationUnit(Prev->Loc, D.Loc)
                             : DT.properlyDominates(PB, UseB))
                Dominated = true;
            }
          llvm::outs() << "  " << p07::formatDiag(Ctx.getSourceManager(), D) << "  [B"
                       << (UseB ? UseB->getBlockID() : 0) << ", xcheck " << (Reach ? "ok" : "SUSPICIOUS") << "]"
                       << (Dominated ? "  (dropped: dominated by an earlier report)" : "") << "\n";
          if (!Dominated) Emitted[D.Var].push_back(&D);
        }
      });
}
