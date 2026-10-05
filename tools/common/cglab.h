// cglab.h v1
// cglab.h -- shared helpers for the Part 8 (call graph) tools of the clang-cfg-lab.
//
// Header-only. Include as  #include "cglab.h"  (it includes cfglab.h, so runTool,
// platform flags and lineOf are available too). API changes after v1 are additive.
//
// What it gives you
//   * cglab::Graph            -- a deterministic, name-sorted snapshot of a
//                                clang::CallGraph: nodes with printed names, kinds,
//                                USRs and lines; edges with call-site line/class;
//                                a reverse (callers) map; post-order, reverse
//                                post-order and SCC numbers computed through
//                                llvm::GraphTraits (post_order, ReversePostOrder-
//                                Traversal, scc_iterator, depth_first)
//   * cglab::nodeName()       -- the one naming rule (below)
//   * cglab::DotWriter / Graph::writeDot / Graph::toJson -- --emit=dot|json
//   * cglab::libraryDot()     -- llvm::WriteGraph output with the Node0x... ids replaced
//   * cglab::runPerTU()       -- main() boilerplate: one callback per translation unit
//   * CGLAB_DEFINE_COMMON_FLAGS(Cat) -- --sort --emit --with-root --all-files
//   * added for Sections 8.4-8.9 (p08_build, p08_anycall, p08_mine, p08_nodes --view), at the end
//     of the file: ContextVisitor (a visitor that knows its enclosing function), runPerAST (main()
//     boilerplate that builds no graph), anyCallKindName, visitorFlagsLine, typeName,
//     displayName, normalizeDot (the Node0x -> N<id> rewrite libraryDot uses, for any
//     WriteGraph / ViewGraph text)
//
// Naming rule (printed name == node identity inside one TU)
//   <root>                      the virtual root (clang prints "< root >")
//   ns::f   Class::method       NamedDecl::printQualifiedName
//   twice<int>                  template arguments appended (printQualifiedName drops them)
//   f()::(lambda@L16)::operator()   lambda closure types get @L<line>, so two lambdas in
//                                   one function no longer print the same
//   <block@L26>                 a BlockDecl (no name of its own)
//   Counter::bump:              Objective-C methods
//   Names are quoted in text output only when they contain a space ("operator new").
//   If two nodes still print the same, they are told apart by a "(param,types)"
//   signature; if that does not help, by "@L<line>", then by "#<k>" in source order.
//
// Determinism: node order never depends on pointers. CallGraph iteration is a DenseMap, so
// every list below is sorted (by printed name by default); traversal orders follow the
// graph's own recorded callee order, which is the AST order. Lines come from
// SourceManager::getSpellingLineNumber, file names are llvm::sys::path::filename.

#ifndef CGLAB_H
#define CGLAB_H

#include "cfglab.h"

#include "clang/Analysis/AnyCall.h"
#include "clang/Analysis/CallGraph.h"
#include "clang/Index/USRGeneration.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace cglab {
// llvm::WriteGraph needs a DOTGraphTraits<GraphType> to label nodes. The one clang uses for
// `const CallGraph*` is defined inside CallGraph.cpp, so a tool that instantiates WriteGraph
// on `const CallGraph*` gets the default traits and empty labels ("{}"). LibGraph is a thin
// wrapper that lets the lab supply the same traits (see DOTGraphTraits<LibGraph> below).
struct LibGraph {
  const clang::CallGraph *CG;
};
} // namespace cglab

namespace llvm {
template <> struct GraphTraits<cglab::LibGraph> : GraphTraits<const clang::CallGraph *> {
  using Base = GraphTraits<const clang::CallGraph *>;
  static NodeRef getEntryNode(cglab::LibGraph G) { return G.CG->getRoot(); }
  static Base::nodes_iterator nodes_begin(cglab::LibGraph G) { return Base::nodes_begin(G.CG); }
  static Base::nodes_iterator nodes_end(cglab::LibGraph G) { return Base::nodes_end(G.CG); }
  static unsigned size(cglab::LibGraph G) { return G.CG->size(); }
};
// The labelling rule of CallGraph.cpp: "< root >", the unqualified name, or "< >".
template <> struct DOTGraphTraits<cglab::LibGraph> : DefaultDOTGraphTraits {
  DOTGraphTraits(bool Simple = false) : DefaultDOTGraphTraits(Simple) {}
  static std::string getNodeLabel(const clang::CallGraphNode *N, cglab::LibGraph G) {
    if (G.CG->getRoot() == N) return "< root >";
    if (const auto *ND = dyn_cast_or_null<clang::NamedDecl>(N->getDecl()))
      return ND->getNameAsString();
    return "< >";
  }
};
} // namespace llvm

namespace cglab {
// A small index-based graph with llvm::GraphTraits, used to re-run the SCC algorithm on "this
// call graph plus some extra edges" (Section 8.8). Build it with Graph::adjacency().
struct AdjGraph {
  struct N {
    unsigned Id = 0;
    std::vector<N *> Kids;
  };
  std::vector<N> Nodes; // Nodes[0] is <root>; never resize after the Kids pointers are set
};
} // namespace cglab

namespace llvm {
template <> struct GraphTraits<cglab::AdjGraph::N *> {
  using NodeRef = cglab::AdjGraph::N *;
  using ChildIteratorType = std::vector<cglab::AdjGraph::N *>::iterator;
  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) { return N->Kids.begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->Kids.end(); }
};
template <> struct GraphTraits<cglab::AdjGraph *> : GraphTraits<cglab::AdjGraph::N *> {
  static NodeRef getEntryNode(cglab::AdjGraph *G) { return &G->Nodes[0]; }
};
} // namespace llvm

namespace cglab {

using namespace clang;
using namespace clang::tooling;

// --------------------------------------------------------------------------
// Names, kinds, lines
// --------------------------------------------------------------------------

enum class Kind { Root, Def, Decl, Implicit, Tpl, Lambda, Block, ObjC };

inline const char *kindName(Kind K) {
  switch (K) {
  case Kind::Root: return "root";
  case Kind::Def: return "def";
  case Kind::Decl: return "decl";
  case Kind::Implicit: return "implicit";
  case Kind::Tpl: return "tpl";
  case Kind::Lambda: return "lambda";
  case Kind::Block: return "block";
  case Kind::ObjC: return "objc";
  }
  return "?";
}

// The declaration carrying the body, if there is one (a node is keyed by the
// canonical, usually bodiless, declaration).
inline const Decl *bestDecl(const Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    if (const FunctionDecl *Def = FD->getDefinition()) return Def;
  return D;
}

// Where to put a declaration on the map: its own location, or for an implicit
// member (no location of its own) the class it belongs to.
inline SourceLocation declLoc(const Decl *D) {
  SourceLocation L = D->getLocation();
  if (L.isInvalid())
    if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) L = MD->getParent()->getLocation();
  return L;
}

inline unsigned declLine(const SourceManager &SM, const Decl *D) {
  return SM.getSpellingLineNumber(declLoc(D));
}
inline unsigned siteLine(const SourceManager &SM, const Stmt *S) {
  return SM.getSpellingLineNumber(S->getBeginLoc());
}
inline unsigned siteCol(const SourceManager &SM, const Stmt *S) {
  return SM.getSpellingColumnNumber(S->getBeginLoc());
}

inline bool inMainFile(const SourceManager &SM, const Decl *D) {
  SourceLocation L = declLoc(D);
  return L.isValid() && SM.isInMainFile(SM.getExpansionLoc(L));
}

// "p08_basic.cpp": the main file without its directory.
inline std::string mainFileName(const SourceManager &SM) {
  if (auto FE = SM.getFileEntryRefForID(SM.getMainFileID()))
    return llvm::sys::path::filename(FE->getName()).str();
  return "<unknown>";
}
inline std::string mainFileStem(const SourceManager &SM) {
  return llvm::sys::path::stem(mainFileName(SM)).str();
}

// Call-graph node lookup that does what getNode() does not (CallGraph::getNode takes the
// declaration as given): functions are keyed by their canonical declaration, so canonicalise;
// an Objective-C method is keyed by the method in the @implementation, while its canonical
// declaration is the @interface one, so for those the implementation is looked up too.
inline CallGraphNode *lookupNode(const CallGraph &CG, const Decl *D) {
  if (CallGraphNode *N = CG.getNode(D)) return N;
  if (CallGraphNode *N = CG.getNode(D->getCanonicalDecl())) return N;
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    const ObjCImplDecl *Impl = nullptr;
    const DeclContext *C = MD->getDeclContext();
    if (const auto *I = dyn_cast<ObjCInterfaceDecl>(C)) Impl = I->getImplementation();
    else if (const auto *Cat = dyn_cast<ObjCCategoryDecl>(C)) Impl = Cat->getImplementation();
    else Impl = dyn_cast<ObjCImplDecl>(C);
    if (Impl)
      if (const ObjCMethodDecl *M = Impl->getMethod(MD->getSelector(), MD->isInstanceMethod()))
        return CG.getNode(M);
  }
  return nullptr;
}

inline bool isLambdaCallOp(const Decl *D) {
  const auto *MD = dyn_cast<CXXMethodDecl>(D);
  return MD && MD->getParent()->isLambda();
}

// USR of a declaration; empty when there is none (blocks come back as the bare "c:").
inline std::string usrOf(const Decl *D) {
  llvm::SmallString<128> S;
  if (index::generateUSRForDecl(D, S)) return "";
  if (S == "c:") return "";
  return S.str().str();
}

// The printed name before collisions are resolved.
inline std::string baseName(const Decl *D) {
  const SourceManager &SM = D->getASTContext().getSourceManager();
  if (const auto *BD = dyn_cast<BlockDecl>(D))
    return "<block@L" + std::to_string(SM.getSpellingLineNumber(BD->getCaretLocation())) + ">";
  const auto *ND = dyn_cast<NamedDecl>(D);
  if (!ND) return "<?>";
  std::string S;
  llvm::raw_string_ostream OS(S);
  ND->printQualifiedName(OS);
  OS.flush();
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    if (const TemplateArgumentList *Args = FD->getTemplateSpecializationArgs()) {
      OS << "<";
      bool First = true;
      for (const TemplateArgument &A : Args->asArray()) {
        if (!First) OS << ",";
        First = false;
        A.print(D->getASTContext().getPrintingPolicy(), OS, /*IncludeType=*/false);
      }
      OS << ">";
      OS.flush();
    }
  // printQualifiedName prints every closure type as "(lambda)": tell them apart by line,
  // outermost lambda first, which is left to right in the string.
  std::vector<unsigned> Lines;
  for (const DeclContext *C = D->getDeclContext(); C; C = C->getParent())
    if (const auto *RD = dyn_cast<CXXRecordDecl>(C); RD && RD->isLambda())
      Lines.insert(Lines.begin(), SM.getSpellingLineNumber(RD->getLocation()));
  size_t Pos = 0;
  for (unsigned L : Lines) {
    Pos = S.find("(lambda)", Pos);
    if (Pos == std::string::npos) break;
    std::string Rep = "(lambda@L" + std::to_string(L) + ")";
    S.replace(Pos, 8, Rep);
    Pos += Rep.size();
  }
  return S;
}

inline std::string signatureOf(const Decl *D) {
  const auto *FD = dyn_cast<FunctionDecl>(D);
  if (!FD) return "";
  std::string S = "(";
  bool First = true;
  for (const ParmVarDecl *P : FD->parameters()) {
    if (!First) S += ",";
    First = false;
    S += P->getType().getAsString(D->getASTContext().getPrintingPolicy());
  }
  S += ")";
  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD); MD && MD->isConst()) S += " const";
  return S;
}

inline Kind classify(const Decl *D) {
  if (isa<BlockDecl>(D)) return Kind::Block;
  bool Body = D->hasBody();
  if (isa<ObjCMethodDecl>(D)) return Body ? Kind::ObjC : Kind::Decl;
  if (isLambdaCallOp(D)) return Kind::Lambda;
  if (!Body) return Kind::Decl;
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isImplicit()) return Kind::Implicit;
    if (FD->isTemplateInstantiation()) return Kind::Tpl;
  }
  return Kind::Def;
}

// Text-output quoting: only a name with a space is quoted.
inline std::string quoteName(const std::string &N) {
  if (N.find(' ') == std::string::npos) return N;
  return "\"" + N + "\"";
}

// --------------------------------------------------------------------------
// The graph snapshot
// --------------------------------------------------------------------------

enum class SortKey { Name, Source, Rpo };
enum class EmitKind { Text, Dot, Json };

inline bool parseSort(llvm::StringRef S, SortKey &Out) {
  if (S == "name") Out = SortKey::Name;
  else if (S == "source") Out = SortKey::Source;
  else if (S == "rpo") Out = SortKey::Rpo;
  else return false;
  return true;
}
inline bool parseEmit(llvm::StringRef S, EmitKind &Out) {
  if (S == "text") Out = EmitKind::Text;
  else if (S == "dot") Out = EmitKind::Dot;
  else if (S == "json") Out = EmitKind::Json;
  else return false;
  return true;
}

struct GraphOptions {
  // Keep only nodes defined in the main file plus the nodes they call (declaration-only
  // callees, implicit operator new, ...). false = every node, headers included.
  bool MainFileOnly = true;
};

struct Edge {
  unsigned From = 0, To = 0;
  unsigned Line = 0, Col = 0;     // call-site position; 0 for root edges
  std::string ExprClass;          // CallExpr, CXXMemberCallExpr, CXXConstructExpr, ... ("" for root)
  const char *EdgeKind = "root";  // call | ctor | new | objc | block | op | root
  bool Root = false;              // an edge out of <root>
  bool Back = false;              // closes a cycle: its target is on the DFS stack (from <root>)
  const Expr *Site = nullptr;     // null for root edges
};

struct Node {
  unsigned Id = 0;                  // index in Graph::nodes(); 0 is <root>; the rest follow printed-name order
  CallGraphNode *CGN = nullptr;
  const Decl *D = nullptr;          // the graph's key (canonical declaration); null for <root>
  const Decl *Best = nullptr;       // the definition if there is one
  std::string Name;                 // unique printed name
  Kind K = Kind::Root;
  bool Noreturn = false;
  bool Static = false;              // function with internal linkage
  bool External = false;            // a plain definition (def/objc) with external visibility
  std::string Usr;                  // empty if none
  unsigned Line = 0, Col = 0;       // of the declaration (0 for <root>)
  unsigned Scc = 0;                 // scc_iterator order, bottom-up; <root> is last
  unsigned Po = 0, Rpo = 0;         // post-order / reverse post-order index (over the kept nodes)
  bool Recursive = false;           // member of a cyclic SCC
};

struct Scc {
  unsigned Id = 0;
  std::vector<unsigned> Members;    // node ids, ascending (= name order)
  bool Cyclic = false;
  bool Self = false;                // cyclic with exactly one member: calls itself
};

class Graph {
public:
  Graph(CallGraph &CG, ASTContext &Ctx, GraphOptions O = {}) : CGr(CG), Ctx(Ctx) { build(O); }

  CallGraph &callGraph() const { return CGr; }
  ASTContext &context() const { return Ctx; }
  const SourceManager &sm() const { return Ctx.getSourceManager(); }
  std::string file() const { return mainFileName(sm()); }

  unsigned root() const { return 0; }
  const std::vector<Node> &nodes() const { return Nodes; }
  const std::vector<Edge> &edges() const { return Edges; }
  const Node &node(unsigned Id) const { return Nodes[Id]; }
  const Edge &edge(unsigned Ei) const { return Edges[Ei]; }
  const std::vector<Scc> &sccs() const { return Sccs; }

  // Counts as printed in "== file: N nodes, M edges" (the root and its fan-out only with WithRoot).
  unsigned numNodes(bool WithRoot = false) const {
    return Nodes.size() - (WithRoot ? 0 : 1);
  }
  unsigned numEdges(bool WithRoot = false) const {
    return WithRoot ? Edges.size() : Edges.size() - RootEdges;
  }

  std::optional<unsigned> find(llvm::StringRef Name) const {
    auto It = ByName.find(Name.str());
    if (It == ByName.end()) return std::nullopt;
    return It->second;
  }
  // Canonical-declaration lookup (see lookupNode).
  std::optional<unsigned> find(const Decl *D) const {
    if (const CallGraphNode *N = lookupNode(CGr, D)) return find(N);
    return std::nullopt;
  }
  std::optional<unsigned> find(const CallGraphNode *N) const {
    auto It = Ids.find(N);
    if (It == Ids.end()) return std::nullopt;
    return It->second;
  }

  // ---- ordering ---------------------------------------------------------

  void sortBy(std::vector<unsigned> &V, SortKey K) const {
    std::stable_sort(V.begin(), V.end(), [&](unsigned A, unsigned B) {
      const Node &X = Nodes[A], &Y = Nodes[B];
      switch (K) {
      case SortKey::Name: return A < B; // ids follow name order, root first
      case SortKey::Source:
        return std::tie(X.Line, X.Col, A) < std::tie(Y.Line, Y.Col, B);
      case SortKey::Rpo: return X.Rpo < Y.Rpo;
      }
      return A < B;
    });
  }
  // Every node id in the requested order (the root only with WithRoot).
  std::vector<unsigned> order(SortKey K = SortKey::Name, bool WithRoot = false) const {
    std::vector<unsigned> V;
    for (unsigned I = WithRoot ? 0 : 1; I < Nodes.size(); ++I) V.push_back(I);
    sortBy(V, K);
    return V;
  }

  // ---- edges ------------------------------------------------------------

  // Out-edges of a node, in the requested order. Name: by callee name, then call site;
  // Source: by call site; Rpo: by callee rpo, then call site. Recorded order breaks ties.
  std::vector<unsigned> outEdges(unsigned Id, SortKey K = SortKey::Name,
                                 bool WithRoot = false) const {
    std::vector<unsigned> V;
    for (unsigned Ei : Out[Id])
      if (WithRoot || !Edges[Ei].Root) V.push_back(Ei);
    std::stable_sort(V.begin(), V.end(), [&](unsigned A, unsigned B) {
      const Edge &X = Edges[A], &Y = Edges[B];
      switch (K) {
      case SortKey::Name: return std::tie(X.To, X.Line, X.Col) < std::tie(Y.To, Y.Line, Y.Col);
      case SortKey::Source: return std::tie(X.Line, X.Col, X.To) < std::tie(Y.Line, Y.Col, Y.To);
      case SortKey::Rpo:
        return std::tie(Nodes[X.To].Rpo, X.Line, X.Col) < std::tie(Nodes[Y.To].Rpo, Y.Line, Y.Col);
      }
      return false;
    });
    return V;
  }
  // Edge indices in recorded (AST) order, exactly what the library iterates.
  const std::vector<unsigned> &recordedOut(unsigned Id) const { return Out[Id]; }
  // The reverse map: indices of every edge into Id (including the root's).
  const std::vector<unsigned> &inEdges(unsigned Id) const { return In[Id]; }

  // Callers of Id, ascending by id (= by name); never the root. Transitive: every function
  // from which Id is reachable (Id itself only if it lies on a cycle).
  std::vector<unsigned> callers(unsigned Id, bool Transitive = false) const {
    std::set<unsigned> Seen;
    std::vector<unsigned> Work{Id};
    while (!Work.empty()) {
      unsigned N = Work.back();
      Work.pop_back();
      for (unsigned Ei : In[N]) {
        const Edge &E = Edges[Ei];
        if (E.Root) continue;
        if (Seen.insert(E.From).second && Transitive) Work.push_back(E.From);
      }
      if (!Transitive) break;
    }
    return std::vector<unsigned>(Seen.begin(), Seen.end());
  }
  // Distinct callees of Id, ascending by id.
  std::vector<unsigned> callees(unsigned Id, bool WithRoot = false) const {
    std::set<unsigned> S;
    for (unsigned Ei : Out[Id])
      if (WithRoot || !Edges[Ei].Root) S.insert(Edges[Ei].To);
    return std::vector<unsigned>(S.begin(), S.end());
  }

  // ---- traversals through llvm::GraphTraits ------------------------------

  // post_order(&CG) over the whole graph: callees before callers, <root> last.
  const std::vector<unsigned> &postOrder() const { return PostOrder; }
  // ReversePostOrderTraversal<CallGraph*>: what CallGraph::print and the Static Analyzer use.
  const std::vector<unsigned> &reversePostOrder() const { return Rpo; }
  // The same two orders for the subgraph reachable from one node.
  std::vector<unsigned> postOrderFrom(unsigned Id) const {
    std::vector<unsigned> V;
    for (CallGraphNode *N : llvm::post_order(Nodes[Id].CGN))
      if (auto I = find(N)) V.push_back(*I);
    return V;
  }
  std::vector<unsigned> reversePostOrderFrom(unsigned Id) const {
    std::vector<unsigned> V;
    llvm::ReversePostOrderTraversal<CallGraphNode *> RPOT(Nodes[Id].CGN);
    for (CallGraphNode *N : RPOT)
      if (auto I = find(N)) V.push_back(*I);
    return V;
  }
  // llvm::depth_first from every root; ascending by id. The roots themselves are included.
  std::vector<unsigned> reachableFrom(const std::vector<unsigned> &Roots) const {
    std::set<unsigned> S;
    for (unsigned R : Roots)
      for (CallGraphNode *N : llvm::depth_first(Nodes[R].CGN))
        if (auto I = find(N)) S.insert(*I);
    return std::vector<unsigned>(S.begin(), S.end());
  }

  // ---- extending the graph --------------------------------------------------

  // The graph's edges (recorded order, the root's fan-out included) as an AdjGraph; Extra is
  // appended after the recorded edges of its source.
  AdjGraph adjacency(const std::vector<std::pair<unsigned, unsigned>> &Extra = {}) const {
    AdjGraph AG;
    AG.Nodes.resize(Nodes.size());
    for (unsigned I = 0; I < Nodes.size(); ++I) AG.Nodes[I].Id = I;
    for (unsigned I = 0; I < Nodes.size(); ++I)
      for (unsigned Ei : Out[I]) AG.Nodes[I].Kids.push_back(&AG.Nodes[Edges[Ei].To]);
    for (auto [From, To] : Extra) AG.Nodes[From].Kids.push_back(&AG.Nodes[To]);
    return AG;
  }
  // The SCCs of this graph plus Extra edges (from, to), numbered like sccs(): scc_iterator
  // order from <root>, members ascending. With no extra edges the result equals sccs().
  std::vector<Scc> sccsWithEdges(const std::vector<std::pair<unsigned, unsigned>> &Extra) const {
    AdjGraph AG = adjacency(Extra);
    std::vector<Scc> R;
    for (auto I = llvm::scc_begin(&AG); !I.isAtEnd(); ++I) {
      Scc S;
      for (AdjGraph::N *N : *I) S.Members.push_back(N->Id);
      std::sort(S.Members.begin(), S.Members.end());
      S.Id = R.size();
      S.Cyclic = I.hasCycle();
      S.Self = S.Cyclic && (*I).size() == 1;
      R.push_back(std::move(S));
    }
    return R;
  }

  // ---- text helpers shared by the tools ------------------------------------

  std::string join(const std::vector<unsigned> &Ids) const {
    std::string S;
    for (unsigned I : Ids) S += (S.empty() ? "" : " ") + quoteName(Nodes[I].Name);
    return S;
  }
  std::string name(unsigned Id) const { return quoteName(Nodes[Id].Name); }

private:
  struct Raw {
    CallGraphNode *N;
    std::string Base, Name;
    unsigned LocEnc;
  };

  void build(const GraphOptions &O) {
    const SourceManager &SM = Ctx.getSourceManager();
    CallGraphNode *RootN = CGr.getRoot();

    // 1. which nodes survive the main-file filter
    llvm::DenseMap<const CallGraphNode *, bool> Keep;
    std::vector<Raw> Raws;
    for (auto &KV : CGr) {
      CallGraphNode *N = KV.second.get();
      if (N == RootN) continue;
      bool Main = !O.MainFileOnly || inMainFile(SM, bestDecl(N->getDecl()));
      if (Main) Keep[N] = true;
    }
    if (O.MainFileOnly) {
      std::vector<const CallGraphNode *> Mains;
      for (auto &KV : Keep) Mains.push_back(KV.first);
      for (const CallGraphNode *M : Mains)
        for (const CallGraphNode::CallRecord &R : *M) Keep[R.Callee] = true;
    }
    llvm::DenseMap<const CallGraphNode *, bool> Defined; // nodes whose out-edges are recorded
    for (auto &KV : CGr) {
      CallGraphNode *N = KV.second.get();
      if (N == RootN || !Keep.count(N)) continue;
      // the node is keyed by the canonical (first) declaration, often in a header: judge the
      // definition, which is the one that has a body and out-edges
      Defined[N] = !O.MainFileOnly || inMainFile(SM, bestDecl(N->getDecl()));
      Raws.push_back({N, baseName(N->getDecl()), "", declLoc(bestDecl(N->getDecl())).getRawEncoding()});
    }

    // 2. unique names, independent of DenseMap order
    std::sort(Raws.begin(), Raws.end(), [](const Raw &A, const Raw &B) {
      return std::tie(A.Base, A.LocEnc) < std::tie(B.Base, B.LocEnc);
    });
    for (size_t I = 0; I < Raws.size();) {
      size_t J = I;
      while (J < Raws.size() && Raws[J].Base == Raws[I].Base) ++J;
      if (J - I == 1) {
        Raws[I].Name = Raws[I].Base;
      } else {
        std::map<std::string, std::vector<size_t>> BySig;
        for (size_t K = I; K < J; ++K)
          BySig[Raws[K].Base + signatureOf(Raws[K].N->getDecl())].push_back(K);
        std::map<std::string, std::vector<size_t>> ByLine; // the ones a signature does not separate
        for (auto &[Sig, Ks] : BySig) {
          if (Ks.size() == 1) { Raws[Ks[0]].Name = Sig; continue; }
          for (size_t K : Ks)
            ByLine[Raws[K].Base + "@L" + std::to_string(declLine(SM, bestDecl(Raws[K].N->getDecl())))].push_back(K);
        }
        for (auto &[LName, Ls] : ByLine) {
          if (Ls.size() == 1) { Raws[Ls[0]].Name = LName; continue; }
          std::sort(Ls.begin(), Ls.end()); // Raws is in (name, source) order, so index order = source order
          unsigned Seq = 0;
          for (size_t K : Ls) Raws[K].Name = LName + "#" + std::to_string(++Seq);
        }
      }
      I = J;
    }
    std::sort(Raws.begin(), Raws.end(),
              [](const Raw &A, const Raw &B) { return A.Name < B.Name; });

    // 3. nodes: <root> first, then the rest in name order
    Nodes.reserve(Raws.size() + 1);
    Node R;
    R.Name = "<root>";
    R.CGN = RootN;
    Nodes.push_back(R);
    for (const Raw &Rw : Raws) {
      Node N;
      N.Id = Nodes.size();
      N.CGN = Rw.N;
      N.D = Rw.N->getDecl();
      N.Best = bestDecl(N.D);
      N.Name = Rw.Name;
      N.K = classify(N.D);
      const auto *FD = dyn_cast<FunctionDecl>(N.Best);
      N.Noreturn = FD && FD->isNoReturn();
      bool Plain = N.K == Kind::Def || N.K == Kind::Decl || N.K == Kind::Implicit || N.K == Kind::Tpl;
      N.Static = FD && Plain && !FD->isExternallyVisible();
      N.External = (N.K == Kind::Def && FD && FD->isExternallyVisible()) || N.K == Kind::ObjC;
      N.Usr = usrOf(N.D);
      N.Line = declLine(SM, N.Best);
      SourceLocation L = declLoc(N.Best);
      N.Col = L.isValid() ? SM.getSpellingColumnNumber(L) : 0;
      Nodes.push_back(std::move(N));
    }
    ByName.clear();
    for (const Node &N : Nodes) {
      ByName[N.Name] = N.Id;
      Ids[N.CGN] = N.Id;
    }
    Out.assign(Nodes.size(), {});
    In.assign(Nodes.size(), {});

    // 4. edges, in the library's recorded order; the root's fan-out first
    auto Add = [&](unsigned From, const CallGraphNode::CallRecord &Rec, bool IsRoot) {
      auto To = find(Rec.Callee);
      if (!To) return;
      Edge E;
      E.From = From;
      E.To = *To;
      E.Root = IsRoot;
      if (const Expr *X = Rec.CallExpr) {
        E.Site = X;
        E.Line = siteLine(SM, X);
        E.Col = siteCol(SM, X);
        E.ExprClass = X->getStmtClassName();
        E.EdgeKind = edgeKind(X, Rec.Callee->getDecl());
      }
      Out[From].push_back(Edges.size());
      In[*To].push_back(Edges.size());
      if (IsRoot) ++RootEdges;
      Edges.push_back(std::move(E));
    };
    for (const CallGraphNode::CallRecord &Rec : *RootN) Add(0, Rec, true);
    for (const Node &N : Nodes) {
      if (N.Id == 0 || !Defined[N.CGN]) continue;
      for (const CallGraphNode::CallRecord &Rec : *N.CGN) Add(N.Id, Rec, false);
    }

    // 5. orders and SCCs through llvm::GraphTraits<CallGraph*>
    unsigned Po = 0;
    for (CallGraphNode *N : llvm::post_order(&CGr))
      if (auto I = find(N)) {
        Nodes[*I].Po = Po++;
        PostOrder.push_back(*I);
      }
    unsigned Rp = 0;
    llvm::ReversePostOrderTraversal<CallGraph *> RPOT(&CGr);
    for (CallGraphNode *N : RPOT)
      if (auto I = find(N)) {
        Nodes[*I].Rpo = Rp++;
        Rpo.push_back(*I);
      }
    for (auto I = llvm::scc_begin(&CGr); !I.isAtEnd(); ++I) {
      Scc S;
      for (CallGraphNode *N : *I)
        if (auto Id = find(N)) S.Members.push_back(*Id);
      if (S.Members.empty()) continue;
      std::sort(S.Members.begin(), S.Members.end());
      S.Id = Sccs.size();
      S.Cyclic = I.hasCycle();
      S.Self = S.Cyclic && (*I).size() == 1;
      for (unsigned M : S.Members) {
        Nodes[M].Scc = S.Id;
        Nodes[M].Recursive = S.Cyclic;
      }
      Sccs.push_back(std::move(S));
    }

    // 6. back edges: DFS from <root> in recorded order; an edge into the DFS stack closes a cycle
    std::vector<char> Color(Nodes.size(), 0); // 0 white, 1 on stack, 2 done
    struct Frame { unsigned V; size_t I; };
    std::vector<Frame> Stack{{0, 0}};
    Color[0] = 1;
    while (!Stack.empty()) {
      unsigned V = Stack.back().V;
      if (Stack.back().I == Out[V].size()) {
        Color[V] = 2;
        Stack.pop_back();
        continue;
      }
      Edge &E = Edges[Out[V][Stack.back().I++]];
      if (Color[E.To] == 1) {
        E.Back = true;
      } else if (Color[E.To] == 0) {
        Color[E.To] = 1;
        Stack.push_back({E.To, 0});
      }
    }
  }

  static const char *edgeKind(const Expr *X, const Decl *Callee) {
    if (isa<BlockDecl>(Callee)) return "block"; // CallExpr whose callee is a BlockExpr
    if (isa<CXXOperatorCallExpr>(X)) return "op";
    if (isa<CallExpr>(X)) return "call";        // also CXXMemberCallExpr
    if (isa<CXXConstructExpr>(X)) return "ctor";
    if (isa<CXXNewExpr>(X)) return "new";
    if (isa<ObjCMessageExpr>(X)) return "objc";
    return "call";
  }

  CallGraph &CGr;
  ASTContext &Ctx;
  std::vector<Node> Nodes;
  std::vector<Edge> Edges;
  std::vector<std::vector<unsigned>> Out, In;
  std::vector<unsigned> PostOrder, Rpo;
  std::vector<Scc> Sccs;
  std::map<std::string, unsigned> ByName;
  llvm::DenseMap<const CallGraphNode *, unsigned> Ids; // lookup only, never iterated
  unsigned RootEdges = 0;
};

// --------------------------------------------------------------------------
// Text lines
// --------------------------------------------------------------------------

inline void printHeader(llvm::raw_ostream &OS, const Graph &G, bool WithRoot) {
  OS << "== " << G.file() << ": " << G.numNodes(WithRoot) << " nodes, " << G.numEdges(WithRoot)
     << " edges\n";
}

// node <name> [kind=K [noreturn] [static]] [usr=U|usr=-]
inline void printNodeLine(llvm::raw_ostream &OS, const Node &N, bool Kinds, bool Usr) {
  OS << "node " << quoteName(N.Name);
  if (Kinds) {
    OS << " kind=" << kindName(N.K);
    if (N.Noreturn) OS << " noreturn";
    if (N.Static) OS << " static";
  }
  if (Usr) OS << " usr=" << (N.Usr.empty() ? "-" : N.Usr);
  OS << "\n";
}

// edge <caller> -> <callee> [@L<line> <ExprClass>] [kind=K]
inline void printEdgeLine(llvm::raw_ostream &OS, const Graph &G, const Edge &E, bool Sites,
                          bool Kinds) {
  OS << "edge " << G.name(E.From) << " -> " << G.name(E.To);
  if (Sites && E.Line) OS << " @L" << E.Line << " " << E.ExprClass;
  if (Kinds && !E.Root) OS << " kind=" << E.EdgeKind;
  OS << "\n";
}

// scc <id> [cyclic self|mutual]: <members...>   (one line per SCC, in the given order; the
// root's own SCC only with WithRoot; CyclicOnly drops the trivial ones)
inline void printSccs(llvm::raw_ostream &OS, const Graph &G, const std::vector<Scc> &Sccs,
                      bool CyclicOnly = false, bool WithRoot = false) {
  for (const Scc &S : Sccs) {
    if (CyclicOnly && !S.Cyclic) continue;
    std::vector<unsigned> M = S.Members;
    if (!WithRoot) M.erase(std::remove(M.begin(), M.end(), G.root()), M.end());
    if (M.empty()) continue;
    OS << "scc " << S.Id;
    if (S.Cyclic) OS << " cyclic " << (S.Self ? "self" : "mutual");
    OS << ": " << G.join(M) << "\n";
  }
}

// --------------------------------------------------------------------------
// DOT (the lab's diagram contract: class= only, no colors, fonts or sizes)
// --------------------------------------------------------------------------

inline std::string dotQuote(llvm::StringRef S) {
  std::string R = "\"";
  for (char C : S) {
    if (C == '"' || C == '\\') R += '\\';
    if (C == '\n') { R += "\\n"; continue; }
    R += C;
  }
  return R + "\"";
}
inline std::string dotId(llvm::StringRef S) {
  static const char *Keywords[] = {"node", "edge", "graph", "digraph", "subgraph", "strict"};
  bool Plain = !S.empty() && (isalpha((unsigned char)S[0]) || S[0] == '_');
  for (char C : S) Plain = Plain && (isalnum((unsigned char)C) || C == '_');
  for (const char *K : Keywords)
    if (S.equals_insensitive(K)) Plain = false;
  return Plain ? S.str() : dotQuote(S);
}

struct DotAttrs {
  std::vector<std::string> Classes; // class= values from the shared vocabulary
  std::string Label, XLabel, Tooltip;
  std::vector<std::string> Extra;   // raw "name=value" for the few other attributes (style=dashed)
  void addClass(const std::string &C) {
    if (std::find(Classes.begin(), Classes.end(), C) == Classes.end()) Classes.push_back(C);
  }
  void dropClass(const std::string &C) { Classes.erase(std::remove(Classes.begin(), Classes.end(), C), Classes.end()); }
};

class DotWriter {
public:
  DotWriter(llvm::raw_ostream &OS, llvm::StringRef Name, llvm::StringRef RankDir = "") : OS(OS) {
    OS << "digraph " << dotId(Name) << " {\n";
    if (!RankDir.empty()) OS << "  rankdir=" << RankDir << ";\n";
  }
  void beginCluster(llvm::StringRef Id, llvm::StringRef Label,
                    const std::vector<std::string> &Classes = {}) {
    indent();
    OS << "subgraph " << dotId("cluster_" + Id.str()) << " {\n";
    ++Depth;
    indent();
    OS << "label=" << dotQuote(Label) << ";\n";
    if (!Classes.empty()) {
      indent();
      OS << "class=" << dotQuote(joinClasses(Classes)) << ";\n";
    }
  }
  void endCluster() {
    --Depth;
    indent();
    OS << "}\n";
  }
  void node(llvm::StringRef Id, const DotAttrs &A = {}) {
    indent();
    OS << dotId(Id) << attrs(A) << ";\n";
  }
  void edge(llvm::StringRef From, llvm::StringRef To, const DotAttrs &A = {}) {
    indent();
    OS << dotId(From) << " -> " << dotId(To) << attrs(A) << ";\n";
  }
  void finish() { OS << "}\n"; }

private:
  static std::string joinClasses(const std::vector<std::string> &C) {
    std::string S;
    for (const std::string &X : C) S += (S.empty() ? "" : " ") + X;
    return S;
  }
  static std::string attrs(const DotAttrs &A) {
    std::vector<std::string> P;
    if (!A.Label.empty()) P.push_back("label=" + dotQuote(A.Label));
    if (!A.XLabel.empty()) P.push_back("xlabel=" + dotQuote(A.XLabel));
    if (!A.Tooltip.empty()) P.push_back("tooltip=" + dotQuote(A.Tooltip));
    for (const std::string &E : A.Extra) P.push_back(E);
    if (!A.Classes.empty()) P.push_back("class=" + dotQuote(joinClasses(A.Classes)));
    if (P.empty()) return "";
    std::string S = " [";
    for (size_t I = 0; I < P.size(); ++I) S += (I ? ", " : "") + P[I];
    return S + "]";
  }
  void indent() { OS << std::string(Depth * 2, ' '); }
  llvm::raw_ostream &OS;
  unsigned Depth = 1;
};

// An edge that is not in the Graph (a candidate edge from a resolution step, say).
struct ExtraEdge {
  unsigned From = 0, To = 0;
  std::string Label;
  std::vector<std::string> Classes; // e.g. {"indirect"}, {"cha"}, {"virtual"}
};

struct DotOptions {
  std::string Name;                  // digraph name; default: the main file's stem
  bool WithRoot = false;             // draw <root> and its fan-out (weak edges)
  bool SiteLabels = false;           // label edges with their call-site lines (@L9)
  bool SccClusters = false;          // cyclic SCCs as class="scc" clusters
  bool OrderLabels = false;          // xlabel "po I / rpo J" on every node
  bool Tooltips = false;             // tooltip "kind=def" on nodes ...
  bool TooltipUsr = false;           // ... plus " usr=..."
  SortKey Sort = SortKey::Name;
  std::function<bool(const Node &)> Keep;                      // draw only these nodes
  std::function<void(const Node &, DotAttrs &)> NodeHook;      // edit the defaults
  std::function<void(const Edge &, DotAttrs &)> EdgeHook;
  std::function<std::string(const Scc &)> ClusterLabel;        // default "scc <id>"
  std::vector<ExtraEdge> Extra;      // drawn after the graph's own edges
};

// Default node classes: root; sink (noreturn); dim (declaration only); recursive (cyclic SCC);
// entry (main). Default edge classes: weak (from <root>); back (closes a cycle).
inline DotAttrs defaultNodeAttrs(const Node &N, bool Usr = false) {
  DotAttrs A;
  if (N.K == Kind::Root) A.addClass("root");
  if (N.Name == "main") A.addClass("entry");
  if (N.Recursive) A.addClass("recursive");
  if (N.Noreturn) A.addClass("sink");
  if (N.K == Kind::Decl) A.addClass("dim");
  if (N.K != Kind::Root) {
    A.Tooltip = std::string("kind=") + kindName(N.K);
    if (Usr) A.Tooltip += " usr=" + (N.Usr.empty() ? std::string("-") : N.Usr);
  }
  return A;
}

inline void writeDot(llvm::raw_ostream &OS, const Graph &G, const DotOptions &O = {}) {
  DotWriter W(OS, O.Name.empty() ? mainFileStem(G.sm()) : O.Name);
  auto Show = [&](const Node &N) {
    if (N.Id == 0 && !O.WithRoot) return false;
    return !O.Keep || O.Keep(N);
  };
  auto NodeAttrs = [&](const Node &N) {
    DotAttrs A = defaultNodeAttrs(N, O.TooltipUsr);
    if (!O.Tooltips) A.Tooltip.clear();
    if (O.OrderLabels) A.XLabel = "po " + std::to_string(N.Po) + " / rpo " + std::to_string(N.Rpo);
    if (O.NodeHook) O.NodeHook(N, A);
    return A;
  };
  std::vector<unsigned> Order = G.order(O.Sort, O.WithRoot);
  std::set<unsigned> InCluster;
  if (O.SccClusters)
    for (const Scc &S : G.sccs()) {
      if (!S.Cyclic) continue;
      std::vector<unsigned> M;
      for (unsigned Id : S.Members)
        if (Show(G.node(Id))) M.push_back(Id);
      if (M.empty()) continue;
      G.sortBy(M, O.Sort);
      W.beginCluster("scc" + std::to_string(S.Id),
                     O.ClusterLabel ? O.ClusterLabel(S) : "scc " + std::to_string(S.Id), {"scc"});
      for (unsigned Id : M) {
        W.node(G.node(Id).Name, NodeAttrs(G.node(Id)));
        InCluster.insert(Id);
      }
      W.endCluster();
    }
  for (unsigned Id : Order)
    if (Show(G.node(Id)) && !InCluster.count(Id)) W.node(G.node(Id).Name, NodeAttrs(G.node(Id)));
  // one drawn edge per (caller, callee) pair; parallel call sites are folded into the label
  for (unsigned Id : Order) {
    if (!Show(G.node(Id))) continue;
    std::vector<unsigned> Es = G.outEdges(Id, O.Sort, O.WithRoot);
    std::set<unsigned> Done;
    for (size_t I = 0; I < Es.size(); ++I) {
      const Edge &E = G.edge(Es[I]);
      if (!Show(G.node(E.To)) || !Done.insert(E.To).second) continue;
      std::string Sites;
      unsigned Count = 0;
      for (size_t J = I; J < Es.size(); ++J) {
        const Edge &F = G.edge(Es[J]);
        if (F.To != E.To) continue;
        ++Count;
        if (O.SiteLabels && F.Line) Sites += (Sites.empty() ? "@L" : ",@L") + std::to_string(F.Line);
      }
      DotAttrs A;
      if (E.Root) A.addClass("weak");
      if (E.Back) A.addClass("back");
      if (O.SiteLabels) A.Label = Sites;
      else if (Count > 1) A.Label = "\xC3\x97" + std::to_string(Count); // "×2"
      if (O.EdgeHook) O.EdgeHook(E, A);
      W.edge(G.node(E.From).Name, G.node(E.To).Name, A);
    }
  }
  for (const ExtraEdge &X : O.Extra) {
    if (!Show(G.node(X.From)) || !Show(G.node(X.To))) continue;
    DotAttrs A;
    A.Label = X.Label;
    A.Classes = X.Classes;
    W.edge(G.node(X.From).Name, G.node(X.To).Name, A);
  }
  W.finish();
}

// --------------------------------------------------------------------------
// JSON
// --------------------------------------------------------------------------

struct JsonOptions {
  bool WithRoot = false;
  SortKey Sort = SortKey::Name;
  std::function<bool(const Node &)> Keep;
  std::function<void(const Node &, llvm::json::Object &)> NodeHook;  // add fields
  std::function<void(const Edge &, llvm::json::Object &)> EdgeHook;
  std::function<void(const Scc &, llvm::json::Object &)> SccHook;
};

// {"file", "nodes":[{name,kind,line,scc,po,rpo,recursive,noreturn,static,usr}],
//  "edges":[{from,to,line,col,class,kind,back}], "sccs":[{id,cyclic,self,members}]}
// (llvm::json::Object prints its keys alphabetically.)
inline llvm::json::Object toJson(const Graph &G, const JsonOptions &O = {}) {
  auto Show = [&](const Node &N) {
    if (N.Id == 0 && !O.WithRoot) return false;
    return !O.Keep || O.Keep(N);
  };
  llvm::json::Array Ns, Es, Ss;
  std::vector<unsigned> Order = G.order(O.Sort, O.WithRoot);
  for (unsigned Id : Order) {
    const Node &N = G.node(Id);
    if (!Show(N)) continue;
    llvm::json::Object J{{"name", N.Name}, {"kind", kindName(N.K)}, {"line", N.Line},
                         {"scc", N.Scc}, {"po", N.Po}, {"rpo", N.Rpo},
                         {"recursive", N.Recursive}, {"noreturn", N.Noreturn},
                         {"static", N.Static}, {"usr", N.Usr}};
    if (O.NodeHook) O.NodeHook(N, J);
    Ns.push_back(std::move(J));
  }
  for (unsigned Id : Order) {
    if (!Show(G.node(Id))) continue;
    for (unsigned Ei : G.outEdges(Id, O.Sort, O.WithRoot)) {
      const Edge &E = G.edge(Ei);
      if (!Show(G.node(E.To))) continue;
      llvm::json::Object J{{"from", G.node(E.From).Name}, {"to", G.node(E.To).Name},
                           {"line", E.Line}, {"col", E.Col}, {"class", E.ExprClass},
                           {"kind", E.EdgeKind}, {"back", E.Back}};
      if (O.EdgeHook) O.EdgeHook(E, J);
      Es.push_back(std::move(J));
    }
  }
  for (const Scc &S : G.sccs()) {
    llvm::json::Array M;
    for (unsigned Id : S.Members)
      if (Id != 0 || O.WithRoot) M.push_back(G.node(Id).Name);
    if (M.empty()) continue;
    llvm::json::Object J{{"id", S.Id}, {"cyclic", S.Cyclic}, {"self", S.Self},
                         {"members", std::move(M)}};
    if (O.SccHook) O.SccHook(S, J);
    Ss.push_back(std::move(J));
  }
  return llvm::json::Object{{"file", G.file()}, {"nodes", std::move(Ns)}, {"edges", std::move(Es)},
                            {"sccs", std::move(Ss)}};
}

inline void writeJson(llvm::raw_ostream &OS, const Graph &G, const JsonOptions &O = {}) {
  OS << llvm::formatv("{0:2}", llvm::json::Value(toJson(G, O))) << "\n";
}

// --------------------------------------------------------------------------
// The library's own printers
// --------------------------------------------------------------------------

// CallGraph::print() verbatim (= debug.DumpCallGraph, but on stdout). With DropRoot the
// "Function: < root > calls: ..." line is removed.
inline void libraryDump(llvm::raw_ostream &OS, const CallGraph &CG, bool DropRoot = false) {
  std::string S;
  llvm::raw_string_ostream SS(S);
  CG.print(SS);
  SS.flush();
  if (!DropRoot) { OS << S; return; }
  size_t Pos = 0;
  while (Pos < S.size()) {
    size_t End = S.find('\n', Pos);
    End = End == std::string::npos ? S.size() : End + 1;
    if (!llvm::StringRef(S).substr(Pos).starts_with("  Function: < root > calls:"))
      OS << S.substr(Pos, End - Pos);
    Pos = End;
  }
}

// Rewrite the text of an llvm::WriteGraph / llvm::ViewGraph of the call graph so that it is the
// same on every run: the Node0x<pointer> ids become N<id> (ids in printed-name order, <root> =
// N0) and the node blocks are sorted. G must have been built with MainFileOnly=false (the
// graph writers print every node, so the ids must cover every node).
inline void normalizeDot(llvm::raw_ostream &OS, const std::string &Raw, const Graph &G) {
  std::map<std::string, std::string> Rename;
  for (const Node &N : G.nodes()) {
    std::string P;
    llvm::raw_string_ostream PS(P);
    PS << "Node" << static_cast<const void *>(N.CGN);
    PS.flush();
    Rename[P] = "N" + std::to_string(N.Id);
  }
  // header lines, then one block per node (the node line and the edge lines that follow it)
  std::vector<std::string> Head, Tail;
  std::map<unsigned, std::string> Blocks;
  int Cur = -1;
  size_t Pos = 0;
  while (Pos < Raw.size()) {
    size_t End = Raw.find('\n', Pos);
    std::string Line = Raw.substr(Pos, End == std::string::npos ? std::string::npos : End - Pos);
    Pos = End == std::string::npos ? Raw.size() : End + 1;
    for (auto &[From, To] : Rename)
      for (size_t P = Line.find(From); P != std::string::npos; P = Line.find(From, P + To.size()))
        Line.replace(P, From.size(), To);
    bool IsNode = Line.size() > 1 && Line[0] == '\t' && Line.find("N") == 1 &&
                  Line.find("->") == std::string::npos && Line.find('[') != std::string::npos;
    if (IsNode) {
      Cur = std::stoi(Line.substr(2, Line.find(' ') - 2));
      Blocks[Cur] += Line + "\n";
    } else if (Cur >= 0 && Line != "}") {
      Blocks[Cur] += Line + "\n";
    } else if (Cur >= 0) {
      Tail.push_back(Line);
    } else {
      Head.push_back(Line);
    }
  }
  for (const std::string &L : Head) OS << L << "\n";
  for (auto &[Id, B] : Blocks) OS << B;
  for (const std::string &L : Tail) OS << L << "\n";
}

// llvm::WriteGraph over the call graph (through LibGraph, which supplies CallGraph.cpp's
// labels), normalised by normalizeDot. Build G with MainFileOnly=false: WriteGraph prints
// every node.
inline void libraryDot(llvm::raw_ostream &OS, const Graph &G) {
  std::string Raw;
  llvm::raw_string_ostream RS(Raw);
  llvm::WriteGraph(RS, LibGraph{&G.callGraph()}, /*ShortNames=*/false, "CallGraph");
  RS.flush();
  normalizeDot(OS, Raw, G);
}

// --------------------------------------------------------------------------
// main() boilerplate: one callback per translation unit
// --------------------------------------------------------------------------

inline std::unique_ptr<CallGraph> buildCallGraph(ASTContext &Ctx) {
  auto CG = std::make_unique<CallGraph>();
  CG->addToCallGraph(Ctx.getTranslationUnitDecl());
  return CG;
}

using TuCallback = std::function<void(ASTContext &, CallGraph &)>;

namespace detail {
struct TuConsumer : ASTConsumer {
  const TuCallback &CB;
  explicit TuConsumer(const TuCallback &F) : CB(F) {}
  void HandleTranslationUnit(ASTContext &Ctx) override {
    std::unique_ptr<CallGraph> CG = buildCallGraph(Ctx);
    CB(Ctx, *CG);
  }
};
struct TuActionFactory : FrontendActionFactory {
  const TuCallback &CB;
  explicit TuActionFactory(const TuCallback &F) : CB(F) {}
  std::unique_ptr<FrontendAction> create() override {
    struct Act : ASTFrontendAction {
      const TuCallback &CB;
      explicit Act(const TuCallback &F) : CB(F) {}
      std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef) override {
        return std::make_unique<TuConsumer>(CB);
      }
    };
    return std::make_unique<Act>(CB);
  }
};
} // namespace detail

// Parse the command line (-- flags, platform flags as in cfglab::runTool), build one
// CallGraph per source file and hand it to CB.
inline int runPerTU(int argc, const char **argv, llvm::cl::OptionCategory &Cat, TuCallback CB) {
  std::vector<std::string> Storage;
  auto Args = cfglab::withDefaultCompileFlags(argc, argv, Storage);
  int N = static_cast<int>(Args.size());
  auto Options = CommonOptionsParser::create(N, Args.data(), Cat);
  if (!Options) {
    llvm::errs() << llvm::toString(Options.takeError());
    return 1;
  }
  ClangTool Tool(Options->getCompilations(), Options->getSourcePathList());
  cfglab::addPlatformFlags(Tool);
  detail::TuActionFactory F(CB);
  return Tool.run(&F);
}

} // namespace cglab

// Put this once at namespace scope in a tool, after its OptionCategory, to give the tool
//     --sort=name|source|rpo  --emit=text|dot|json  --with-root  --all-files
// and two helpers:  bool cgCommonFlags(cglab::SortKey&, cglab::EmitKind&)  (false after
//                                                       printing why on a bad value)
//                   cglab::GraphOptions cgGraphOptions()
// Variable names carry a Cg prefix: a bare `All` or `Sort` collides with clang:: names.
#define CGLAB_DEFINE_COMMON_FLAGS(CAT)                                                       \
  static llvm::cl::opt<std::string> CgSortFlag(                                              \
      "sort", llvm::cl::init("name"), llvm::cl::cat(CAT),                                    \
      llvm::cl::desc("order of node and edge lists: name|source|rpo"));                      \
  static llvm::cl::opt<std::string> CgEmitFlag(                                              \
      "emit", llvm::cl::init("text"), llvm::cl::cat(CAT),                                    \
      llvm::cl::desc("output format: text|dot|json"));                                       \
  static llvm::cl::opt<bool> CgWithRootFlag(                                                 \
      "with-root", llvm::cl::cat(CAT),                                                       \
      llvm::cl::desc("include <root> and its edges to every node"));                         \
  static llvm::cl::opt<bool> CgAllFilesFlag(                                                 \
      "all-files", llvm::cl::cat(CAT),                                                       \
      llvm::cl::desc("do not restrict the graph to functions of the main file"));            \
  [[maybe_unused]] static bool cgCommonFlags(cglab::SortKey &S, cglab::EmitKind &E) {        \
    if (!cglab::parseSort(CgSortFlag, S)) {                                                  \
      llvm::errs() << "--sort must be name, source or rpo\n";                                \
      return false;                                                                          \
    }                                                                                        \
    if (!cglab::parseEmit(CgEmitFlag, E)) {                                                  \
      llvm::errs() << "--emit must be text, dot or json\n";                                  \
      return false;                                                                          \
    }                                                                                        \
    return true;                                                                             \
  }                                                                                          \
  [[maybe_unused]] static cglab::GraphOptions cgGraphOptions() {                             \
    cglab::GraphOptions O;                                                                   \
    O.MainFileOnly = !CgAllFilesFlag;                                                        \
    return O;                                                                                \
  }

// --------------------------------------------------------------------------
// Additions for p08_build, p08_anycall and p08_mine (Sections 8.4, 8.7, 8.8). The helpers below
// are new; above this line only libraryDot changed, and only in that its text rewriting moved
// into normalizeDot (same output, checked byte for byte against the previous build).
// --------------------------------------------------------------------------

namespace cglab {

inline const char *anyCallKindName(AnyCall::Kind K) {
  switch (K) {
  case AnyCall::Function: return "Function";
  case AnyCall::ObjCMethod: return "ObjCMethod";
  case AnyCall::Block: return "Block";
  case AnyCall::Destructor: return "Destructor";
  case AnyCall::Constructor: return "Constructor";
  case AnyCall::InheritedConstructor: return "InheritedConstructor";
  case AnyCall::Allocator: return "Allocator";
  case AnyCall::Deallocator: return "Deallocator";
  }
  return "?";
}

// The four data members of DynamicRecursiveASTVisitor that change what a CallGraph visits, as
// one line:  flags implicit=1 instantiations=1 typelocs=0 lambda-body=1
inline std::string visitorFlagsLine(const DynamicRecursiveASTVisitor &V) {
  return std::string("flags implicit=") + (V.ShouldVisitImplicitCode ? "1" : "0") +
         " instantiations=" + (V.ShouldVisitTemplateInstantiations ? "1" : "0") +
         " typelocs=" + (V.ShouldWalkTypesOfTypeLocs ? "1" : "0") +
         " lambda-body=" + (V.ShouldVisitLambdaBody ? "1" : "0");
}

// A printed type ("int (*)(int)", "void *").
inline std::string typeName(QualType T, const ASTContext &Ctx) {
  return T.getAsString(Ctx.getPrintingPolicy());
}

// The name the tools print for any declaration: the graph's unique node name when the graph
// has a node for it, the naming rule's base name otherwise (a field, a declaration the graph
// does not keep, an implicit operator delete ...).
inline std::string displayName(const Graph &G, const Decl *D) {
  if (!D) return "?";
  if (std::optional<unsigned> Id = G.find(D)) return G.node(*Id).Name;
  return baseName(D);
}

// A DynamicRecursiveASTVisitor that knows which declaration it is inside: the innermost
// function, method, Objective-C method, block, field (while visiting its in-class initialiser)
// and, with VarContexts, file-scope or static-member variable (while visiting its initialiser).
// Derive, set the Should* flags in the constructor, override Visit* and read cur(). The
// context is pushed in TraverseDecl, which the visitor calls for every declaration it reaches,
// a lambda's call operator and a block included.
struct ContextVisitor : DynamicRecursiveASTVisitor {
  bool VarContexts = false;

  const Decl *cur() const { return Stack.empty() ? nullptr : Stack.back(); }

  bool isContext(const Decl *D) const {
    if (isa<FunctionDecl>(D) || isa<ObjCMethodDecl>(D) || isa<BlockDecl>(D)) return true;
    if (const auto *FD = dyn_cast<FieldDecl>(D)) return FD->hasInClassInitializer();
    if (const auto *VD = dyn_cast<VarDecl>(D))
      return VarContexts && VD->hasInit() && (VD->isFileVarDecl() || VD->isStaticDataMember());
    return false;
  }

  // Inside a template pattern or a member of a class template pattern: the expressions there
  // are not resolved yet.
  static bool inDependentContext(const Decl *D) {
    if (isa<DeclContext>(D)) return cast<DeclContext>(D)->isDependentContext();
    return D->getDeclContext()->isDependentContext();
  }

  bool TraverseDecl(Decl *D) override {
    if (!D || !isContext(D)) return DynamicRecursiveASTVisitor::TraverseDecl(D);
    Stack.push_back(D);
    bool R = DynamicRecursiveASTVisitor::TraverseDecl(D);
    Stack.pop_back();
    return R;
  }

private:
  std::vector<const Decl *> Stack;
};

// Like runPerTU, but the callback gets only the ASTContext and builds the CallGraph itself:
// a tool that sets the visitor flags before addToCallGraph, or adds functions one at a time,
// cannot use runPerTU (which builds the graph with the library's defaults first).
using AstCallback = std::function<void(ASTContext &)>;

namespace detail {
struct AstConsumer : ASTConsumer {
  const AstCallback &CB;
  explicit AstConsumer(const AstCallback &F) : CB(F) {}
  void HandleTranslationUnit(ASTContext &Ctx) override { CB(Ctx); }
};
struct AstActionFactory : FrontendActionFactory {
  const AstCallback &CB;
  explicit AstActionFactory(const AstCallback &F) : CB(F) {}
  std::unique_ptr<FrontendAction> create() override {
    struct Act : ASTFrontendAction {
      const AstCallback &CB;
      explicit Act(const AstCallback &F) : CB(F) {}
      std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef) override {
        return std::make_unique<AstConsumer>(CB);
      }
    };
    return std::make_unique<Act>(CB);
  }
};
} // namespace detail

inline int runPerAST(int argc, const char **argv, llvm::cl::OptionCategory &Cat, AstCallback CB) {
  std::vector<std::string> Storage;
  auto Args = cfglab::withDefaultCompileFlags(argc, argv, Storage);
  int N = static_cast<int>(Args.size());
  auto Options = CommonOptionsParser::create(N, Args.data(), Cat);
  if (!Options) {
    llvm::errs() << llvm::toString(Options.takeError());
    return 1;
  }
  ClangTool Tool(Options->getCompilations(), Options->getSourcePathList());
  cfglab::addPlatformFlags(Tool);
  detail::AstActionFactory F(CB);
  return Tool.run(&F);
}

} // namespace cglab

#endif // CGLAB_H
