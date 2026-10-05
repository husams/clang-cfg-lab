// p04_reach -- block reachability (Part 4.3).
//
//   p04_reach <file> [--func=NAME] [--from=N] [--to=N] [--matrix]
//
// For each function prints
//   scan     reachable_code::ScanReachableFromBlock(&entry): count and blocks
//   dead     blocks that scan does not reach
//   self     blocks B with isReachable(B, B) -- the experiment: is this a cycle test?
//   cycle    blocks that lie on a cycle, found correctly: some successor S of B
//            has S == B or isReachable(S, B)
//   --matrix one line per block: every destination isReachable(Src, Dst) accepts
//   --from/--to answer one query: isReachable(B<from>, B<to>)

#include "cfglab.h"

#include "clang/Analysis/Analyses/CFGReachabilityAnalysis.h"
#include "clang/Analysis/Analyses/ReachableCode.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "llvm/ADT/BitVector.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p04_reach options");
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat), llvm::cl::desc("only this function"));
static llvm::cl::opt<int> From("from", llvm::cl::init(-1), llvm::cl::cat(Cat), llvm::cl::desc("source block id"));
static llvm::cl::opt<int> To("to", llvm::cl::init(-1), llvm::cl::cat(Cat), llvm::cl::desc("destination block id"));
static llvm::cl::opt<bool> Matrix("matrix", llvm::cl::cat(Cat), llvm::cl::desc("print the whole relation"));

static const CFGBlock *byId(const CFG &G, unsigned Id) {
  for (const CFGBlock *B : G)
    if (B->getBlockID() == Id) return B;
  return nullptr;
}

static std::string list(const std::vector<const CFGBlock *> &V) {
  std::string S;
  for (const CFGBlock *B : V) S += (S.empty() ? "" : " ") + cfglab::blockName(B);
  return S.empty() ? "-" : S;
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

        // The analysis is cached in the context and is lazy: each destination is
        // mapped (one backwards walk) the first time it is asked about.
        CFGReverseBlockReachabilityAnalysis *RA = AC->getCFGReachablityAnalysis();

        if (From >= 0 && To >= 0) {
          const CFGBlock *S = byId(*G, From), *D = byId(*G, To);
          if (!S || !D) { llvm::outs() << "no such block\n"; return; }
          llvm::outs() << "isReachable(" << cfglab::blockName(S) << ", " << cfglab::blockName(D)
                       << ") = " << (RA->isReachable(S, D) ? "true" : "false") << "\n";
          return;
        }

        llvm::BitVector Seen(G->getNumBlockIDs());
        unsigned N = reachable_code::ScanReachableFromBlock(&G->getEntry(), Seen);
        std::vector<const CFGBlock *> Live, Dead, Self, Cycle;
        for (const CFGBlock *B : *G) {
          (Seen[B->getBlockID()] ? Live : Dead).push_back(B);
          if (RA->isReachable(B, B)) Self.push_back(B);
          for (const CFGBlock *S : B->succs())
            if (S && (S == B || RA->isReachable(S, B))) { Cycle.push_back(B); break; }
        }
        llvm::outs() << "scan    : " << N << " reachable from entry: " << list(Live) << "\n";
        llvm::outs() << "dead    : " << list(Dead) << "\n";
        llvm::outs() << "self    : " << list(Self) << "\n";
        llvm::outs() << "cycle   : " << list(Cycle) << "\n";

        if (Matrix)
          for (const CFGBlock *S : *G) {
            std::vector<const CFGBlock *> R;
            for (const CFGBlock *D : *G)
              if (RA->isReachable(S, D)) R.push_back(D);
            std::string Name = cfglab::blockName(S);
            Name.resize(4, ' ');
            llvm::outs() << Name << "-> " << list(R) << "\n";
          }
      });
}
