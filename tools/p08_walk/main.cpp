// p08_walk -- traversals of a clang::CallGraph through llvm::GraphTraits (Parts 8.4, 8.5).
//
//   p08_walk <file> [--order=po|rpo|scc] [--sccs [--cyclic]] [--from=NAME]
//                   [--dead [--roots=main|external|all]] [--callers=NAME [--transitive]]
//                   [--edges [--sites]]
//                   [--with-root] [--sort=name|source|rpo] [--emit=text|dot|json]
//                   [--all-files] [-- <compile flags>]
//
// Text output, one record per line ("-" stands for an empty list; <root> is left out of the
// lists unless --with-root; names are quoted only when they contain a space):
//   order po|rpo: <names...>      llvm::post_order / ReversePostOrderTraversal from --from
//                                 (default <root>), in traversal order -- not sorted
//   scc <id> [cyclic self|mutual]: <members...>
//                                 scc_iterator order (callees first); <id> is the position in
//                                 that order, trivial SCCs included; --cyclic lists only the
//                                 cyclic ones; --order=scc is the same as --sccs
//   reach <from>: <names...>      llvm::depth_first from --from (only printed with --from)
//   dead: <names...>              nodes not reachable from the roots
//   callers <name>: <names...>    direct callers, from a reverse map built once
//   callers* <name>: <names...>   transitive callers (with --transitive)
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
// labels, --from marks the entry node, --callers keeps only the queried node and its callers.

#include "cglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p08_walk options");
CGLAB_DEFINE_COMMON_FLAGS(Cat)
static llvm::cl::opt<std::string> OrderFlag("order", llvm::cl::cat(Cat),
                                            llvm::cl::desc("po|rpo|scc: print a traversal order"));
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

namespace {

[[noreturn]] void die(const std::string &Msg) {
  llvm::errs() << "p08_walk: " << Msg << "\n";
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

void report(ASTContext &Ctx, CallGraph &CG) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) std::exit(2);
  cglab::Graph G(CG, Ctx, cgGraphOptions());
  bool WithRoot = CgWithRootFlag;

  if (!OrderFlag.empty() && OrderFlag != "po" && OrderFlag != "rpo" && OrderFlag != "scc")
    die("--order must be po, rpo or scc");
  unsigned From = FromFlag.empty() ? G.root() : needNode(G, FromFlag);
  std::optional<unsigned> Query;
  if (!CallersFlag.empty()) Query = needNode(G, CallersFlag);

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
    if (Emit == cglab::EmitKind::Dot) {
      cglab::DotOptions O;
      O.WithRoot = WithRoot;
      O.Sort = Sort;
      O.SccClusters = true;
      O.OrderLabels = !OrderFlag.empty();
      O.Keep = Show;
      O.NodeHook = [&](const cglab::Node &N, cglab::DotAttrs &A) {
        if (WantDead && Dead.count(N.Id)) A.addClass("dim");
        if (!FromFlag.empty()) {
          A.dropClass("entry");
          if (N.Id == From) A.addClass("entry");
        }
        if (Query && N.Id == *Query) A.addClass("hl");
      };
      cglab::writeDot(llvm::outs(), G, O);
    } else {
      cglab::JsonOptions O;
      O.WithRoot = WithRoot;
      O.Sort = Sort;
      O.Keep = Show;
      O.NodeHook = [&](const cglab::Node &N, llvm::json::Object &J) {
        if (WantDead) J["dead"] = Dead.count(N.Id) != 0;
      };
      cglab::writeJson(llvm::outs(), G, O);
    }
    return;
  }

  bool Any = !OrderFlag.empty() || SccsFlag || !FromFlag.empty() || DeadFlag || Query || EdgesFlag;
  bool PrintSccs = SccsFlag || OrderFlag == "scc" || !Any;

  if (OrderFlag == "po" || OrderFlag == "rpo") {
    bool Po = OrderFlag == "po";
    std::vector<unsigned> Seq;
    if (From == G.root()) Seq = Po ? G.postOrder() : G.reversePostOrder();
    else Seq = Po ? G.postOrderFrom(From) : G.reversePostOrderFrom(From);
    llvm::outs() << "order " << OrderFlag << ": " << listOf(G, Seq, WithRoot) << "\n";
  }
  if (PrintSccs) cglab::printSccs(llvm::outs(), G, G.sccs(), CyclicFlag, WithRoot);
  if (!FromFlag.empty()) {
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
  if (EdgesFlag)
    for (unsigned Id : G.order(Sort, WithRoot)) {
      if (!Show(G.node(Id))) continue;
      for (unsigned Ei : G.outEdges(Id, Sort, WithRoot))
        if (Show(G.node(G.edge(Ei).To))) cglab::printEdgeLine(llvm::outs(), G, G.edge(Ei), SitesFlag, false);
    }
}

} // namespace

int main(int argc, const char **argv) { return cglab::runPerTU(argc, argv, Cat, report); }
