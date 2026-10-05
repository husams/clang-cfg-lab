// p08_nodes -- the nodes and edges of a clang::CallGraph (Parts 8.1-8.3).
//
//   p08_nodes <file> [--edges] [--sites] [--kinds] [--usr] [--func=NAME]
//                    [--with-root] [--sort=name|source|rpo] [--emit=text|dot|json]
//                    [--dump [--no-root]] [--library-dot] [--lookup=NAME] [--all-files]
//                    [-- <compile flags>]
//
// Text output, one record per line (names are quoted only when they contain a space):
//   == <file>: N nodes, M edges
//   node <name> [kind=def|decl|implicit|tpl|lambda|block|objc [noreturn] [static]] [usr=<usr>|usr=-]
//   edge <caller> -> <callee> [@L<line> <ExprClass>] [kind=call|ctor|new|objc|block|op]
// --kinds adds the kind column to nodes and edges, --sites the call site to edges, --usr the
// USR. --func restricts the lists to one function and its out-edges. The root and its edges
// to every node appear only with --with-root.
//
// --dump prints CallGraph::print() verbatim (= debug.DumpCallGraph, but on stdout);
// with --no-root the "< root >" line is dropped. --library-dot prints llvm::WriteGraph with
// the pointer ids normalised. --emit=dot|json export the graph (see cglab.h).
//
// --lookup=NAME prints, for every declaration in the redeclaration chain of a function, whether
// CallGraph::getNode() finds it as given (only the canonical, first declaration does) and
// whether cglab::lookupNode(), which canonicalises first, does (Section 8.2):
//   lookup <name> @L<line> decl|def canonical|redecl getNode=found|null canonicalised=found|null

#include "cglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p08_nodes options");
CGLAB_DEFINE_COMMON_FLAGS(Cat)
static llvm::cl::opt<bool> EdgesFlag("edges", llvm::cl::cat(Cat), llvm::cl::desc("print edge lines"));
static llvm::cl::opt<bool> SitesFlag("sites", llvm::cl::cat(Cat),
                                     llvm::cl::desc("add the call site (@L<line> <ExprClass>) to edge lines"));
static llvm::cl::opt<bool> KindsFlag("kinds", llvm::cl::cat(Cat),
                                     llvm::cl::desc("add kind=, noreturn, static to node lines and kind= to edge lines"));
static llvm::cl::opt<bool> UsrFlag("usr", llvm::cl::cat(Cat), llvm::cl::desc("add usr= to node lines"));
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::cat(Cat),
                                           llvm::cl::desc("only this node and its out-edges (printed name)"));
static llvm::cl::opt<bool> DumpFlag("dump", llvm::cl::cat(Cat),
                                    llvm::cl::desc("CallGraph::print() on stdout, verbatim"));
static llvm::cl::opt<bool> NoRootFlag("no-root", llvm::cl::cat(Cat),
                                      llvm::cl::desc("with --dump: drop the < root > line"));
static llvm::cl::opt<std::string> LookupFlag("lookup", llvm::cl::cat(Cat),
                                             llvm::cl::desc("getNode() on every redeclaration of this function"));
static llvm::cl::opt<bool> LibDotFlag("library-dot", llvm::cl::cat(Cat),
                                      llvm::cl::desc("llvm::WriteGraph output, ids normalised"));

namespace {

void report(ASTContext &Ctx, CallGraph &CG) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) std::exit(2);

  if (DumpFlag) {
    cglab::libraryDump(llvm::outs(), CG, NoRootFlag);
    return;
  }
  if (LibDotFlag) {
    cglab::GraphOptions All;
    All.MainFileOnly = false; // WriteGraph prints every node, so the ids must cover every node
    cglab::Graph G(CG, Ctx, All);
    cglab::libraryDot(llvm::outs(), G);
    return;
  }

  cglab::Graph G(CG, Ctx, cgGraphOptions());
  bool Root = CgWithRootFlag;

  if (!LookupFlag.empty()) {
    std::optional<unsigned> Id = G.find(LookupFlag);
    const auto *FD = Id ? dyn_cast_or_null<FunctionDecl>(G.node(*Id).D) : nullptr;
    if (!FD) {
      llvm::errs() << "no function node named '" << LookupFlag << "' in " << G.file() << "\n";
      std::exit(2);
    }
    for (const FunctionDecl *R : FD->redecls())
      llvm::outs() << "lookup " << cglab::quoteName(G.node(*Id).Name) << " @L"
                   << cglab::declLine(G.sm(), R) << (R->doesThisDeclarationHaveABody() ? " def " : " decl ")
                   << (R == FD->getCanonicalDecl() ? "canonical" : "redecl")
                   << " getNode=" << (CG.getNode(R) ? "found" : "null")
                   << " canonicalised=" << (cglab::lookupNode(CG, R) ? "found" : "null") << "\n";
    return;
  }

  if (Emit == cglab::EmitKind::Dot) {
    cglab::DotOptions O;
    O.WithRoot = Root;
    O.SiteLabels = SitesFlag;
    O.Tooltips = KindsFlag || UsrFlag;
    O.TooltipUsr = UsrFlag;
    O.Sort = Sort;
    cglab::writeDot(llvm::outs(), G, O);
    return;
  }
  if (Emit == cglab::EmitKind::Json) {
    cglab::JsonOptions O;
    O.WithRoot = Root;
    O.Sort = Sort;
    cglab::writeJson(llvm::outs(), G, O);
    return;
  }

  std::optional<unsigned> Only;
  if (!FuncFlag.empty()) {
    Only = G.find(FuncFlag);
    if (!Only) {
      llvm::errs() << "no node named '" << FuncFlag << "' in " << G.file() << "\n";
      std::exit(2);
    }
  }
  cglab::printHeader(llvm::outs(), G, Root);
  for (unsigned Id : G.order(Sort, Root)) {
    if (Only && Id != *Only) continue;
    if (Id == G.root()) {
      llvm::outs() << "node <root>\n";
      continue;
    }
    cglab::printNodeLine(llvm::outs(), G.node(Id), KindsFlag, UsrFlag);
  }
  if (!EdgesFlag) return;
  for (unsigned Id : G.order(Sort, Root)) {
    if (Only && Id != *Only) continue;
    for (unsigned Ei : G.outEdges(Id, Sort, Root))
      cglab::printEdgeLine(llvm::outs(), G, G.edge(Ei), SitesFlag, KindsFlag);
  }
}

} // namespace

int main(int argc, const char **argv) { return cglab::runPerTU(argc, argv, Cat, report); }
