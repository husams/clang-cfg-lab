// p09_metrics -- the numbers people ask of a call graph, each one traversal (Part 9.5).
//
//   p09_metrics <file> [--top=N [--by=in|out|sites|height]] [--func=NAME] [--edges]
//                      [--with-root] [--sort=name|source|rpo] [--emit=text|dot|json]
//                      [--all-files] [-- <compile flags>]
//
// Text output, one record per line (names are quoted only when they contain a space; <root> is
// never a function and has no metric line):
//   metric <fn> in=<n> out=<m> sites=<k> height=<h|inf> scc=<id> [recursive] [dead] [leaf] [root]
//   edge <caller> -> <callee>     with --edges: one line per distinct caller -> callee pair
//   totals: functions=<n> edges=<m> sites=<k> sccs=<s> cyclic=<c> dead=<d> leaves=<l> roots=<r> height=<h|inf>
//                                 always the last line
//
// The definitions (everything counts the graph's own edges, so no pointer calls, no virtual targets):
//   in      distinct callers of the function (a self call counts: a recursive function is its own caller)
//   out     distinct callees (CallGraphNode::callees() with the repeats folded)
//   sites   call sites that name a callee = CallGraphNode::size(); sites >= out, equal unless the
//           function calls the same callee twice. The total is the edge count of p08_nodes' header
//   height  the longest chain of calls below the function, counted in the SCC condensation: 0 for a
//           leaf, 1 + the highest callee otherwise; inf for the members of a cyclic SCC (the chain
//           never ends); a function that only calls into a cycle counts that call as 1. The same
//           rule as `p10_summary --prop=depth`; the total is the highest height (inf if any is)
//   scc     the id p09_walk --sccs prints (scc_iterator order, callees first, trivial SCCs included)
//   recursive  member of a cyclic SCC          leaf   no callee at all (out == 0)
//   dead       not reachable from main (only judged when the file defines main)
//   root       nobody but the function itself calls it
//   edges   distinct caller -> callee pairs: the sum of out. sccs is the number of SCCs of functions
// --top=N keeps the N highest by --by (default in), descending, ties by name; --by alone sorts them all.
// --func=NAME prints that function's line (--edges: the edges into and out of it). With --top, --edges
// keeps the edges among the selected functions.
// --emit=dot|json export the graph with the metrics on the nodes (dot: an xlabel "in/out/sites", dead
// nodes dim; json: in, out, sites, height, dead, leaf, root).

#include "cglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p09_metrics options");
CGLAB_DEFINE_COMMON_FLAGS(Cat)
static llvm::cl::opt<unsigned> TopFlag("top", llvm::cl::cat(Cat),
                                       llvm::cl::desc("print only the N highest functions by --by"));
static llvm::cl::opt<std::string> ByFlag(
    "by", llvm::cl::cat(Cat),
    llvm::cl::desc("in|out|sites|height: the metric --top ranks by (default in)"));
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::cat(Cat),
                                           llvm::cl::desc("print only this function"));
static llvm::cl::opt<bool> EdgesFlag("edges", llvm::cl::cat(Cat),
                                     llvm::cl::desc("print edge lines (distinct caller -> callee pairs)"));

namespace {

[[noreturn]] void die(const std::string &Msg) {
  llvm::errs() << "p09_metrics: " << Msg << "\n";
  std::exit(2);
}

constexpr unsigned kInf = ~0u; // height of the members of a cyclic SCC (also the largest unsigned, so max works)

std::string heightText(unsigned H) { return H == kInf ? "inf" : std::to_string(H); }

struct Metric {
  unsigned In = 0, Out = 0, Sites = 0;
  unsigned Height = 0;
  unsigned Scc = 0;
  bool Recursive = false, Dead = false, Leaf = false, Root = false;
};

// Metrics of every node, indexed by node id (the entry of <root> stays empty).
std::vector<Metric> measure(const cglab::Graph &G) {
  std::vector<Metric> M(G.nodes().size());
  for (unsigned Id = 1; Id < M.size(); ++Id) {
    Metric &X = M[Id];
    X.In = G.callers(Id).size();
    X.Out = G.callees(Id).size();
    for (unsigned Ei : G.recordedOut(Id))
      if (!G.edge(Ei).Root) ++X.Sites;
    X.Scc = G.node(Id).Scc;
    X.Recursive = G.node(Id).Recursive;
    X.Leaf = X.Out == 0;
    std::vector<unsigned> C = G.callers(Id);
    C.erase(std::remove(C.begin(), C.end(), Id), C.end());
    X.Root = C.empty();
  }
  // height: the SCCs come callees first, so every callee is final when its caller is reached
  for (const cglab::Scc &S : G.sccs()) {
    if (S.Members.size() == 1 && S.Members[0] == G.root()) continue;
    if (S.Cyclic) {
      for (unsigned Id : S.Members) M[Id].Height = kInf;
      continue;
    }
    unsigned Id = S.Members[0];
    unsigned H = 0;
    bool First = true;
    for (unsigned C : G.callees(Id)) {
      unsigned Via = M[C].Height == kInf ? 1 : 1 + M[C].Height;
      if (First || Via > H) H = Via;
      First = false;
    }
    M[Id].Height = H;
  }
  // dead: judged from main, when there is one
  if (std::optional<unsigned> Main = G.find("main")) {
    std::vector<unsigned> Live = G.reachableFrom({*Main});
    std::set<unsigned> LiveSet(Live.begin(), Live.end());
    for (unsigned Id = 1; Id < M.size(); ++Id) M[Id].Dead = !LiveSet.count(Id);
  }
  return M;
}

unsigned metricByName(const Metric &X, const std::string &By) {
  if (By == "out") return X.Out;
  if (By == "sites") return X.Sites;
  if (By == "height") return X.Height;
  return X.In;
}

void printMetric(const cglab::Graph &G, unsigned Id, const Metric &X) {
  llvm::outs() << "metric " << G.name(Id) << " in=" << X.In << " out=" << X.Out << " sites=" << X.Sites
               << " height=" << heightText(X.Height) << " scc=" << X.Scc;
  if (X.Recursive) llvm::outs() << " recursive";
  if (X.Dead) llvm::outs() << " dead";
  if (X.Leaf) llvm::outs() << " leaf";
  if (X.Root) llvm::outs() << " root";
  llvm::outs() << "\n";
}

void report(ASTContext &Ctx, CallGraph &CG) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) std::exit(2);
  cglab::Graph G(CG, Ctx, cgGraphOptions());
  bool WithRoot = CgWithRootFlag;

  std::string By = ByFlag.empty() ? "in" : std::string(ByFlag);
  if (By != "in" && By != "out" && By != "sites" && By != "height")
    die("--by must be in, out, sites or height");
  std::optional<unsigned> One;
  if (!FuncFlag.empty()) {
    One = G.find(std::string(FuncFlag));
    if (!One || *One == G.root()) die("no node named '" + std::string(FuncFlag) + "' in " + G.file());
  }

  std::vector<Metric> M = measure(G);

  // the functions to show, in the order to show them
  std::vector<unsigned> Shown = G.order(Sort, false);
  if (One) Shown = {*One};
  else if (TopFlag.getNumOccurrences() || !ByFlag.empty()) {
    std::stable_sort(Shown.begin(), Shown.end(), [&](unsigned A, unsigned B) {
      unsigned X = metricByName(M[A], By), Y = metricByName(M[B], By);
      return X != Y ? X > Y : A < B; // ids follow name order
    });
    if (TopFlag.getNumOccurrences() && Shown.size() > TopFlag) Shown.resize(TopFlag);
  }
  std::set<unsigned> ShownSet(Shown.begin(), Shown.end());

  // the distinct caller -> callee pairs of the edge lines
  auto ShowEdge = [&](unsigned From, unsigned To) {
    if (One) return From == *One || To == *One;
    return ShownSet.count(From) && ShownSet.count(To);
  };

  // the nodes of a drawn graph: the shown functions, and with --func their neighbours
  auto Draw = [&](const cglab::Node &N) {
    return N.Id == G.root() || ShownSet.count(N.Id) ||
           (One && (ShowEdge(N.Id, *One) || ShowEdge(*One, N.Id)));
  };

  if (Emit == cglab::EmitKind::Dot) {
    cglab::DotOptions O;
    O.WithRoot = WithRoot;
    O.Sort = Sort;
    O.Keep = Draw;
    O.NodeHook = [&](const cglab::Node &N, cglab::DotAttrs &A) {
      if (N.Id == G.root()) return;
      const Metric &X = M[N.Id];
      A.XLabel = "in " + std::to_string(X.In) + " / out " + std::to_string(X.Out) + " / sites " +
                 std::to_string(X.Sites);
      if (X.Dead) A.addClass("dim");
    };
    cglab::writeDot(llvm::outs(), G, O);
    return;
  }
  if (Emit == cglab::EmitKind::Json) {
    cglab::JsonOptions O;
    O.WithRoot = WithRoot;
    O.Sort = Sort;
    O.Keep = Draw;
    O.NodeHook = [&](const cglab::Node &N, llvm::json::Object &J) {
      if (N.Id == G.root()) return;
      const Metric &X = M[N.Id];
      J["in"] = X.In;
      J["out"] = X.Out;
      J["sites"] = X.Sites;
      if (X.Height == kInf) J["height"] = "inf";
      else J["height"] = X.Height;
      J["dead"] = X.Dead;
      J["leaf"] = X.Leaf;
      J["root"] = X.Root;
    };
    cglab::writeJson(llvm::outs(), G, O);
    return;
  }

  for (unsigned Id : Shown) printMetric(G, Id, M[Id]);
  if (EdgesFlag)
    for (unsigned Id : G.order(Sort, false)) {
      std::set<unsigned> Done;
      for (unsigned Ei : G.outEdges(Id, Sort, false)) {
        const cglab::Edge &E = G.edge(Ei);
        if (!Done.insert(E.To).second || !ShowEdge(E.From, E.To)) continue;
        cglab::printEdgeLine(llvm::outs(), G, E, false, false);
      }
    }

  // the totals are about the whole graph, whatever is shown above
  unsigned Edges = 0, Sites = 0, Dead = 0, Leaves = 0, Roots = 0, Cyclic = 0, Sccs = 0, Height = 0;
  for (unsigned Id = 1; Id < M.size(); ++Id) {
    Edges += M[Id].Out;
    Sites += M[Id].Sites;
    Dead += M[Id].Dead;
    Leaves += M[Id].Leaf;
    Roots += M[Id].Root;
    Height = std::max(Height, M[Id].Height);
  }
  for (const cglab::Scc &S : G.sccs()) {
    if (S.Members.size() == 1 && S.Members[0] == G.root()) continue;
    ++Sccs;
    Cyclic += S.Cyclic;
  }
  llvm::outs() << "totals: functions=" << G.numNodes() << " edges=" << Edges << " sites=" << Sites
               << " sccs=" << Sccs << " cyclic=" << Cyclic << " dead=" << Dead << " leaves=" << Leaves
               << " roots=" << Roots << " height=" << heightText(Height) << "\n";
}

} // namespace

int main(int argc, const char **argv) { return cglab::runPerTU(argc, argv, Cat, report); }
