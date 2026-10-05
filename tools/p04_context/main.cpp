// p04_context -- AnalysisDeclContext: lazy CFGs and cached analyses (Part 4.1).
//
//   p04_context <file> [--func=NAME] [--mode=report|options|late|forced]
//                      [--mgr-unoptimized]
//
//   report   (default) which objects exist after which call, pointer identity,
//            pruned-edge counts of getCFG() versus getUnoptimizedCFG()
//   options  the CFG::BuildOptions an AnalysisDeclContextManager starts from
//   late     change an option AFTER the first getCFG() and show nothing happens
//   forced   registerForcedBlockExpression / getBlockForRegisteredExpression

#include "cfglab.h"

#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFGStmtMap.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p04_context options");
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat), llvm::cl::desc("only this function"));
static llvm::cl::opt<std::string> ModeOpt("mode", llvm::cl::init("report"), llvm::cl::cat(Cat),
                                          llvm::cl::desc("report|options|late|forced"));
static llvm::cl::opt<bool> MgrUnopt("mgr-unoptimized", llvm::cl::cat(Cat),
                                    llvm::cl::desc("construct the manager with useUnoptimizedCFG=true"));

static unsigned prunedEdges(const CFG &G) {
  unsigned N = 0;
  for (const CFGBlock *B : G)
    for (const CFGBlock::AdjacentBlock &S : B->succs())
      N += !S.isReachable();
  return N;
}

static const char *yn(bool B) { return B ? "yes" : "no"; }

static void printOptions(const CFG::BuildOptions &BO) {
  for (const auto &F : cfglab::optionFields())
    llvm::outs() << "  " << llvm::left_justify(F.Name, 42) << (BO.*(F.Ptr) ? "true" : "false") << "\n";
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncOpt.empty() && FD->getQualifiedNameAsString() != FuncOpt) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";

        // One manager per translation unit in real code; one per function here
        // keeps the demos independent.
        AnalysisDeclContextManager Mgr(Ctx, /*useUnoptimizedCFG=*/MgrUnopt);
        AnalysisDeclContext *AC = Mgr.getContext(FD);

        if (ModeOpt == "options") {
          printOptions(AC->getCFGBuildOptions());
          return;
        }

        if (ModeOpt == "late") {
          CFG *First = AC->getCFG();
          AC->getCFGBuildOptions().AddScopes = true; // read only when the CFG is built
          llvm::outs() << "after AddScopes=true, getCFG() returns the same object: "
                       << yn(AC->getCFG() == First) << "\n";
          AC->getCFGBuildOptions().PruneTriviallyFalseEdges = false; // read on EVERY getCFG()
          CFG *Second = AC->getCFG();
          llvm::outs() << "after Prune=false, getCFG() returns the same object: " << yn(Second == First) << "\n";
          llvm::outs() << "  ... it is getUnoptimizedCFG(): " << yn(Second == AC->getUnoptimizedCFG()) << "\n";
          llvm::outs() << "  pruned edges: first " << prunedEdges(*First) << ", second " << prunedEdges(*Second) << "\n";
          return;
        }

        if (ModeOpt == "forced") {
          // Find the first 'if' and register its condition as a forced block expression.
          struct V : RecursiveASTVisitor<V> {
            const Stmt *Cond = nullptr;
            bool VisitIfStmt(IfStmt *S) { if (!Cond) Cond = S->getCond(); return true; }
          } Vis;
          Vis.TraverseStmt(AC->getBody());
          if (!Vis.Cond) { llvm::outs() << "no if statement\n"; return; }
          AC->registerForcedBlockExpression(Vis.Cond);
          CFG *G = AC->getCFG();
          const CFGBlock *B = AC->getBlockForRegisteredExpression(Vis.Cond);
          llvm::outs() << "forced expression: " << cfglab::stmtText(Vis.Cond, Ctx) << "\n";
          llvm::outs() << "block holding it: " << cfglab::blockName(B) << " of " << G->size() << " blocks\n";
          return;
        }

        // ---- report ----
        llvm::outs() << "getBody() is the FunctionDecl's body: " << yn(AC->getBody() == FD->getBody()) << "\n";

        CFG *G1 = AC->getCFG();
        CFG *G2 = AC->getCFG();
        llvm::outs() << "getCFG(): " << (G1 ? G1->size() : 0) << " blocks, second call returns the same object: "
                     << yn(G1 == G2) << "\n";
        llvm::outs() << "  pruned edges: " << prunedEdges(*G1) << "\n";

        CFG *U1 = AC->getUnoptimizedCFG();
        CFG *U2 = AC->getUnoptimizedCFG();
        llvm::outs() << "getUnoptimizedCFG(): " << U1->size() << " blocks, cached: " << yn(U1 == U2)
                     << ", distinct from getCFG(): " << yn(U1 != G1) << "\n";
        llvm::outs() << "  pruned edges: " << prunedEdges(*U1) << "\n";

        auto *P1 = AC->getAnalysis<PostOrderCFGView>();
        auto *P2 = AC->getAnalysis<PostOrderCFGView>();
        unsigned N = 0;
        for (const CFGBlock *B : *P1) { (void)B; ++N; }
        llvm::outs() << "getAnalysis<PostOrderCFGView>(): " << N << " blocks in RPO, cached: " << yn(P1 == P2) << "\n";

        auto *L1 = AC->getAnalysis<LiveVariables>();
        auto *L2 = AC->getAnalysis<LiveVariables>();
        llvm::outs() << "getAnalysis<LiveVariables>(): " << (L1 ? "built" : "null") << ", cached: " << yn(L1 == L2) << "\n";

        const CFGStmtMap *M1 = AC->getCFGStmtMap();
        llvm::outs() << "getCFGStmtMap(): cached: " << yn(M1 == AC->getCFGStmtMap()) << "\n";
        auto *R1 = AC->getCFGReachablityAnalysis();
        llvm::outs() << "getCFGReachablityAnalysis() [sic]: cached: " << yn(R1 == AC->getCFGReachablityAnalysis()) << "\n";
        llvm::outs() << "ParentMap: " << yn(&AC->getParentMap() == &AC->getParentMap()) << " (same object each call)\n";
      });
}
