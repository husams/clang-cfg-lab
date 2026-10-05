// p11_index -- the call edges clang::index reports, next to the CallGraph's (Part 11.4).
//
//   p11_index FILE [--diff] [--refs] [--func=NAME] [--implicit-instantiations=0|1]
//                  [--emit=text|dot|json] [--sort=] [--all-files] [-- <flags>]
//
// clang::index (clang/Index/IndexingAction.h) walks the AST and hands every symbol occurrence
// to an IndexDataConsumer: handleDeclOccurrence(Decl, roles, relations, location). A call is an
// occurrence of the callee with SymbolRole::Call and a relation RelationCalledBy that names the
// caller. That is a second source of call edges, with its own inclusion policy: it keeps what
// the CallGraph drops (`__inline*` names, template patterns, a call through a pointer, which is
// a Call of the *variable*) and charges calls the other way (a lambda body or a block to the
// enclosing function; a default argument or a default member initialiser to its container).
//
// Output, one record per line (names as in cglab.h; a name with a space is quoted):
//   == <file>: <n> call occurrences, <r> references
//   edge <caller> -> <callee> @L<line> roles=<Call[,Dyn][,Impl][,NoRelCall]>
//   ref <container> -> <fn> @L<line>                                  (--refs)
//   edge <caller> -> <callee> @L<line> [roles=...] both|only-index|only-graph     (--diff)
//   diff: only-index=<n> only-graph=<n> both=<n>                                  (--diff)
//
// roles: Call is always there; Dyn = SymbolRole::Dynamic (a virtual call); Impl = Implicit;
// NoRelCall = the occurrence has no RelationCalledBy relation (a default argument or a
// default member initialiser): the caller shown is its container, the nearest symbol that
// holds it. A call through a pointer prints the pointer, not a function: `<name>@param` for a
// parameter, `<name>@var` for any other variable or field. `ref` lists a function that is
// *mentioned but not called* (its address is taken: SymbolRole::Reference without Call), under
// the symbol that holds the mention.
//
// The indexer reports one occurrence per place a symbol is walked, and a few places are walked
// twice (a constructor call under the variable it initialises and again under the function;
// a lambda body under the variable and again under the enclosing function). Occurrences are
// deduplicated by (caller, callee, line, column); n and r count what is printed.
//
// --diff compares with the CallGraph by (caller, callee, line): `both`, `only-index` (the
// indexer has it, the graph does not), `only-graph`. Lines are matched as a multiset, so two
// calls to the same callee on one line are two edges. --implicit-instantiations=1 also indexes
// template instantiations (IndexingOptions::IndexImplicitInstantiation; default 0, the
// library's default): the pattern's calls are then reported under both `twice` and `twice<int>`.
// <root> has no meaning here: --with-root is accepted and ignored.
//
// --emit=dot classes: a `Dyn` edge `virtual`; a call through a pointer `indirect` to a `dim`
// node; `ref` edges `weak`; with --diff `only-index` edges `hl` and `only-graph` edges `dim`.

#include "cglab.h"

#include "clang/Index/IndexDataConsumer.h"
#include "clang/Index/IndexSymbol.h"
#include "clang/Index/IndexingAction.h"
#include "clang/Index/IndexingOptions.h"

#include <map>
#include <tuple>

using namespace clang;

static llvm::cl::OptionCategory Cat("p11_index options");
CGLAB_DEFINE_COMMON_FLAGS(Cat)
static llvm::cl::opt<bool> OptDiff("diff", llvm::cl::cat(Cat), llvm::cl::desc("compare with the CallGraph's edges"));
static llvm::cl::opt<bool> OptRefs("refs", llvm::cl::cat(Cat),
                                   llvm::cl::desc("also list functions that are mentioned but not called"));
static llvm::cl::opt<std::string> OptFunc("func", llvm::cl::cat(Cat),
                                          llvm::cl::desc("only edges and references under this caller or container"));
static llvm::cl::opt<unsigned> OptImplicit("implicit-instantiations", llvm::cl::init(0), llvm::cl::cat(Cat),
                                           llvm::cl::desc("0|1: index template instantiations too"));

namespace {

// ---------------------------------------------------------------------------
// What the indexer reported
// ---------------------------------------------------------------------------

struct Occ {
  std::string Caller, Callee;
  unsigned Line = 0, Col = 0;
  bool Dyn = false, Impl = false, NoRelCall = false;
  bool Pointer = false; // the callee is a variable: a call through a pointer
  bool Param = false;   // ... and that variable is a parameter
};

using OccKey = std::tuple<std::string, std::string, unsigned, unsigned>; // caller, callee, line, column

struct RawOcc {
  const Decl *Callee, *Caller;
  index::SymbolRoleSet Roles;
  bool RelCall;
  unsigned Line, Col;
};

struct Collector : index::IndexDataConsumer {
  ASTContext *Ctx = nullptr;
  std::vector<RawOcc> Calls, Refs;
  std::function<void(Collector &)> Report;

  void initialize(ASTContext &C) override { Ctx = &C; }

  bool handleDeclOccurrence(const Decl *D, index::SymbolRoleSet Roles, ArrayRef<index::SymbolRelation> Rels,
                            SourceLocation Loc, ASTNodeInfo) override {
    const SourceManager &SM = Ctx->getSourceManager();
    if (!CgAllFilesFlag && !SM.isInMainFile(SM.getExpansionLoc(Loc))) return true;
    bool IsCall = Roles & static_cast<unsigned>(index::SymbolRole::Call);
    bool IsRef = Roles & static_cast<unsigned>(index::SymbolRole::Reference);
    if (!IsCall && !(IsRef && isFunction(D))) return true;
    const Decl *Caller = nullptr;
    bool RelCall = false;
    for (const index::SymbolRelation &R : Rels)
      if (R.Roles & static_cast<unsigned>(index::SymbolRole::RelationCalledBy)) {
        Caller = R.RelatedSymbol;
        RelCall = true;
        break;
      }
    if (!Caller)
      for (const index::SymbolRelation &R : Rels)
        if (R.Roles & static_cast<unsigned>(index::SymbolRole::RelationContainedBy)) {
          Caller = R.RelatedSymbol;
          break;
        }
    RawOcc O{D, Caller, Roles, RelCall, SM.getSpellingLineNumber(Loc), SM.getSpellingColumnNumber(Loc)};
    (IsCall ? Calls : Refs).push_back(O);
    return true;
  }

  void finish() override { Report(*this); }

  static bool isFunction(const Decl *D) {
    return isa<FunctionDecl>(D) || isa<FunctionTemplateDecl>(D) || isa<ObjCMethodDecl>(D);
  }
};

// The name the lab uses for a declaration: the call graph's when it has a node (so edges
// compare), else the plain qualified name (a template pattern, a variable, a field).
std::string nameOf(const cglab::Graph &G, const Decl *D) {
  if (!D) return "<global>";
  if (const auto *FT = dyn_cast<FunctionTemplateDecl>(D)) D = FT->getTemplatedDecl();
  if (auto Id = G.find(D)) return G.node(*Id).Name;
  return cglab::baseName(D);
}

Occ makeOcc(const cglab::Graph &G, const RawOcc &R) {
  Occ O;
  O.Caller = nameOf(G, R.Caller);
  O.Callee = nameOf(G, R.Callee);
  O.Line = R.Line;
  O.Col = R.Col;
  O.Dyn = R.Roles & static_cast<unsigned>(index::SymbolRole::Dynamic);
  O.Impl = R.Roles & static_cast<unsigned>(index::SymbolRole::Implicit);
  O.NoRelCall = !R.RelCall;
  if (!Collector::isFunction(R.Callee)) {
    O.Pointer = true;
    O.Param = isa<ParmVarDecl>(R.Callee);
    O.Callee += O.Param ? "@param" : "@var";
  }
  return O;
}

// Deduplicate by (caller, callee, line, column); flags of the duplicates are OR-ed.
std::vector<Occ> dedupe(const cglab::Graph &G, const std::vector<RawOcc> &Raw, bool Calls) {
  std::map<OccKey, Occ> Seen;
  for (const RawOcc &R : Raw) {
    if (Calls && !Collector::isFunction(R.Callee) && !isa<VarDecl>(R.Callee) && !isa<FieldDecl>(R.Callee) &&
        !isa<BindingDecl>(R.Callee) && !isa<IndirectFieldDecl>(R.Callee))
      continue; // a Call of something that is neither a function nor a variable
    Occ O = makeOcc(G, R);
    if (!OptFunc.empty() && O.Caller != OptFunc) continue;
    auto [It, New] = Seen.emplace(OccKey{O.Caller, O.Callee, O.Line, O.Col}, O);
    if (!New) {
      It->second.Dyn |= O.Dyn;
      It->second.Impl |= O.Impl;
      It->second.NoRelCall &= O.NoRelCall; // a relation on any duplicate counts
    }
  }
  std::vector<Occ> V;
  for (auto &KV : Seen) V.push_back(std::move(KV.second));
  return V;
}

// ---------------------------------------------------------------------------
// Rows: what is printed, whichever format
// ---------------------------------------------------------------------------

enum class Side { Index, Both, GraphOnly };

struct Row {
  Occ O;
  Side S = Side::Index;
  unsigned Rpo = 0; // the caller's reverse post-order index in the CallGraph (sort=rpo)
};

const char *sideName(Side S) {
  switch (S) {
  case Side::Index: return "only-index";
  case Side::Both: return "both";
  case Side::GraphOnly: return "only-graph";
  }
  return "?";
}

std::string rolesText(const Occ &O) {
  std::string S = "Call";
  if (O.Dyn) S += ",Dyn";
  if (O.Impl) S += ",Impl";
  if (O.NoRelCall) S += ",NoRelCall";
  return S;
}

void sortRows(std::vector<Row> &Rows, cglab::SortKey K) {
  std::stable_sort(Rows.begin(), Rows.end(), [K](const Row &A, const Row &B) {
    const Occ &X = A.O, &Y = B.O;
    switch (K) {
    case cglab::SortKey::Source:
      return std::tie(X.Line, X.Col, X.Caller, X.Callee) < std::tie(Y.Line, Y.Col, Y.Caller, Y.Callee);
    case cglab::SortKey::Rpo:
      return std::tie(A.Rpo, X.Caller, X.Callee, X.Line, X.Col) < std::tie(B.Rpo, Y.Caller, Y.Callee, Y.Line, Y.Col);
    case cglab::SortKey::Name:
      break;
    }
    return std::tie(X.Caller, X.Callee, X.Line, X.Col) < std::tie(Y.Caller, Y.Callee, Y.Line, Y.Col);
  });
}

// ---------------------------------------------------------------------------
// The comparison with the CallGraph
// ---------------------------------------------------------------------------

std::vector<Row> compare(const cglab::Graph &G, const std::vector<Occ> &Index) {
  using Key = std::tuple<std::string, std::string, unsigned>; // caller, callee, line
  std::map<Key, std::vector<unsigned>> Graph;                  // edge indices, to consume
  std::vector<Occ> GraphOccs;
  for (const cglab::Edge &E : G.edges()) {
    if (E.Root || !E.Line) continue;
    Occ O;
    O.Caller = G.node(E.From).Name;
    O.Callee = G.node(E.To).Name;
    O.Line = E.Line;
    O.Col = E.Col;
    if (!OptFunc.empty() && O.Caller != OptFunc) continue;
    Graph[{O.Caller, O.Callee, O.Line}].push_back(GraphOccs.size());
    GraphOccs.push_back(std::move(O));
  }
  std::vector<Row> Rows;
  for (const Occ &O : Index) {
    Row R{O, Side::Index, 0};
    auto It = Graph.find({O.Caller, O.Callee, O.Line});
    if (It != Graph.end() && !It->second.empty()) {
      It->second.pop_back();
      R.S = Side::Both;
    }
    Rows.push_back(std::move(R));
  }
  for (auto &[K, Left] : Graph)
    for (unsigned I : Left) {
      Occ O = GraphOccs[I];
      O.NoRelCall = false; // the graph has no roles
      Rows.push_back({O, Side::GraphOnly, 0});
    }
  return Rows;
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

struct Result {
  std::string File;
  std::vector<Row> Edges;
  std::vector<Row> Refs;
  unsigned OnlyIndex = 0, OnlyGraph = 0, Both = 0;
};

void emitText(const Result &R) {
  llvm::outs() << "== " << R.File << ": " << R.Edges.size() - R.OnlyGraph << " call occurrences, " << R.Refs.size()
               << " references\n";
  for (const Row &Rw : R.Edges) {
    const Occ &O = Rw.O;
    llvm::outs() << "edge " << cglab::quoteName(O.Caller) << " -> " << cglab::quoteName(O.Callee) << " @L" << O.Line;
    if (Rw.S != Side::GraphOnly) llvm::outs() << " roles=" << rolesText(O);
    if (OptDiff) llvm::outs() << " " << sideName(Rw.S);
    llvm::outs() << "\n";
  }
  for (const Row &Rw : R.Refs)
    llvm::outs() << "ref " << cglab::quoteName(Rw.O.Caller) << " -> " << cglab::quoteName(Rw.O.Callee) << " @L"
                 << Rw.O.Line << "\n";
  if (OptDiff)
    llvm::outs() << "diff: only-index=" << R.OnlyIndex << " only-graph=" << R.OnlyGraph << " both=" << R.Both << "\n";
}

void emitDot(const Result &R, const cglab::Graph &G) {
  cglab::DotWriter W(llvm::outs(), llvm::sys::path::stem(R.File));
  std::map<std::string, bool> Nodes; // name -> is a pointer variable
  auto Note = [&](const Occ &O) {
    Nodes.emplace(O.Caller, false);
    Nodes[O.Callee] = Nodes[O.Callee] || O.Pointer;
  };
  for (const Row &Rw : R.Edges) Note(Rw.O);
  for (const Row &Rw : R.Refs) Note(Rw.O);
  for (auto &[Name, Ptr] : Nodes) {
    cglab::DotAttrs A;
    if (Name == "main") A.addClass("entry");
    if (Ptr) A.addClass("dim");
    else if (auto Id = G.find(llvm::StringRef(Name)))
      if (G.node(*Id).K == cglab::Kind::Decl) A.addClass("dim");
    W.node(Name, A);
  }
  // one drawn edge per (caller, callee, class); the lines of parallel calls go in the label
  struct Drawn {
    std::string Label;
    unsigned Count = 0;
  };
  std::map<std::tuple<std::string, std::string, std::string>, Drawn> Edges; // + classes as the key's third part
  std::vector<std::tuple<std::string, std::string, std::string>> Order;
  for (const Row &Rw : R.Edges) {
    const Occ &O = Rw.O;
    std::string Classes;
    auto Add = [&](const char *C) { Classes += (Classes.empty() ? "" : " ") + std::string(C); };
    if (O.Pointer) Add("indirect");
    if (O.Dyn) Add("virtual");
    if (Rw.S == Side::Index && OptDiff) Add("hl");
    if (Rw.S == Side::GraphOnly) Add("dim");
    auto Key = std::make_tuple(O.Caller, O.Callee, Classes);
    Drawn &D = Edges[Key];
    if (!D.Count++) Order.push_back(Key);
    D.Label += (D.Label.empty() ? "@L" : ",@L") + std::to_string(O.Line);
  }
  for (const auto &Key : Order) {
    cglab::DotAttrs A;
    A.Label = Edges[Key].Label;
    std::string C = std::get<2>(Key);
    for (size_t P = 0; !C.empty();) {
      P = C.find(' ');
      A.addClass(C.substr(0, P));
      if (P == std::string::npos) break;
      C = C.substr(P + 1);
    }
    W.edge(std::get<0>(Key), std::get<1>(Key), A);
  }
  for (const Row &Rw : R.Refs) {
    cglab::DotAttrs A;
    A.Label = "ref @L" + std::to_string(Rw.O.Line);
    A.addClass("weak");
    W.edge(Rw.O.Caller, Rw.O.Callee, A);
  }
  W.finish();
}

void emitJson(const Result &R) {
  auto Obj = [](const Row &Rw) {
    const Occ &O = Rw.O;
    llvm::json::Array Roles;
    if (Rw.S != Side::GraphOnly) {
      Roles.push_back("Call");
      if (O.Dyn) Roles.push_back("Dyn");
      if (O.Impl) Roles.push_back("Impl");
      if (O.NoRelCall) Roles.push_back("NoRelCall");
    }
    llvm::json::Object J{{"caller", O.Caller}, {"callee", O.Callee}, {"line", O.Line}, {"col", O.Col},
                         {"roles", std::move(Roles)}, {"pointer", O.Pointer}};
    if (OptDiff) J["class"] = sideName(Rw.S);
    return J;
  };
  llvm::json::Array Es, Rs;
  for (const Row &Rw : R.Edges) Es.push_back(Obj(Rw));
  for (const Row &Rw : R.Refs) Rs.push_back(Obj(Rw));
  llvm::json::Object Doc{{"file", R.File}, {"edges", std::move(Es)}, {"refs", std::move(Rs)}};
  if (OptDiff)
    Doc["diff"] = llvm::json::Object{{"onlyIndex", R.OnlyIndex}, {"onlyGraph", R.OnlyGraph}, {"both", R.Both}};
  llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(Doc))) << "\n";
}

void report(Collector &C) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) std::exit(2);
  ASTContext &Ctx = *C.Ctx;
  std::unique_ptr<CallGraph> CG = cglab::buildCallGraph(Ctx);
  cglab::Graph G(*CG, Ctx, cgGraphOptions());

  Result R;
  R.File = G.file();
  std::vector<Occ> Calls = dedupe(G, C.Calls, /*Calls=*/true), Refs = dedupe(G, C.Refs, /*Calls=*/false);
  if (OptDiff) R.Edges = compare(G, Calls);
  else
    for (const Occ &O : Calls) R.Edges.push_back({O, Side::Index, 0});
  for (const Occ &O : Refs) R.Refs.push_back({O, Side::Index, 0});
  for (Row &Rw : R.Edges) {
    if (auto Id = G.find(llvm::StringRef(Rw.O.Caller))) Rw.Rpo = G.node(*Id).Rpo;
    else Rw.Rpo = ~0u;
    R.OnlyIndex += Rw.S == Side::Index;
    R.OnlyGraph += Rw.S == Side::GraphOnly;
    R.Both += Rw.S == Side::Both;
  }
  sortRows(R.Edges, Sort);
  sortRows(R.Refs, Sort);

  if (Emit == cglab::EmitKind::Dot) emitDot(R, G);
  else if (Emit == cglab::EmitKind::Json) emitJson(R);
  else emitText(R);
}

struct Factory : tooling::FrontendActionFactory {
  std::unique_ptr<FrontendAction> create() override {
    auto C = std::make_shared<Collector>();
    C->Report = report;
    index::IndexingOptions Opts;
    Opts.IndexFunctionLocals = true; // a call through a local pointer or block refers to a local
    Opts.IndexImplicitInstantiation = OptImplicit != 0;
    Opts.IndexMacros = false;
    return index::createIndexingAction(C, Opts);
  }
};

} // namespace

int main(int argc, const char **argv) {
  std::vector<std::string> Storage;
  auto Args = cfglab::withDefaultCompileFlags(argc, argv, Storage);
  int N = static_cast<int>(Args.size());
  auto Options = tooling::CommonOptionsParser::create(N, Args.data(), Cat);
  if (!Options) {
    llvm::errs() << llvm::toString(Options.takeError());
    return 1;
  }
  if (OptImplicit > 1) {
    llvm::errs() << "p11_index: --implicit-instantiations must be 0 or 1\n";
    return 2;
  }
  tooling::ClangTool Tool(Options->getCompilations(), Options->getSourcePathList());
  cfglab::addPlatformFlags(Tool);
  Factory F;
  return Tool.run(&F);
}
