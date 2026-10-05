// p08_xtu -- merge the call graphs of several translation units by USR (Part 8.9).
//
//   p08_xtu FILE... [--edges] [--unresolved] [--usr] [--emit=text|dot|json] [-- <flags>]
//
// One clang::CallGraph per file, merged into one graph: a declaration-only node in one TU
// becomes the definition from another, an inline function in a header stays one node, two
// `static` functions of the same name stay two (see Merge.h for the rules).
//
//   == merged: <N> nodes, <M> edges, <T> tus
//   node <name> kind=def|decl [noreturn] [static] tus=<a.cpp,b.cpp> [usr=<usr>]    (--usr)
//   edge <caller> -> <callee> @<file>:<line> [xtu]                                  (--edges)
//   unresolved <name> (decl in <file>)                                              (--unresolved)
//
// tus= lists the files that define the function (that mention it, for a declaration-only
// node). `xtu` marks an edge whose callee is defined in another TU than the one the call
// was seen in: it exists only in the merged graph. Two nodes that print alike carry
// "@<tu>" after the name. --emit=dot draws one `tu` cluster per file, cross-TU edges
// `xtu`, cycle-closing edges `back`, declaration-only nodes `external dim`.

#include "Merge.h"

#include <cstdlib>

using namespace clang;

static llvm::cl::OptionCategory Cat("p08_xtu options");
static llvm::cl::opt<bool> OptEdges("edges", llvm::cl::cat(Cat), llvm::cl::desc("print the merged edges"));
static llvm::cl::opt<bool> OptUnresolved("unresolved", llvm::cl::cat(Cat),
                                         llvm::cl::desc("print the functions no TU defines"));
static llvm::cl::opt<bool> OptUsr("usr", llvm::cl::cat(Cat), llvm::cl::desc("show each node's USR"));
static llvm::cl::opt<std::string> OptEmit("emit", llvm::cl::init("text"), llvm::cl::cat(Cat),
                                          llvm::cl::desc("text | dot | json"));

namespace {

p08xtu::Merged M;

std::string quote(const std::string &N) { return cglab::quoteName(N); }

std::string joinTus(const std::set<std::string> &Tus) {
  std::string S;
  for (const std::string &T : Tus) S += (S.empty() ? "" : ",") + T;
  return S;
}

bool unresolved(const p08xtu::Node &N) { return !N.Def && !N.Implicit; }

void emitText() {
  llvm::outs() << "== merged: " << M.nodes().size() << " nodes, " << M.edges().size() << " edges, "
               << M.tus().size() << " tus\n";
  for (const p08xtu::Node &N : M.nodes()) {
    llvm::outs() << "node " << quote(N.Name) << " kind=" << (N.Def ? "def" : "decl");
    if (N.Noreturn) llvm::outs() << " noreturn";
    if (N.Static) llvm::outs() << " static";
    llvm::outs() << " tus=" << joinTus(N.tus());
    if (OptUsr) llvm::outs() << " usr=" << (N.Usr.empty() ? "-" : N.Usr);
    llvm::outs() << "\n";
  }
  if (OptEdges)
    for (const p08xtu::Edge &E : M.edges())
      llvm::outs() << "edge " << quote(M.node(E.From).Name) << " -> " << quote(M.node(E.To).Name) << " @" << E.File
                   << ":" << E.Line << (E.Xtu ? " xtu" : "") << "\n";
  if (OptUnresolved)
    for (const p08xtu::Node &N : M.nodes())
      if (unresolved(N)) llvm::outs() << "unresolved " << quote(N.Name) << " (decl in " << N.DeclFile << ")\n";
}

cglab::DotAttrs nodeAttrs(const p08xtu::Node &N) {
  cglab::DotAttrs A;
  if (N.Name == "main") A.addClass("entry");
  if (N.Recursive) A.addClass("recursive");
  if (N.Noreturn) A.addClass("sink");
  if (!N.Def) {
    A.addClass("external");
    A.addClass("dim");
  }
  A.Tooltip = std::string("kind=") + (N.Def ? "def" : "decl") + " tus=" + joinTus(N.tus());
  if (OptUsr) A.Tooltip += " usr=" + (N.Usr.empty() ? std::string("-") : N.Usr);
  return A;
}

void emitDot() {
  cglab::DotWriter W(llvm::outs(), "p08_xtu");
  // a node defined in exactly one file sits in that file's cluster; one defined in several
  // (an inline function from a header) and one nobody defines sit between the clusters
  for (const std::string &Tu : M.tus()) {
    W.beginCluster(Tu, Tu, {"tu"});
    for (const p08xtu::Node &N : M.nodes())
      if (N.Def && N.DefTus.size() == 1 && *N.DefTus.begin() == Tu) W.node(N.Name, nodeAttrs(N));
    W.endCluster();
  }
  for (const p08xtu::Node &N : M.nodes())
    if (!N.Def || N.DefTus.size() != 1) {
      cglab::DotAttrs A = nodeAttrs(N);
      if (N.Def) A.addClass("hl");
      W.node(N.Name, A);
    }
  // one drawn edge per (caller, callee) pair
  std::set<std::pair<unsigned, unsigned>> Done;
  const std::vector<p08xtu::Edge> &Es = M.edges();
  for (const p08xtu::Edge &E : Es) {
    if (!Done.insert({E.From, E.To}).second) continue;
    bool Xtu = false, Back = false;
    for (const p08xtu::Edge &F : Es)
      if (F.From == E.From && F.To == E.To) {
        Xtu |= F.Xtu;
        Back |= F.Back;
      }
    cglab::DotAttrs A;
    if (Xtu) A.addClass("xtu");
    if (Back) A.addClass("back");
    W.edge(M.node(E.From).Name, M.node(E.To).Name, A);
  }
  W.finish();
}

void emitJson() {
  llvm::json::Array Tus, Nodes, Edges, Unresolved;
  for (const std::string &T : M.tus()) Tus.push_back(T);
  for (const p08xtu::Node &N : M.nodes()) {
    llvm::json::Array NodeTus;
    for (const std::string &T : N.tus()) NodeTus.push_back(T);
    Nodes.push_back(llvm::json::Object{{"name", N.Name},
                                       {"kind", N.Def ? "def" : "decl"},
                                       {"tus", std::move(NodeTus)},
                                       {"usr", N.Usr},
                                       {"noreturn", N.Noreturn},
                                       {"static", N.Static},
                                       {"recursive", N.Recursive},
                                       {"scc", N.Scc}});
    if (unresolved(N)) Unresolved.push_back(llvm::json::Object{{"name", N.Name}, {"declIn", N.DeclFile}});
  }
  for (const p08xtu::Edge &E : M.edges())
    Edges.push_back(llvm::json::Object{{"from", M.node(E.From).Name},
                                       {"to", M.node(E.To).Name},
                                       {"file", E.File},
                                       {"line", E.Line},
                                       {"xtu", E.Xtu},
                                       {"back", E.Back}});
  llvm::json::Object Doc{{"tus", std::move(Tus)},
                         {"nodes", std::move(Nodes)},
                         {"edges", std::move(Edges)},
                         {"unresolved", std::move(Unresolved)}};
  llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(Doc))) << "\n";
}

} // namespace

int main(int argc, const char **argv) {
  int RC = cglab::runPerTU(argc, argv, Cat, [](ASTContext &Ctx, CallGraph &CG) {
    cglab::GraphOptions GO;
    GO.MainFileOnly = false; // headers count: that is where an inline function lives
    M.addTU(cglab::Graph(CG, Ctx, GO));
  });
  if (RC) return 2;
  cglab::EmitKind Emit;
  if (!cglab::parseEmit(OptEmit, Emit)) {
    llvm::errs() << "p08_xtu: --emit must be text, dot or json\n";
    return 2;
  }
  M.finish();
  if (Emit == cglab::EmitKind::Dot) emitDot();
  else if (Emit == cglab::EmitKind::Json) emitJson();
  else emitText();
  return 0;
}
