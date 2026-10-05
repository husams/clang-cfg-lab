// p04_orders -- iteration orders over a CFG (Part 4.2).
//
//   p04_orders <file> [--func=NAME] [--vs-llvm]
//
// For each function prints
//   ids     blocks in CFG iteration order (the order of CFG::begin())
//   RPO     PostOrderCFGView, front to back (loop-body-first reverse post order)
//   sorted  the same blocks after std::sort with PostOrderCFGView::getComparator()
//   WTO     getIntervalWTO(), or "none (irreducible)"
//   WTOrank the BlockOrder value WTOCompare stores for each block
//   missing blocks that are in the CFG but not in the RPO (unreachable)
// --vs-llvm adds llvm::ReversePostOrderTraversal with the default child order.

#include "cfglab.h"

#include "clang/Analysis/Analyses/IntervalPartition.h"
#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "llvm/ADT/PostOrderIterator.h"

#include <algorithm>

using namespace clang;

static llvm::cl::OptionCategory Cat("p04_orders options");
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat), llvm::cl::desc("only this function"));
static llvm::cl::opt<bool> VsLLVM("vs-llvm", llvm::cl::cat(Cat),
                                  llvm::cl::desc("also print llvm::ReversePostOrderTraversal"));

template <typename Range> static std::string ids(const Range &R) {
  std::string S;
  for (const CFGBlock *B : R) S += (S.empty() ? "" : " ") + cfglab::blockName(B);
  return S;
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncOpt.empty() && FD->getQualifiedNameAsString() != FuncOpt) return;
        AnalysisDeclContextManager Mgr(Ctx);
        AnalysisDeclContext *AC = Mgr.getContext(FD);
        CFG *G = AC->getCFG();
        if (!G) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << ": " << G->size() << " blocks\n";

        std::vector<const CFGBlock *> Ids;
        for (const CFGBlock *B : *G) Ids.push_back(B);
        llvm::outs() << "ids     : " << ids(Ids) << "\n";

        // PostOrderCFGView is a ManagedAnalysis: ask the context, do not construct it.
        PostOrderCFGView *RPO = AC->getAnalysis<PostOrderCFGView>();
        std::vector<const CFGBlock *> Rpo(RPO->begin(), RPO->end());
        llvm::outs() << "RPO     : " << ids(Rpo) << "\n";

        std::vector<const CFGBlock *> Sorted(Ids);
        std::sort(Sorted.begin(), Sorted.end(), RPO->getComparator());
        llvm::outs() << "sorted  : " << ids(Sorted) << "   (std::sort with getComparator())\n";

        std::vector<const CFGBlock *> Missing;
        for (const CFGBlock *B : *G)
          if (std::find(Rpo.begin(), Rpo.end(), B) == Rpo.end()) Missing.push_back(B);
        llvm::outs() << "missing : " << (Missing.empty() ? "-" : ids(Missing)) << "\n";

        if (std::optional<WeakTopologicalOrdering> W = getIntervalWTO(*G)) {
          llvm::outs() << "WTO     : " << ids(*W) << "\n";
          WTOCompare Cmp(*W);
          std::string R;
          for (const CFGBlock *B : *W) R += (R.empty() ? "" : " ") + cfglab::blockName(B) + "=" + std::to_string(Cmp.BlockOrder[B->getBlockID()]);
          llvm::outs() << "WTOrank : " << R << "\n";
        } else {
          llvm::outs() << "WTO     : none (irreducible)\n";
        }

        if (VsLLVM) {
          bool HasNull = false;
          for (const CFGBlock *B : *G)
            for (const CFGBlock::AdjacentBlock &S : B->succs()) HasNull |= !S.isReachable();
          if (HasNull) {
            llvm::outs() << "llvm RPO: skipped (a pruned edge is a null successor; see Section 4.6)\n";
            return;
          }
          llvm::ReversePostOrderTraversal<const CFG *> LRPO(G);
          std::vector<const CFGBlock *> L(LRPO.begin(), LRPO.end());
          llvm::outs() << "llvm RPO: " << ids(L) << "\n";
        }
      });
}
