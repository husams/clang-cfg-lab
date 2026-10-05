// p08_check -- a cross-TU recursion and sink checker over the merged call graph (Part 8.9).
//
//   p08_check FILE... [--sink=NAME]... [--no-recursion] [--no-sink] [--trace] [-- <flags>]
//
// Merges the call graphs of every file by USR (../p08_xtu/Merge.h), then reports, like
// clang-tidy's misc-no-recursion and the Part 8.6 summary would on a whole program:
//
//   diag <file>:<line>: recursion: <a> -> <b> -> <a>
//        one cycle per cyclic SCC, found by a breadth-first search from the SCC's first
//        function (by name) back to itself; <file>:<line> is the call site of its first edge
//   diag <file>:<line>: reaches-sink: <a> -> ... -> <sink>
//        one chain per function that can reach a sink, the shortest by call count;
//        <file>:<line> is the first call of the chain. A sink is a [[noreturn]] function, or
//        with --sink=NAME the function(s) of that name instead (repeatable, comma separated)
//   trace <kind>: <caller> -> <callee> @<file>:<line>     one per edge of each chain (--trace)
//   summary: <n> functions, <r> recursive, <s> reach a sink
//
// <n> counts defined functions, <r> the functions on a cycle, <s> the functions with a chain
// (the sinks themselves excluded). Paths are relative to the current directory when they are
// inside it. Exit status: 0 clean, 1 diagnostics, 2 the front end failed or bad options.
//
// The bottom-up Summary.h of 8.6 works on one clang::CallGraph, which holds one AST; a
// merged graph has no single ASTContext, so the chains here are found on the merged graph
// directly with the same rule Summary.h's sinkPath uses (breadth-first, in call-site order).

#include "../p08_xtu/Merge.h"

#include <deque>

using namespace clang;

static llvm::cl::OptionCategory Cat("p08_check options");
static llvm::cl::list<std::string> OptSink("sink", llvm::cl::CommaSeparated, llvm::cl::cat(Cat),
                                           llvm::cl::desc("the sink function (default: [[noreturn]] functions)"));
static llvm::cl::opt<bool> OptNoRecursion("no-recursion", llvm::cl::cat(Cat), llvm::cl::desc("skip the recursion check"));
static llvm::cl::opt<bool> OptNoSink("no-sink", llvm::cl::cat(Cat), llvm::cl::desc("skip the sink check"));
static llvm::cl::opt<bool> OptTrace("trace", llvm::cl::cat(Cat), llvm::cl::desc("print every edge of each chain"));

namespace {

using p08xtu::Edge;
using p08xtu::Merged;

struct Diag {
  std::string Path; // as displayed
  unsigned Line;
  std::string Kind, Text;
  std::vector<unsigned> Chain; // edge indices
};

bool isSink(const p08xtu::Node &N) {
  if (OptSink.empty()) return N.Noreturn;
  for (const std::string &S : OptSink)
    if (N.Name == S || N.Name.rfind(S + "@", 0) == 0) return true;
  return false;
}

// The shortest chain of edges from From to the first node Goal accepts, empty when there is
// none. Breadth-first, callees in name order then call-site order; Allowed, when given,
// restricts the nodes the search may enter. Goal is tested before the visited set, so a search
// for a cycle through From finds From again.
std::vector<unsigned> shortestChain(const Merged &M, unsigned From, const std::function<bool(unsigned)> &Goal,
                                    const std::function<bool(unsigned)> &Allowed = nullptr) {
  std::map<unsigned, unsigned> Via; // node -> the edge that first reached it
  std::set<unsigned> Seen{From};
  std::deque<unsigned> Q{From};
  while (!Q.empty()) {
    unsigned V = Q.front();
    Q.pop_front();
    for (unsigned Ei : M.outEdges(V)) {
      unsigned W = M.edges()[Ei].To;
      if (Allowed && !Allowed(W)) continue;
      if (Goal(W)) {
        std::vector<unsigned> Chain{Ei};
        for (unsigned X = V; X != From; X = M.edges()[Via[X]].From) Chain.insert(Chain.begin(), Via[X]);
        return Chain;
      }
      if (!Seen.insert(W).second) continue;
      Via[W] = Ei;
      Q.push_back(W);
    }
  }
  return {};
}

std::string chainText(const Merged &M, const std::vector<unsigned> &Chain) {
  std::string S = M.node(M.edges()[Chain.front()].From).Name;
  for (unsigned Ei : Chain) S += " -> " + M.node(M.edges()[Ei].To).Name;
  return S;
}

Diag makeDiag(const Merged &M, const char *Kind, const std::vector<unsigned> &Chain) {
  // the first call of the chain; when several call sites reach the same callee, the earliest
  const Edge &First = M.edges()[Chain.front()];
  return {p08xtu::displayPath(First.Path), First.Line, Kind, chainText(M, Chain), Chain};
}

} // namespace

int main(int argc, const char **argv) {
  Merged M;
  int RC = cglab::runPerTU(argc, argv, Cat, [&](ASTContext &Ctx, CallGraph &CG) {
    cglab::GraphOptions GO;
    GO.MainFileOnly = false;
    M.addTU(cglab::Graph(CG, Ctx, GO));
  });
  if (RC) return 2;
  M.finish();

  std::vector<Diag> Diags;
  unsigned Functions = 0, Recursive = 0, Reaching = 0;
  for (const p08xtu::Node &N : M.nodes()) Functions += N.Def;

  if (!OptNoRecursion)
    for (const p08xtu::Scc &S : M.sccs()) {
      if (!S.Cyclic) continue;
      Recursive += S.Members.size();
      std::set<unsigned> In(S.Members.begin(), S.Members.end());
      unsigned Start = S.Members.front();
      std::vector<unsigned> Cycle = shortestChain(
          M, Start, [&](unsigned W) { return W == Start; }, [&](unsigned W) { return In.count(W) != 0; });
      if (!Cycle.empty()) Diags.push_back(makeDiag(M, "recursion", Cycle));
    }

  if (!OptNoSink)
    for (const p08xtu::Node &N : M.nodes()) {
      if (!N.Def || isSink(N)) continue;
      std::vector<unsigned> Chain = shortestChain(M, N.Id, [&](unsigned W) { return isSink(M.node(W)); });
      if (Chain.empty()) continue;
      ++Reaching;
      Diags.push_back(makeDiag(M, "reaches-sink", Chain));
    }

  std::sort(Diags.begin(), Diags.end(), [](const Diag &A, const Diag &B) {
    return std::tie(A.Path, A.Line, A.Kind, A.Text) < std::tie(B.Path, B.Line, B.Kind, B.Text);
  });
  for (const Diag &D : Diags) {
    llvm::outs() << "diag " << D.Path << ":" << D.Line << ": " << D.Kind << ": " << D.Text << "\n";
    if (OptTrace)
      for (unsigned Ei : D.Chain) {
        const Edge &E = M.edges()[Ei];
        llvm::outs() << "trace " << D.Kind << ": " << M.node(E.From).Name << " -> " << M.node(E.To).Name << " @"
                     << p08xtu::displayPath(E.Path) << ":" << E.Line << "\n";
      }
  }
  llvm::outs() << "summary: " << Functions << " functions, " << Recursive << " recursive, " << Reaching
               << " reach a sink\n";
  return Diags.empty() ? 0 : 1;
}
