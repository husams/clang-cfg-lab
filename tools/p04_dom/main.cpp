// p04_dom -- dominators, post-dominators and natural loops (Part 4.4).
//
//   p04_dom <file> [--func=NAME] [--dump] [--query=A,B] [--no-loops]
//
// Default output per function:
//   idom      CFGDomTree: block <- its immediate dominator
//   dom tree  the tree drawn with indentation
//   ipdom     CFGPostDomTree: block <- immediate post-dominator (<virtual> = the null root)
//   pdom tree the post-dominator tree
//   loops     back edges (u -> h where h dominates u), grouped into natural loops
//   retreat   DFS retreating edges that are NOT back edges (the signature of an irreducible CFG)
// --dump     also calls the two library dump() methods (they print to stderr)
// --query    dominates / properlyDominates / nearest common dominator for B<A>, B<B>

#include "cfglab.h"

#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/AnalysisDeclContext.h"

#include <algorithm>
#include <map>
#include <set>

using namespace clang;

static llvm::cl::OptionCategory Cat("p04_dom options");
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat), llvm::cl::desc("only this function"));
static llvm::cl::opt<bool> Dump("dump", llvm::cl::cat(Cat), llvm::cl::desc("call DT.dump() and PDT.dump()"));
static llvm::cl::opt<bool> NoLoops("no-loops", llvm::cl::cat(Cat), llvm::cl::desc("skip the loop analysis"));
static llvm::cl::opt<std::string> Query("query", llvm::cl::cat(Cat), llvm::cl::desc("A,B block ids"));

static unsigned id(const CFGBlock *B) { return B->getBlockID(); }

static const CFGBlock *byId(const CFG &G, unsigned Id) {
  for (const CFGBlock *B : G)
    if (B->getBlockID() == Id) return B;
  return nullptr;
}

// Print a (post-)dominator tree rooted at N, children in block-id order.
static void printTree(DomTreeNode *N, unsigned Depth) {
  std::string Name = N->getBlock() ? cfglab::blockName(N->getBlock()) : "<virtual>";
  llvm::outs() << "  " << std::string(Depth * 2, ' ') << Name << "\n";
  std::vector<DomTreeNode *> Kids(N->begin(), N->end());
  std::sort(Kids.begin(), Kids.end(), [](DomTreeNode *A, DomTreeNode *B) { return id(A->getBlock()) < id(B->getBlock()); });
  for (DomTreeNode *K : Kids) printTree(K, Depth + 1);
}

template <bool Post>
static void printIdoms(const CFG &G, CFGDominatorTreeImpl<Post> &T) {
  std::string Line;
  for (const CFGBlock *B : G) {
    auto *Node = T.getBase().getNode(B);
    if (!Node) { Line += cfglab::blockName(B) + "<-?  "; continue; }   // not in the tree
    DomTreeNode *I = Node->getIDom();
    if (!I) Line += cfglab::blockName(B) + "=root  ";
    else if (!I->getBlock()) Line += cfglab::blockName(B) + "<-<virtual>  ";
    else Line += cfglab::blockName(B) + "<-" + cfglab::blockName(I->getBlock()) + "  ";
  }
  llvm::outs() << Line << "\n";
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

        // Neither class has getTag()/create(): they are NOT reachable through
        // getAnalysis<>. Construct them from the CFG.
        CFGDomTree DT(G);
        CFGPostDomTree PDT(G);

        if (!Query.empty()) {
          unsigned A, B;
          if (sscanf(Query.c_str(), "%u,%u", &A, &B) != 2) { llvm::errs() << "--query=A,B\n"; return; }
          const CFGBlock *BA = byId(*G, A), *BB = byId(*G, B);
          if (!BA || !BB) { llvm::outs() << "no such block\n"; return; }
          llvm::outs() << "dominates(" << cfglab::blockName(BA) << ", " << cfglab::blockName(BB) << ") = " << DT.dominates(BA, BB) << "\n";
          llvm::outs() << "properlyDominates(" << cfglab::blockName(BA) << ", " << cfglab::blockName(BB) << ") = " << DT.properlyDominates(BA, BB) << "\n";
          llvm::outs() << "post: dominates(" << cfglab::blockName(BA) << ", " << cfglab::blockName(BB) << ") = " << PDT.dominates(BA, BB) << "\n";
          const CFGBlock *N = DT.findNearestCommonDominator(BA, BB);
          llvm::outs() << "nearest common dominator: " << cfglab::blockName(N) << "\n";
          const CFGBlock *P = PDT.findNearestCommonDominator(BA, BB);
          llvm::outs() << "nearest common post-dominator: " << cfglab::blockName(P) << "\n";
          return;
        }

        llvm::outs() << "root    : dom=" << cfglab::blockName(DT.getRoot()) << " postdom=" << cfglab::blockName(PDT.getRoot()) << "\n";
        llvm::outs() << "idom    : "; printIdoms(*G, DT);
        llvm::outs() << "dom tree:\n"; printTree(DT.getRootNode(), 0);
        llvm::outs() << "ipdom   : "; printIdoms(*G, PDT);
        llvm::outs() << "pdom tree:\n"; printTree(PDT.getRootNode(), 0);

        if (Dump) {
          llvm::outs().flush();
          DT.dump();
          PDT.dump();
        }
        if (NoLoops) return;

        // ---- back edges and natural loops ----
        std::map<unsigned, std::set<unsigned>> Tails;          // header -> back-edge sources
        for (const CFGBlock *U : *G) {
          if (!DT.isReachableFromEntry(U)) continue;
          for (const CFGBlock *H : U->succs())
            if (H && DT.dominates(H, U)) Tails[id(H)].insert(id(U));
        }
        std::map<unsigned, std::set<unsigned>> Body;           // header -> loop body (incl. header)
        for (auto &[Hid, Ts] : Tails) {
          std::set<unsigned> &Set = Body[Hid];
          Set.insert(Hid);
          std::vector<const CFGBlock *> Work;
          for (unsigned T : Ts)
            if (Set.insert(T).second) Work.push_back(byId(*G, T));
          while (!Work.empty()) {
            const CFGBlock *B = Work.back();
            Work.pop_back();
            for (const CFGBlock *P : B->preds())
              if (P && Set.insert(id(P)).second) Work.push_back(P);
          }
        }
        if (Tails.empty()) llvm::outs() << "loops   : none\n";
        for (auto &[Hid, Set] : Body) {
          std::string T, S;
          for (unsigned X : Tails[Hid]) T += (T.empty() ? "B" : ",B") + std::to_string(X);
          for (unsigned X : Set) S += (S.empty() ? "B" : " B") + std::to_string(X);
          unsigned Depth = 0;                                    // nesting depth = loops containing the header
          for (auto &[Oid, OSet] : Body) Depth += OSet.count(Hid);
          llvm::outs() << "loop    : header B" << Hid << " depth " << Depth << " back-edges from " << T << " body {" << S << "}\n";
        }

        // ---- retreating edges that are not back edges (irreducibility) ----
        enum { White, Grey, Black };
        std::vector<int> Color(G->getNumBlockIDs(), White);
        std::vector<std::pair<unsigned, unsigned>> Odd;
        struct Frame { const CFGBlock *B; CFGBlock::const_succ_iterator It; };
        std::vector<Frame> Stack;
        Stack.push_back({&G->getEntry(), G->getEntry().succ_begin()});
        Color[id(&G->getEntry())] = Grey;
        while (!Stack.empty()) {
          Frame &F = Stack.back();
          if (F.It == F.B->succ_end()) { Color[id(F.B)] = Black; Stack.pop_back(); continue; }
          const CFGBlock *S = *F.It;
          ++F.It;
          if (!S) continue;
          if (Color[id(S)] == Grey) {
            if (!DT.dominates(S, Stack.back().B)) Odd.push_back({id(Stack.back().B), id(S)});
          } else if (Color[id(S)] == White) {
            Color[id(S)] = Grey;
            Stack.push_back({S, S->succ_begin()});
          }
        }
        if (Odd.empty()) llvm::outs() << "retreat : every retreating edge is a back edge (reducible)\n";
        for (auto &[U, V] : Odd)
          llvm::outs() << "retreat : B" << U << " -> B" << V << " goes back in DFS but B" << V << " does not dominate B" << U << " (irreducible)\n";
      });
}
