// p02_edges -- successors, predecessors and AdjacentBlock (Part 2.5).
//
//   p02_edges <file> [--preset=..] [--set=..] [--clear=..] [--func=NAME]
//                    [--naive] [--filtered]
//
// Default: one line per block listing its successors in order and its
// predecessors. A pruned ("unreachable") successor prints as (unreach Bn).
//   --naive     iterate succs() the way most code does -- via the implicit
//               conversion to CFGBlock* -- and show the nullptr you get
//   --filtered  use CFGBlock::filtered_succ_start_end (with
//               IgnoreDefaultsWithCoveredEnums) and filtered_pred_start_end

#include "cfglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p02_edges options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat)
static llvm::cl::opt<bool> Naive("naive", llvm::cl::cat(Cat),
                                 llvm::cl::desc("show the null pointers a naive loop sees"));
static llvm::cl::opt<bool> Filtered("filtered", llvm::cl::cat(Cat),
                                    llvm::cl::desc("use filtered_succ_start_end"));

static std::string edge(const CFGBlock::AdjacentBlock &A) {
  if (A.isReachable())
    return cfglab::blockName(A.getReachableBlock());
  // A pruned edge: getReachableBlock() is null, but the block it WOULD have
  // gone to is still recorded.
  if (const CFGBlock *U = A.getPossiblyUnreachableBlock())
    return "(unreach " + cfglab::blockName(U) + ")";
  return "null";
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!flagWantsFunction(FD)) return;
        CFG::BuildOptions BO;
        if (!flagsToOptions(BO)) std::exit(2);
        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) return;

        unsigned EdgesAll = 0, EdgesReachable = 0;
        for (const CFGBlock *B : *G)
          for (const CFGBlock::AdjacentBlock &S : B->succs()) {
            ++EdgesAll;
            EdgesReachable += S.isReachable();
          }

        llvm::outs() << "== " << FD->getQualifiedNameAsString() << ": blocks=" << G->size()
                     << " isLinear=" << G->isLinear() << " edges=" << EdgesReachable
                     << " (+" << (EdgesAll - EdgesReachable) << " pruned)\n";

        for (const CFGBlock *B : *G) {
          std::string Line = cfglab::blockName(B);
          Line.resize(4, ' ');
          llvm::outs() << Line;

          const Stmt *T = B->getTerminatorStmt();
          std::string Term = T ? T->getStmtClassName() : "-";
          Term.resize(14, ' ');
          llvm::outs() << " T=" << Term;

          llvm::outs() << " succs:";
          if (Naive) {
            // `CFGBlock *S : B->succs()` compiles because AdjacentBlock
            // converts implicitly to CFGBlock* -- by calling
            // getReachableBlock(), which is nullptr for a pruned edge.
            for (CFGBlock *S : B->succs())
              llvm::outs() << " " << (S ? cfglab::blockName(S) : std::string("NULL"));
          } else if (Filtered) {
            CFGBlock::FilterOptions F;
            F.IgnoreDefaultsWithCoveredEnums = 1;
            for (auto I = B->filtered_succ_start_end(F); I.hasMore(); ++I)
              llvm::outs() << " " << cfglab::blockName(*I); // *I is a plain CFGBlock*
          } else {
            for (const CFGBlock::AdjacentBlock &S : B->succs())
              llvm::outs() << " " << edge(S);
          }

          llvm::outs() << "   preds:";
          if (Filtered) {
            CFGBlock::FilterOptions F; // IgnoreNullPredecessors is on by default
            for (auto I = B->filtered_pred_start_end(F); I.hasMore(); ++I)
              llvm::outs() << " " << cfglab::blockName(*I);
          } else {
            for (const CFGBlock::AdjacentBlock &P : B->preds())
              llvm::outs() << " " << edge(P);
          }
          llvm::outs() << "\n";
        }
      });
}
