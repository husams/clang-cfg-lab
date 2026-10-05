// p08_mine -- your own call graph, diffed against the library's (Section 8.8).
//
//   p08_mine <file> [--edges=false] [--only=lib|mine|both] [--func=NAME] [--kinds]
//                   [--policy=delete,fnptr,tplpattern,inline,inherited|none]
//                   [--sort=name|source] [--emit=text|dot|json] [--all-files]
//                   [-- <compile flags>]
//
// "mine" is a ContextVisitor over the main file (implicit members and template instantiations
// visited, like CallGraph) that records, for the function it is inside, what CGBuilder
// records plus what CGBuilder leaves out. The library's rules are reproduced exactly:
// CallExpr with a direct callee or a block literal as callee, `new` (operator new), a
// constructor call (only when the constructor has a definition), an Objective-C message to a
// method defined here, the lambda call operator as a node of its own. The differences are
// switched by --policy (all on by default; `none` = the library's rules and nothing else):
//
//   delete      `delete p` is an edge to operator delete and, when the class has a non-trivial
//               destructor, to ~T (CGBuilder has no VisitCXXDeleteExpr)
//   fnptr       a call through a pointer is an edge to the pseudo node "?(<callee type>)", one
//               node per callee type (CGBuilder drops it)
//   tplpattern  calls in a template pattern are recorded under the pattern, a node of kind
//               tpl-pattern (includeInGraph refuses patterns); a call whose callee is an
//               overload set with exactly one function is resolved to it
//   inline      functions whose name starts with __inline are nodes and callees (CGBuilder's
//               includeCalleeInGraph refuses them)
//   inherited   a CXXInheritedCtorInitExpr is an edge to the base-class constructor
//
// A default argument and an in-class initialiser are charged to where they are WRITTEN: the
// function that declares the default (`dflt -> leaf`), the field (`Member::m -> helper`).
// CGBuilder charges them to the user of the default (`use_default -> leaf`) and to the
// constructor (`Member::Member -> helper`), so those edges are only-lib and the written-place
// ones only-mine. Generic lambdas are not walked specially (their call operator is a pattern).
//
// Text output, one record per line (names quoted only when they contain a space):
//   == <file>: lib N nodes M edges, mine N' nodes M' edges
//   node <name> in=lib|mine|both [kind=...]             (--kinds adds kind=; tpl-pattern, field,
//                                                         indirect are kinds of mine-only nodes)
//   edge <caller> -> <callee> both|only-lib|only-mine @L<line>
//   diff: only-lib=<n> only-mine=<n> both=<n>
// An edge is identified by (caller, callee, line, column); the node lines list the union of
// both graphs. --func restricts to one caller; --only filters the node and edge lines.

#include "cglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p08_mine options");
CGLAB_DEFINE_COMMON_FLAGS(Cat)
static llvm::cl::opt<bool> EdgesFlag("edges", llvm::cl::init(true), llvm::cl::cat(Cat),
                                     llvm::cl::desc("print edge lines (--edges=false: nodes only)"));
static llvm::cl::opt<std::string> OnlyFlag("only", llvm::cl::cat(Cat),
                                           llvm::cl::desc("only lib | mine | both lines"));
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::cat(Cat),
                                           llvm::cl::desc("only edges out of this function (printed name)"));
static llvm::cl::opt<bool> KindsFlag("kinds", llvm::cl::cat(Cat), llvm::cl::desc("add kind= to node lines"));
static llvm::cl::opt<std::string> PolicyFlag("policy", llvm::cl::init("delete,fnptr,tplpattern,inline,inherited"),
                                             llvm::cl::cat(Cat),
                                             llvm::cl::desc("what mine records beyond the library: "
                                                            "delete,fnptr,tplpattern,inline,inherited | none"));

namespace {

struct Policy {
  bool Delete = false, FnPtr = false, TplPattern = false, Inline = false, Inherited = false;
};

bool parsePolicy(llvm::StringRef S, Policy &P) {
  llvm::SmallVector<llvm::StringRef, 8> Parts;
  S.split(Parts, ',', -1, /*KeepEmpty=*/false);
  for (llvm::StringRef W : Parts) {
    if (W == "none") continue;
    else if (W == "delete") P.Delete = true;
    else if (W == "fnptr") P.FnPtr = true;
    else if (W == "tplpattern") P.TplPattern = true;
    else if (W == "inline") P.Inline = true;
    else if (W == "inherited") P.Inherited = true;
    else {
      llvm::errs() << "--policy: unknown name '" << W << "' (delete, fnptr, tplpattern, inline, inherited, none)\n";
      return false;
    }
  }
  return true;
}

// The target of an edge: a declaration, or a pseudo node for a call through a pointer.
struct Target {
  const Decl *D = nullptr;
  std::string Pseudo;
};

struct MEdge {
  const Decl *Caller = nullptr;
  Target Callee;
  unsigned Line = 0, Col = 0;
};

const Decl *canon(const Decl *D) { return isa<ObjCMethodDecl>(D) ? D : D->getCanonicalDecl(); }

struct Mine : cglab::ContextVisitor {
  ASTContext &Ctx;
  bool MainOnly;
  Policy P;
  std::vector<MEdge> Edges;
  std::vector<const Decl *> Walked;   // nodes the visitor entered, in traversal order
  std::set<const Decl *> Patterns;    // the Walked ones that are template patterns

  Mine(ASTContext &C, bool Main, Policy Pol) : Ctx(C), MainOnly(Main), P(Pol) {
    ShouldVisitImplicitCode = true;
    ShouldVisitTemplateInstantiations = true;
  }

  bool wanted(const Decl *D) const { return !MainOnly || cglab::inMainFile(Ctx.getSourceManager(), D); }

  bool TraverseDecl(Decl *D) override {
    if (D && isContext(D)) {
      if (!wanted(D)) return true;
      bool Pattern = false;
      if (inDependentContext(D)) {
        if (!P.TplPattern || !isa<FunctionDecl>(D)) return true;
        Pattern = true;
      }
      if (const auto *FD = dyn_cast<FunctionDecl>(D))
        if (!P.Inline && !CallGraph::includeCalleeInGraph(FD)) return true;
      bool Def = isa<BlockDecl>(D);
      if (const auto *FD = dyn_cast<FunctionDecl>(D)) Def = FD->doesThisDeclarationHaveABody();
      else if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) Def = MD->hasBody();
      if (Def) {
        Walked.push_back(canon(D));
        if (Pattern) Patterns.insert(canon(D));
      }
    }
    return ContextVisitor::TraverseDecl(D);
  }

  // charged to the declaration that spells the default: the parameter's default argument is
  // traversed under the function that declares it, an in-class initialiser under the field
  bool TraverseCXXDefaultArgExpr(CXXDefaultArgExpr *) override { return true; }
  bool TraverseCXXDefaultInitExpr(CXXDefaultInitExpr *) override { return true; }

  void add(const Decl *To, const Expr *Site) {
    const Decl *C = cur();
    if (!C || !To) return;
    if (const auto *FD = dyn_cast<FunctionDecl>(To))
      if (!P.Inline && !CallGraph::includeCalleeInGraph(FD)) return;
    addTarget(Target{canon(To), ""}, Site);
  }
  void addTarget(Target T, const Expr *Site) {
    const Decl *C = cur();
    if (!C) return;
    const SourceManager &SM = Ctx.getSourceManager();
    Edges.push_back(MEdge{canon(C), std::move(T), cglab::siteLine(SM, Site), cglab::siteCol(SM, Site)});
  }

  bool VisitCallExpr(CallExpr *CE) override {
    if (!cur()) return true;
    if (const FunctionDecl *FD = CE->getDirectCallee()) {
      add(FD, CE);
      return true;
    }
    const Expr *Callee = CE->getCallee()->IgnoreParenImpCasts();
    if (const auto *BE = dyn_cast<BlockExpr>(Callee)) {
      add(BE->getBlockDecl(), CE);
      return true;
    }
    if (const auto *ULE = dyn_cast<UnresolvedLookupExpr>(Callee)) {
      // a call in a template pattern: resolvable only when the name has one candidate
      if (P.TplPattern && std::distance(ULE->decls_begin(), ULE->decls_end()) == 1)
        if (const auto *FD = dyn_cast<FunctionDecl>((*ULE->decls_begin())->getUnderlyingDecl())) add(FD, CE);
      return true;
    }
    if (Callee->isTypeDependent() || !P.FnPtr) return true;
    QualType T = Callee->getType();
    if (const auto *BO = dyn_cast<BinaryOperator>(Callee))
      if (BO->isPtrMemOp()) T = BO->getRHS()->getType(); // (obj.*pmf)(): the member pointer's type
    addTarget(Target{nullptr, "?(" + cglab::typeName(T, Ctx) + ")"}, CE);
    return true;
  }

  bool VisitCXXNewExpr(CXXNewExpr *E) override {
    if (cur()) add(E->getOperatorNew(), E);
    return true;
  }

  bool VisitCXXConstructExpr(CXXConstructExpr *E) override {
    if (!cur()) return true;
    if (const FunctionDecl *Def = E->getConstructor()->getDefinition()) add(Def, E);
    return true;
  }

  bool VisitCXXDeleteExpr(CXXDeleteExpr *E) override {
    if (!P.Delete || !cur()) return true;
    add(E->getOperatorDelete(), E);
    QualType Destroyed = E->getDestroyedType(); // null for a dependent type that is not a pointer
    if (const CXXRecordDecl *RD = Destroyed.isNull() ? nullptr : Destroyed->getAsCXXRecordDecl())
      if (RD->hasDefinition())
        if (const CXXDestructorDecl *D = RD->getDestructor())
          if (!D->isTrivial()) add(D, E);
    return true;
  }

  bool VisitCXXInheritedCtorInitExpr(CXXInheritedCtorInitExpr *E) override {
    if (P.Inherited && cur()) add(E->getConstructor(), E);
    return true;
  }

  bool VisitObjCMessageExpr(ObjCMessageExpr *ME) override {
    if (!cur()) return true;
    if (ObjCInterfaceDecl *IDecl = ME->getReceiverInterface()) {
      Selector Sel = ME->getSelector();
      const Decl *D = ME->isInstanceMessage() ? IDecl->lookupPrivateMethod(Sel) : IDecl->lookupPrivateClassMethod(Sel);
      if (D) add(D, ME);
    }
    return true;
  }
};

struct NodeInfo {
  bool Lib = false, Mine = false;
  std::string LibKind, MineKind;
  const char *in() const { return Lib && Mine ? "both" : Lib ? "lib" : "mine"; }
  const std::string &kind() const { return Lib ? LibKind : MineKind; }
};

struct Row {
  std::string From, To, Status; // Status: both | only-lib | only-mine
  unsigned Line = 0, Col = 0;
};

void report(ASTContext &Ctx, CallGraph &CG) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) std::exit(2);
  Policy Pol;
  if (!parsePolicy(PolicyFlag, Pol)) std::exit(2);
  if (!OnlyFlag.empty() && OnlyFlag != "lib" && OnlyFlag != "mine" && OnlyFlag != "both") {
    llvm::errs() << "--only must be lib, mine or both\n";
    std::exit(2);
  }

  cglab::Graph G(CG, Ctx, cgGraphOptions());
  Mine V(Ctx, !CgAllFilesFlag, Pol);
  V.TraverseDecl(Ctx.getTranslationUnitDecl());

  // ---- names: the graph's where it has a node, the naming rule's otherwise; two different
  // declarations that still print alike are told apart by their signature
  std::map<std::string, std::set<const Decl *>> Groups; // name -> declarations (size only)
  auto note = [&](const Decl *D) {
    if (D && !G.find(D)) Groups[cglab::baseName(D)].insert(D);
  };
  for (const Decl *D : V.Walked) note(D);
  for (const MEdge &E : V.Edges) {
    note(E.Caller);
    note(E.Callee.D);
  }
  auto nameOf = [&](const Decl *D) {
    if (std::optional<unsigned> Id = G.find(D)) return G.node(*Id).Name;
    std::string B = cglab::baseName(D);
    return Groups[B].size() > 1 ? B + cglab::signatureOf(D) : B;
  };
  auto kindOf = [&](const Decl *D) -> std::string {
    if (V.Patterns.count(D)) return "tpl-pattern";
    if (isa<FieldDecl>(D)) return "field";
    return cglab::kindName(cglab::classify(D));
  };
  auto targetName = [&](const Target &T) { return T.D ? nameOf(T.D) : T.Pseudo; };

  // ---- nodes: the union
  std::map<std::string, NodeInfo> Nodes;
  for (unsigned Id : G.order()) {
    NodeInfo &N = Nodes[G.node(Id).Name];
    N.Lib = true;
    N.LibKind = cglab::kindName(G.node(Id).K);
  }
  size_t MineNodes = 0;
  auto mineNode = [&](const std::string &Name, const std::string &Kind) {
    NodeInfo &N = Nodes[Name];
    if (!N.Mine) ++MineNodes;
    N.Mine = true;
    if (N.MineKind.empty()) N.MineKind = Kind;
  };
  for (const Decl *D : V.Walked) mineNode(nameOf(D), kindOf(D));
  for (const MEdge &E : V.Edges) {
    mineNode(nameOf(E.Caller), kindOf(E.Caller));
    mineNode(targetName(E.Callee), E.Callee.D ? kindOf(E.Callee.D) : "indirect");
  }

  // ---- edges: both graphs keyed by (caller, callee, line, column)
  using Key = std::tuple<std::string, std::string, unsigned, unsigned>;
  std::map<Key, std::pair<unsigned, unsigned>> Count; // key -> (lib, mine)
  for (const cglab::Edge &E : G.edges())
    if (!E.Root) ++Count[{G.node(E.From).Name, G.node(E.To).Name, E.Line, E.Col}].first;
  for (const MEdge &E : V.Edges) ++Count[{nameOf(E.Caller), targetName(E.Callee), E.Line, E.Col}].second;
  std::vector<Row> Rows;
  size_t NumBoth = 0, NumLib = 0, NumMine = 0;
  for (const auto &[K, C] : Count) {
    auto [From, To, Line, Col] = K;
    if (!FuncFlag.empty() && From != FuncFlag) continue;
    unsigned Both = std::min(C.first, C.second);
    for (unsigned I = 0; I < Both; ++I) Rows.push_back({From, To, "both", Line, Col});
    for (unsigned I = Both; I < C.first; ++I) Rows.push_back({From, To, "only-lib", Line, Col});
    for (unsigned I = Both; I < C.second; ++I) Rows.push_back({From, To, "only-mine", Line, Col});
    NumBoth += Both;
    NumLib += C.first - Both;
    NumMine += C.second - Both;
  }
  if (!FuncFlag.empty() && !Nodes.count(FuncFlag)) {
    llvm::errs() << "no node named '" << FuncFlag << "' in " << G.file() << "\n";
    std::exit(2);
  }
  if (Sort == cglab::SortKey::Source)
    std::stable_sort(Rows.begin(), Rows.end(), [](const Row &A, const Row &B) {
      return std::tie(A.Line, A.Col, A.From, A.To) < std::tie(B.Line, B.Col, B.From, B.To);
    });
  auto wantRow = [&](const Row &R) {
    return OnlyFlag.empty() || R.Status == (OnlyFlag == "lib" ? "only-lib" : OnlyFlag == "mine" ? "only-mine" : "both");
  };
  auto wantNode = [&](const std::string &Name, const NodeInfo &N) {
    if (!FuncFlag.empty() && Name != FuncFlag) return false;
    return OnlyFlag.empty() || OnlyFlag == N.in();
  };

  if (Emit == cglab::EmitKind::Json) {
    llvm::json::Array Ns, Es;
    for (const auto &[Name, N] : Nodes)
      if (wantNode(Name, N)) Ns.push_back(llvm::json::Object{{"name", Name}, {"in", N.in()}, {"kind", N.kind()}});
    for (const Row &R : Rows)
      if (wantRow(R))
        Es.push_back(llvm::json::Object{{"from", R.From}, {"to", R.To}, {"status", R.Status},
                                        {"line", R.Line}, {"col", R.Col}});
    llvm::json::Object J{{"file", G.file()}, {"nodes", std::move(Ns)}, {"edges", std::move(Es)},
                         {"diff", llvm::json::Object{{"only-lib", (int64_t)NumLib},
                                                     {"only-mine", (int64_t)NumMine},
                                                     {"both", (int64_t)NumBoth}}}};
    llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(J))) << "\n";
    return;
  }
  if (Emit == cglab::EmitKind::Dot) {
    // both edges plain, only-mine hl, only-lib weak; an edge to a "?(type)" node is indirect
    cglab::DotWriter W(llvm::outs(), cglab::mainFileStem(G.sm()));
    std::set<std::string> Used;
    for (const Row &R : Rows)
      if (wantRow(R)) {
        Used.insert(R.From);
        Used.insert(R.To);
      }
    for (const auto &[Name, N] : Nodes) {
      if (!Used.count(Name)) continue;
      cglab::DotAttrs A;
      if (N.kind() == "indirect") A.addClass("dim");
      else if (!N.Lib) A.addClass("hl");
      W.node(Name, A);
    }
    std::map<std::tuple<std::string, std::string, std::string>, unsigned> Folded;
    for (const Row &R : Rows)
      if (wantRow(R)) ++Folded[{R.From, R.To, R.Status}];
    for (const auto &[K, N] : Folded) {
      const auto &[From, To, Status] = K;
      cglab::DotAttrs A;
      if (To.rfind("?(", 0) == 0) A.addClass("indirect");
      if (Status == "only-mine") A.addClass("hl");
      if (Status == "only-lib") A.addClass("weak");
      if (N > 1) A.Label = "\xC3\x97" + std::to_string(N);
      W.edge(From, To, A);
    }
    W.finish();
    return;
  }

  llvm::outs() << "== " << G.file() << ": lib " << G.numNodes() << " nodes " << G.numEdges() << " edges, mine "
               << MineNodes << " nodes " << V.Edges.size() << " edges\n";
  for (const auto &[Name, N] : Nodes) {
    if (!wantNode(Name, N)) continue;
    llvm::outs() << "node " << cglab::quoteName(Name) << " in=" << N.in();
    if (KindsFlag) llvm::outs() << " kind=" << N.kind();
    llvm::outs() << "\n";
  }
  if (EdgesFlag) {
    for (const Row &R : Rows)
      if (wantRow(R))
        llvm::outs() << "edge " << cglab::quoteName(R.From) << " -> " << cglab::quoteName(R.To) << " " << R.Status
                     << " @L" << R.Line << "\n";
    llvm::outs() << "diff: only-lib=" << NumLib << " only-mine=" << NumMine << " both=" << NumBoth << "\n";
  }
}

} // namespace

int main(int argc, const char **argv) { return cglab::runPerTU(argc, argv, Cat, report); }
