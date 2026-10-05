// p04_graphs -- LLVM's generic graph algorithms over the CFG (Part 4.6).
//
//   p04_graphs <file> [--func=NAME] [--raw]
//
// Runs post_order, reverse post order, depth_first, post_order(Inverse<>) and
// scc_iterator over the CFG through GraphTraits, then finds cycles three ways.
//
// By default the algorithms run over SafeCFG, a copy of the graph that drops the
// null successors a pruned edge produces (see GraphTraits<const SafeCFG*> below).
// --raw runs them over `const CFG *` directly: fine for a CFG without pruned
// edges, a crash for one with them.

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SCCIterator.h"

#include <deque>

using namespace clang;

static llvm::cl::OptionCategory Cat("p04_graphs options");
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat), llvm::cl::desc("only this function"));
static llvm::cl::opt<bool> Raw("raw", llvm::cl::cat(Cat), llvm::cl::desc("use GraphTraits<const CFG*> directly"));

// ---- SafeCFG: our own graph type, with our own GraphTraits ----------------------------
struct SNode {
  const CFGBlock *B;
  std::vector<const SNode *> Succs, Preds;
};
struct SafeCFG {
  std::deque<SNode> Nodes;          // stable addresses
  const SNode *Entry = nullptr, *Exit = nullptr;
  explicit SafeCFG(const CFG &G) {
    std::vector<SNode *> ById(G.getNumBlockIDs(), nullptr);
    for (const CFGBlock *B : G) { Nodes.push_back({B, {}, {}}); ById[B->getBlockID()] = &Nodes.back(); }
    for (const CFGBlock *B : G)
      for (const CFGBlock *S : B->succs())
        if (S) { ById[B->getBlockID()]->Succs.push_back(ById[S->getBlockID()]);
                 ById[S->getBlockID()]->Preds.push_back(ById[B->getBlockID()]); }
    Entry = ById[G.getEntry().getBlockID()];
    Exit = ById[G.getExit().getBlockID()];
  }
};

namespace llvm {
template <> struct GraphTraits<const SNode *> {
  using NodeRef = const SNode *;
  using ChildIteratorType = std::vector<const SNode *>::const_iterator;
  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) { return N->Succs.begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->Succs.end(); }
};
template <> struct GraphTraits<Inverse<const SNode *>> {
  using NodeRef = const SNode *;
  using ChildIteratorType = std::vector<const SNode *>::const_iterator;
  static NodeRef getEntryNode(Inverse<const SNode *> G) { return G.Graph; }
  static ChildIteratorType child_begin(NodeRef N) { return N->Preds.begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->Preds.end(); }
};
template <> struct GraphTraits<const SafeCFG *> : GraphTraits<const SNode *> {
  static NodeRef getEntryNode(const SafeCFG *G) { return G->Entry; }
};
} // namespace llvm

static unsigned bid(const CFGBlock *B) { return B->getBlockID(); }
static unsigned bid(const SNode *N) { return N->B->getBlockID(); }

template <typename Range> static std::string idsOf(Range &&R) {
  std::string S;
  for (auto N : R) S += (S.empty() ? "B" : " B") + std::to_string(bid(N));
  return S.empty() ? "-" : S;
}

// Everything generic: works for GraphT = const CFG * and GraphT = const SafeCFG *.
template <typename GraphT>
static void report(GraphT G, typename llvm::GraphTraits<GraphT>::NodeRef Exit) {
  using NodeRef = typename llvm::GraphTraits<GraphT>::NodeRef;
  llvm::outs() << "post_order      : " << idsOf(llvm::post_order(G)) << "\n";

  llvm::ReversePostOrderTraversal<GraphT> RPOT(G);
  llvm::outs() << "reverse post   : " << idsOf(llvm::make_range(RPOT.begin(), RPOT.end())) << "\n";
  llvm::outs() << "depth_first    : " << idsOf(llvm::depth_first(G)) << "\n";
  // Inverse<const CFG *> does not compile for a whole graph in 22.1.8 (its getEntryNode
  // takes the CFG, not the Inverse); start from the exit block instead.
  llvm::outs() << "inverse po     : " << idsOf(llvm::post_order(llvm::Inverse<NodeRef>(Exit))) << "\n";

  // Strongly connected components, sinks first. A component is a cycle if it has
  // more than one node, or one node with a self edge (hasCycle()).
  std::string S, Cyclic;
  unsigned N = 0, NCyc = 0;
  for (auto I = llvm::scc_begin(G); !I.isAtEnd(); ++I) {
    std::string One;
    for (NodeRef Nd : *I) One += (One.empty() ? "B" : " B") + std::to_string(bid(Nd));
    ++N;
    if (I.hasCycle()) { ++NCyc; One = "[" + One + "]"; Cyclic += (Cyclic.empty() ? "" : " ") + One; }
    else One = "{" + One + "}";
    S += (S.empty() ? "" : " ") + One;
  }
  llvm::outs() << "scc (sinks 1st): " << S << "\n";
  llvm::outs() << "cyclic sccs    : " << NCyc << " of " << N << (Cyclic.empty() ? "" : "  " + Cyclic) << "\n";

  // Cycle detection by DFS colouring: a successor that is still on the DFS stack is a back edge.
  std::vector<std::pair<unsigned, unsigned>> BackEdges;
  std::map<unsigned, int> Colour; // 1 = on stack, 2 = done
  struct Frame { NodeRef N; typename llvm::GraphTraits<GraphT>::ChildIteratorType It; };
  std::vector<Frame> Stack;
  NodeRef E = llvm::GraphTraits<GraphT>::getEntryNode(G);
  Colour[bid(E)] = 1;
  Stack.push_back({E, llvm::GraphTraits<GraphT>::child_begin(E)});
  while (!Stack.empty()) {
    Frame &F = Stack.back();
    if (F.It == llvm::GraphTraits<GraphT>::child_end(F.N)) { Colour[bid(F.N)] = 2; Stack.pop_back(); continue; }
    NodeRef C = *F.It;
    ++F.It;
    if (Colour[bid(C)] == 1) BackEdges.push_back({bid(F.N), bid(C)});
    else if (Colour[bid(C)] == 0) { Colour[bid(C)] = 1; Stack.push_back({C, llvm::GraphTraits<GraphT>::child_begin(C)}); }
  }
  std::string BE;
  for (auto &[U, V] : BackEdges) BE += (BE.empty() ? "" : " ") + ("B" + std::to_string(U) + "->B" + std::to_string(V));
  llvm::outs() << "dfs back edges : " << (BE.empty() ? "-" : BE) << "\n";
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncOpt.empty() && FD->getQualifiedNameAsString() != FuncOpt) return;
        AnalysisDeclContextManager Mgr(Ctx);
        CFG *G = Mgr.getContext(FD)->getCFG();
        if (!G) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << ": " << G->size() << " blocks\n";
        llvm::outs().flush();
        if (Raw) {
          report<const CFG *>(G, &G->getExit());
        } else {
          SafeCFG S(*G);
          report<const SafeCFG *>(&S, S.Exit);
        }
      });
}
