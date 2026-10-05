// p09_walk -- traversals of a clang::CallGraph through llvm::GraphTraits (Parts 9.1-9.6).
//
//   p09_walk <file> [--order=po|rpo|dfs|bfs|scc [--reverse]] [--sccs [--cyclic]] [--from=NAME]
//                   [--dead [--roots=main|external|all]] [--callers=NAME [--transitive]]
//                   [--path=A,B [--all-paths [--max-paths=N] [--max-len=N]]] [--cycle=NAME]
//                   [--edges [--sites]]
//                   [--with-root] [--sort=name|source|rpo] [--emit=text|dot|json]
//                   [--all-files] [-- <compile flags>]
//
// Text output, one record per line ("-" stands for an empty list; <root> is left out of the
// lists unless --with-root; names are quoted only when they contain a space):
//   order po|rpo: <names...>      llvm::post_order / ReversePostOrderTraversal from --from
//                                 (default <root>), in traversal order -- not sorted
//   order dfs <from>: <names...>  llvm::depth_first from --from (default <root>), preorder
//   order bfs <from>: <name>@<level> ...
//                                 llvm::breadth_first; <level> is bf_iterator::getLevel(), the
//                                 call depth from <from> (from <root> every function is 1)
//   order po|rpo reverse: <names...>
//   order dfs|bfs reverse <from>: ...
//                                 the same iterators over the reverse graph (--reverse): the
//                                 edges point callee -> caller, <root> still reaches every
//                                 function, and a node lists its callers by name. With --from
//                                 the walk starts there: its transitive callers. po is then
//                                 "callers first" (a caller precedes everything it calls)
//   scc <id> [cyclic self|mutual]: <members...>
//                                 scc_iterator order (callees first); <id> is the position in
//                                 that order, trivial SCCs included; --cyclic lists only the
//                                 cyclic ones; --order=scc is the same as --sccs
//   reach <from>: <names...>      llvm::depth_first from --from, sorted (only printed with --from,
//                                 not with --order=dfs|bfs or --reverse, which print their own list)
//   dead: <names...>              nodes not reachable from the roots
//   callers <name>: <names...>    direct callers, from a reverse map built once
//   callers* <name>: <names...>   transitive callers (with --transitive)
//   path <A> -> <x> -> <B> (<n> calls)
//   path <A> -> <B>: none         --path: the shortest call chain from A to B, by breadth-first
//                                 search (ties: the callee recorded first wins); with --all-paths
//                                 one line per simple path of at most --max-len calls, shortest
//                                 first (at most --max-paths of them), then
//   paths: <n> shown, <m> found, limit <max-len>
//   cycle <F> -> <x> -> <F>       --cycle: the shortest cycle through F, searched inside F's SCC
//   cycle <F>: none               (the cycle misc-no-recursion prints is "some" cycle, not the
//                                 shortest; a self call is "cycle f -> f")
//   edge <caller> -> <callee> [@L<line> <ExprClass>]
//                                 with --edges, after the lines above: every edge, same
//                                 grammar as p08_nodes (--sites adds the call site); with
//                                 --callers only the edges among the queried function and its
//                                 callers
// The roots of --dead: --roots=main (just main), external (every definition with external
// linkage), all (every function nobody else calls). Without --roots: --from if given, else
// main if it exists, else all.
// With nothing selected, --sccs is the default. --emit=dot|json export the whole graph with
// scc/po/rpo numbers; cyclic SCCs become clusters, dead nodes are dim, --order adds po/rpo
// labels (bfs: the level, dfs: the index, --reverse: the position in the reverse order),
// --from marks the entry node, --callers keeps only the queried node and its callers, and
// --path/--cycle mark the shortest chain hl.

#include "cglab.h"

#include "llvm/ADT/BreadthFirstIterator.h"

#include <deque>

using namespace clang;

static llvm::cl::OptionCategory Cat("p09_walk options");
CGLAB_DEFINE_COMMON_FLAGS(Cat)
static llvm::cl::opt<std::string> OrderFlag("order", llvm::cl::cat(Cat),
                                            llvm::cl::desc("po|rpo|dfs|bfs|scc: print a traversal order"));
static llvm::cl::opt<bool> SccsFlag("sccs", llvm::cl::cat(Cat), llvm::cl::desc("print the SCCs"));
static llvm::cl::opt<bool> CyclicFlag("cyclic", llvm::cl::cat(Cat),
                                      llvm::cl::desc("with --sccs: only the cyclic SCCs"));
static llvm::cl::opt<std::string> FromFlag("from", llvm::cl::cat(Cat),
                                           llvm::cl::desc("start node of reach/orders (default <root>)"));
static llvm::cl::opt<std::string> RootsFlag("roots", llvm::cl::cat(Cat),
                                            llvm::cl::desc("roots for --dead: main|external|all"));
static llvm::cl::opt<bool> DeadFlag("dead", llvm::cl::cat(Cat),
                                    llvm::cl::desc("print the functions not reachable from the roots"));
static llvm::cl::opt<std::string> CallersFlag("callers", llvm::cl::cat(Cat),
                                              llvm::cl::desc("print the callers of this function"));
static llvm::cl::opt<bool> EdgesFlag("edges", llvm::cl::cat(Cat), llvm::cl::desc("print edge lines"));
static llvm::cl::opt<bool> SitesFlag("sites", llvm::cl::cat(Cat),
                                     llvm::cl::desc("with --edges: add @L<line> <ExprClass>"));
static llvm::cl::opt<bool> TransitiveFlag("transitive", llvm::cl::cat(Cat),
                                          llvm::cl::desc("with --callers: also the transitive callers"));
static llvm::cl::opt<bool> ReverseFlag("reverse", llvm::cl::cat(Cat),
                                       llvm::cl::desc("run --order over the reverse graph (callers first)"));
static llvm::cl::opt<std::string> PathFlag("path", llvm::cl::cat(Cat),
                                           llvm::cl::desc("A,B: the shortest call chain from A to B"));
static llvm::cl::opt<std::string> CycleFlag("cycle", llvm::cl::cat(Cat),
                                            llvm::cl::desc("F: the shortest cycle through F"));
static llvm::cl::opt<bool> AllPathsFlag(
    "all-paths", llvm::cl::cat(Cat),
    llvm::cl::desc("with --path: every simple path of at most --max-len calls"));
static llvm::cl::opt<unsigned> MaxPathsFlag(
    "max-paths", llvm::cl::init(5), llvm::cl::cat(Cat),
    llvm::cl::desc("with --all-paths: print at most N paths (default 5)"));
static llvm::cl::opt<unsigned> MaxLenFlag(
    "max-len", llvm::cl::init(8), llvm::cl::cat(Cat),
    llvm::cl::desc("with --all-paths: paths of at most N calls (default 8)"));

namespace {

[[noreturn]] void die(const std::string &Msg) {
  llvm::errs() << "p09_walk: " << Msg << "\n";
  std::exit(2);
}

unsigned needNode(const cglab::Graph &G, const std::string &Name) {
  std::optional<unsigned> Id = G.find(Name);
  if (!Id) die("no node named '" + Name + "' in " + G.file());
  return *Id;
}

std::string listOf(const cglab::Graph &G, std::vector<unsigned> Ids, bool WithRoot) {
  if (!WithRoot) Ids.erase(std::remove(Ids.begin(), Ids.end(), G.root()), Ids.end());
  return Ids.empty() ? "-" : G.join(Ids);
}

// <name>@<level> for a breadth-first walk; the root only with WithRoot.
std::string levelListOf(const cglab::Graph &G, const std::vector<std::pair<unsigned, unsigned>> &Seq,
                        bool WithRoot) {
  std::string S;
  for (auto [Id, Level] : Seq) {
    if (Id == G.root() && !WithRoot) continue;
    S += (S.empty() ? "" : " ") + G.name(Id) + "@" + std::to_string(Level);
  }
  return S.empty() ? "-" : S;
}

// The roots --dead measures reachability from.
std::vector<unsigned> deadRoots(const cglab::Graph &G) {
  std::vector<unsigned> R;
  std::string Mode = RootsFlag;
  if (Mode.empty()) Mode = !FromFlag.empty() ? "from" : (G.find("main") ? "main" : "all");
  if (Mode == "from") {
    R.push_back(needNode(G, FromFlag));
  } else if (Mode == "main") {
    R.push_back(needNode(G, "main"));
  } else if (Mode == "external") {
    for (unsigned Id : G.order())
      if (G.node(Id).External) R.push_back(Id);
  } else if (Mode == "all") {
    for (unsigned Id : G.order()) {
      std::vector<unsigned> C = G.callers(Id);
      C.erase(std::remove(C.begin(), C.end(), Id), C.end());
      if (C.empty()) R.push_back(Id); // nobody else calls it
    }
  } else {
    die("--roots must be main, external or all");
  }
  return R;
}

// ---- the generic iterators, forward and over the reverse graph ----------------------------

// The reverse call graph as an AdjGraph (it has its own llvm::GraphTraits, so the same iterators
// run on it): node i keeps the id i; an edge goes callee -> caller, one per distinct caller, by
// name. Node 0 is a synthetic <root> that, like the real one, reaches every function.
cglab::AdjGraph reverseGraph(const cglab::Graph &G) {
  cglab::AdjGraph AG;
  AG.Nodes.resize(G.nodes().size());
  for (unsigned I = 0; I < AG.Nodes.size(); ++I) AG.Nodes[I].Id = I;
  for (unsigned I = 1; I < AG.Nodes.size(); ++I) {
    AG.Nodes[0].Kids.push_back(&AG.Nodes[I]);
    for (unsigned C : G.callers(I)) AG.Nodes[I].Kids.push_back(&AG.Nodes[C]);
  }
  return AG;
}

struct Walk {
  std::vector<unsigned> Seq;                           // node ids in traversal order
  std::vector<std::pair<unsigned, unsigned>> Levels;   // bfs only: (id, level)
};

Walk walkForward(const cglab::Graph &G, const std::string &Kind, unsigned From) {
  Walk W;
  CallGraphNode *Start = G.node(From).CGN;
  if (Kind == "po") {
    W.Seq = From == G.root() ? G.postOrder() : G.postOrderFrom(From);
  } else if (Kind == "rpo") {
    W.Seq = From == G.root() ? G.reversePostOrder() : G.reversePostOrderFrom(From);
  } else if (Kind == "dfs") {
    for (CallGraphNode *N : llvm::depth_first(Start))
      if (auto I = G.find(N)) W.Seq.push_back(*I);
  } else { // bfs
    for (auto It = llvm::bf_begin(Start), End = llvm::bf_end(Start); It != End; ++It)
      if (auto I = G.find(*It)) {
        W.Seq.push_back(*I);
        W.Levels.push_back({*I, It.getLevel()});
      }
  }
  return W;
}

Walk walkReverse(const cglab::Graph &G, cglab::AdjGraph &AG, const std::string &Kind, unsigned From) {
  Walk W;
  cglab::AdjGraph::N *Start = &AG.Nodes[From];
  if (Kind == "po") {
    for (cglab::AdjGraph::N *N : llvm::post_order(Start)) W.Seq.push_back(N->Id);
  } else if (Kind == "rpo") {
    llvm::ReversePostOrderTraversal<cglab::AdjGraph::N *> RPOT(Start);
    for (cglab::AdjGraph::N *N : RPOT) W.Seq.push_back(N->Id);
  } else if (Kind == "dfs") {
    for (cglab::AdjGraph::N *N : llvm::depth_first(Start)) W.Seq.push_back(N->Id);
  } else { // bfs
    for (auto It = llvm::bf_begin(Start), End = llvm::bf_end(Start); It != End; ++It) {
      W.Seq.push_back((*It)->Id);
      W.Levels.push_back({(*It)->Id, It.getLevel()});
    }
  }
  return W;
}

// ---- witness chains --------------------------------------------------------------------------

using Chain = std::vector<unsigned>;

// Distinct callees of every node in the order the library records them (the order its iterators
// follow); the root's fan-out is left out, so no chain runs through <root>.
std::vector<std::vector<unsigned>> calleeLists(const cglab::Graph &G) {
  std::vector<std::vector<unsigned>> Adj(G.nodes().size());
  for (unsigned Id = 1; Id < Adj.size(); ++Id) {
    std::set<unsigned> Seen;
    for (unsigned Ei : G.recordedOut(Id)) {
      const cglab::Edge &E = G.edge(Ei);
      if (!E.Root && Seen.insert(E.To).second) Adj[Id].push_back(E.To);
    }
  }
  return Adj;
}

// Breadth-first search for the shortest chain From -> ... -> To that only visits nodes Allowed
// accepts. With From == To it is the shortest cycle through From (at least one call).
std::optional<Chain> shortestChain(const std::vector<std::vector<unsigned>> &Adj, unsigned From,
                                   unsigned To, const std::function<bool(unsigned)> &Allowed) {
  std::vector<unsigned> Prev(Adj.size(), ~0u);
  std::vector<char> Seen(Adj.size(), 0);
  std::deque<unsigned> Queue{From};
  Seen[From] = From != To; // a cycle query must come back to From, so From is not "seen" yet
  while (!Queue.empty()) {
    unsigned U = Queue.front();
    Queue.pop_front();
    for (unsigned V : Adj[U]) {
      if (!Allowed(V)) continue;
      if (V == To) {
        Prev[V] = U;
        Chain C{To};
        for (unsigned Cur = Prev[To];; Cur = Prev[Cur]) {
          C.push_back(Cur);
          if (Cur == From) break;
        }
        std::reverse(C.begin(), C.end());
        return C;
      }
      if (Seen[V]) continue;
      Seen[V] = 1;
      Prev[V] = U;
      Queue.push_back(V);
    }
  }
  return std::nullopt;
}

// Depth-first enumeration of the simple paths From -> To of at most MaxLen calls. Found counts
// all of them; Best keeps the Keep shortest (ties: the order of discovery).
struct PathSearch {
  const std::vector<std::vector<unsigned>> &Adj;
  unsigned To, MaxLen;
  size_t Keep;
  std::set<unsigned> CanReach; // nodes with a path to To: the rest is never entered
  size_t Found = 0;
  std::vector<std::pair<size_t, Chain>> Best; // (discovery index, chain), sorted by (length, index)
  Chain Cur;
  std::vector<char> On;

  PathSearch(const std::vector<std::vector<unsigned>> &Adj, const cglab::Graph &G, unsigned To,
             unsigned MaxLen, size_t Keep)
      : Adj(Adj), To(To), MaxLen(MaxLen), Keep(Keep), On(Adj.size(), 0) {
    for (unsigned C : G.callers(To, true)) CanReach.insert(C);
    CanReach.insert(To);
  }

  void run(unsigned From) {
    Cur = {From};
    On[From] = 1;
    visit(From);
  }

  void visit(unsigned U) {
    if (U == To) {
      record();
      return;
    }
    if (Cur.size() - 1 >= MaxLen) return;
    for (unsigned V : Adj[U]) {
      if (On[V] || !CanReach.count(V)) continue;
      On[V] = 1;
      Cur.push_back(V);
      visit(V);
      Cur.pop_back();
      On[V] = 0;
    }
  }

  void record() {
    size_t Index = Found++;
    auto Less = [](const std::pair<size_t, Chain> &A, const std::pair<size_t, Chain> &B) {
      return std::make_pair(A.second.size(), A.first) < std::make_pair(B.second.size(), B.first);
    };
    std::pair<size_t, Chain> New{Index, Cur};
    Best.insert(std::upper_bound(Best.begin(), Best.end(), New, Less), New);
    if (Best.size() > Keep) Best.pop_back();
  }
};

std::string chainText(const cglab::Graph &G, const Chain &C) {
  std::string S;
  for (unsigned Id : C) S += (S.empty() ? "" : " -> ") + G.name(Id);
  return S;
}

// "A,B" -> A, B. The comma that splits is the first one outside <> and ().
std::pair<std::string, std::string> splitPair(const std::string &S) {
  int Depth = 0;
  for (size_t I = 0; I < S.size(); ++I) {
    char C = S[I];
    if (C == '<' || C == '(') ++Depth;
    else if (C == '>' || C == ')') --Depth;
    else if (C == ',' && Depth == 0) return {S.substr(0, I), S.substr(I + 1)};
  }
  die("--path needs two function names: --path=A,B");
}

void report(ASTContext &Ctx, CallGraph &CG) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) std::exit(2);
  cglab::Graph G(CG, Ctx, cgGraphOptions());
  bool WithRoot = CgWithRootFlag;

  if (!OrderFlag.empty() && OrderFlag != "po" && OrderFlag != "rpo" && OrderFlag != "scc" &&
      OrderFlag != "dfs" && OrderFlag != "bfs")
    die("--order must be po, rpo, dfs, bfs or scc");
  bool Reverse = ReverseFlag;
  if (Reverse && (OrderFlag.empty() || OrderFlag == "scc"))
    die("--reverse needs --order=po, rpo, dfs or bfs");
  if (AllPathsFlag && PathFlag.empty()) die("--all-paths needs --path=A,B");
  unsigned From = FromFlag.empty() ? G.root() : needNode(G, FromFlag);
  std::optional<unsigned> Query;
  if (!CallersFlag.empty()) Query = needNode(G, CallersFlag);

  // the generic iterators, over the call graph or over the reverse graph
  bool WantWalk = OrderFlag == "po" || OrderFlag == "rpo" || OrderFlag == "dfs" || OrderFlag == "bfs";
  Walk W;
  if (WantWalk) {
    if (Reverse) {
      cglab::AdjGraph AG = reverseGraph(G);
      W = walkReverse(G, AG, OrderFlag, From);
    } else {
      W = walkForward(G, OrderFlag, From);
    }
  }

  // witness chains
  std::optional<Chain> PathChain, CycleChain;
  std::optional<unsigned> PathA, PathB, CycleNode;
  if (!PathFlag.empty()) {
    auto [A, B] = splitPair(PathFlag);
    PathA = needNode(G, A);
    PathB = needNode(G, B);
    if (*PathA == *PathB)
      die("--path needs two different functions; --cycle=" + A + " is the question for recursion");
  }
  if (!CycleFlag.empty()) CycleNode = needNode(G, CycleFlag);
  std::vector<std::vector<unsigned>> Adj;
  if (PathA || CycleNode) Adj = calleeLists(G);
  if (PathA) PathChain = shortestChain(Adj, *PathA, *PathB, [](unsigned) { return true; });
  if (CycleNode) {
    unsigned Scc = G.node(*CycleNode).Scc;
    CycleChain = shortestChain(Adj, *CycleNode, *CycleNode,
                               [&](unsigned V) { return G.node(V).Scc == Scc; });
  }

  // reachability: computed whenever something asks for it
  bool WantDead = DeadFlag || !RootsFlag.empty() || !FromFlag.empty();
  std::set<unsigned> Dead;
  if (WantDead) {
    std::vector<unsigned> Reached = G.reachableFrom(deadRoots(G));
    std::set<unsigned> Live(Reached.begin(), Reached.end());
    for (unsigned Id : G.order())
      if (!Live.count(Id)) Dead.insert(Id);
  }

  std::set<unsigned> Keep;
  if (Query) {
    Keep.insert(*Query);
    for (unsigned C : G.callers(*Query, TransitiveFlag)) Keep.insert(C);
  }
  auto Show = [&](const cglab::Node &N) { return !Query || Keep.count(N.Id); };

  if (Emit != cglab::EmitKind::Text) {
    // what the order adds to a node: bfs the level, dfs the index, --reverse the position
    std::map<unsigned, std::string> OrderLabel;
    if (WantWalk && (Reverse || OrderFlag == "bfs" || OrderFlag == "dfs")) {
      std::map<unsigned, unsigned> Level(W.Levels.begin(), W.Levels.end());
      unsigned Pos = 0;
      for (unsigned Id : W.Seq) {
        OrderLabel[Id] = OrderFlag == "bfs" ? "bfs " + std::to_string(Level[Id])
                                            : std::string(Reverse ? "rev " : "") + OrderFlag + " " +
                                                  std::to_string(Pos);
        ++Pos;
      }
    }
    std::set<unsigned> WitnessNodes;
    std::set<std::pair<unsigned, unsigned>> WitnessEdges;
    for (const std::optional<Chain> *C : {&PathChain, &CycleChain})
      if (*C)
        for (size_t I = 0; I < (*C)->size(); ++I) {
          WitnessNodes.insert((**C)[I]);
          if (I) WitnessEdges.insert({(**C)[I - 1], (**C)[I]});
        }
    if (Emit == cglab::EmitKind::Dot) {
      cglab::DotOptions O;
      O.WithRoot = WithRoot;
      O.Sort = Sort;
      O.SccClusters = true;
      O.OrderLabels = WantWalk && OrderLabel.empty();
      O.Keep = Show;
      O.NodeHook = [&](const cglab::Node &N, cglab::DotAttrs &A) {
        if (WantDead && Dead.count(N.Id)) A.addClass("dim");
        if (!FromFlag.empty()) {
          A.dropClass("entry");
          if (N.Id == From) A.addClass("entry");
        }
        if (Query && N.Id == *Query) A.addClass("hl");
        if (WitnessNodes.count(N.Id)) A.addClass("hl");
        if (auto It = OrderLabel.find(N.Id); It != OrderLabel.end()) A.XLabel = It->second;
      };
      O.EdgeHook = [&](const cglab::Edge &E, cglab::DotAttrs &A) {
        if (WitnessEdges.count({E.From, E.To})) A.addClass("hl");
      };
      cglab::writeDot(llvm::outs(), G, O);
    } else {
      cglab::JsonOptions O;
      O.WithRoot = WithRoot;
      O.Sort = Sort;
      O.Keep = Show;
      std::map<unsigned, unsigned> Level(W.Levels.begin(), W.Levels.end());
      O.NodeHook = [&](const cglab::Node &N, llvm::json::Object &J) {
        if (WantDead) J["dead"] = Dead.count(N.Id) != 0;
        if (OrderFlag == "bfs" && Level.count(N.Id)) J["level"] = Level[N.Id];
        if (PathA || CycleNode) J["witness"] = WitnessNodes.count(N.Id) != 0;
      };
      cglab::writeJson(llvm::outs(), G, O);
    }
    return;
  }

  bool Any = !OrderFlag.empty() || SccsFlag || !FromFlag.empty() || DeadFlag || Query || EdgesFlag ||
             !PathFlag.empty() || !CycleFlag.empty();
  bool PrintSccs = SccsFlag || OrderFlag == "scc" || !Any;

  if (WantWalk) {
    llvm::outs() << "order " << OrderFlag;
    if (Reverse) llvm::outs() << " reverse";
    if (OrderFlag == "dfs" || OrderFlag == "bfs") llvm::outs() << " " << G.name(From);
    llvm::outs() << ": "
                 << (OrderFlag == "bfs" ? levelListOf(G, W.Levels, WithRoot) : listOf(G, W.Seq, WithRoot))
                 << "\n";
  }
  if (PrintSccs) cglab::printSccs(llvm::outs(), G, G.sccs(), CyclicFlag, WithRoot);
  if (!FromFlag.empty() && !Reverse && OrderFlag != "dfs" && OrderFlag != "bfs") {
    std::vector<unsigned> R = G.reachableFrom({From});
    G.sortBy(R, Sort);
    llvm::outs() << "reach " << G.name(From) << ": " << listOf(G, R, WithRoot) << "\n";
  }
  if (DeadFlag) {
    std::vector<unsigned> D(Dead.begin(), Dead.end());
    G.sortBy(D, Sort);
    llvm::outs() << "dead: " << listOf(G, D, false) << "\n";
  }
  if (Query) {
    std::vector<unsigned> C = G.callers(*Query);
    G.sortBy(C, Sort);
    llvm::outs() << "callers " << G.name(*Query) << ": " << listOf(G, C, false) << "\n";
    if (TransitiveFlag) {
      std::vector<unsigned> T = G.callers(*Query, true);
      G.sortBy(T, Sort);
      llvm::outs() << "callers* " << G.name(*Query) << ": " << listOf(G, T, false) << "\n";
    }
  }
  if (PathA) {
    if (AllPathsFlag) {
      PathSearch S(Adj, G, *PathB, MaxLenFlag, MaxPathsFlag);
      S.run(*PathA);
      if (!S.Found) llvm::outs() << "path " << G.name(*PathA) << " -> " << G.name(*PathB) << ": none\n";
      for (auto &[Index, C] : S.Best)
        llvm::outs() << "path " << chainText(G, C) << " (" << C.size() - 1 << " calls)\n";
      llvm::outs() << "paths: " << S.Best.size() << " shown, " << S.Found << " found, limit "
                   << MaxLenFlag << "\n";
    } else if (PathChain) {
      llvm::outs() << "path " << chainText(G, *PathChain) << " (" << PathChain->size() - 1 << " calls)\n";
    } else {
      llvm::outs() << "path " << G.name(*PathA) << " -> " << G.name(*PathB) << ": none\n";
    }
  }
  if (CycleNode) {
    if (CycleChain) llvm::outs() << "cycle " << chainText(G, *CycleChain) << "\n";
    else llvm::outs() << "cycle " << G.name(*CycleNode) << ": none\n";
  }
  if (EdgesFlag)
    for (unsigned Id : G.order(Sort, WithRoot)) {
      if (!Show(G.node(Id))) continue;
      for (unsigned Ei : G.outEdges(Id, Sort, WithRoot))
        if (Show(G.node(G.edge(Ei).To))) cglab::printEdgeLine(llvm::outs(), G, G.edge(Ei), SitesFlag, false);
    }
}

} // namespace

int main(int argc, const char **argv) { return cglab::runPerTU(argc, argv, Cat, report); }
