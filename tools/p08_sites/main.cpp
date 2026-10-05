// p08_sites -- the call sites in a function's CFG, joined with the call graph (Part 8.7).
//
//   p08_sites <file> [--preset=..] [--set=..] [--clear=..] [--always-add=..] [--func=NAME]
//                    [--walk=NAME [--depth=N]] [--unresolved] [--succs] [--sort=..] [--emit=text|dot|json]
//
// The call graph says that f may call g; the CFG says where (block, element) and which calls
// the graph has no edge for. Text output, one record per line:
//   == <fn>: N blocks, S sites
//   site <fn> B<id>.<i> <AnyCall kind> <callee|?> <class>      (B<id>.<i> is the dump's [B3.2])
//   succ <fn> B<id> -> B<id> [T|F]                              (--succs: the CFG edges, T/F on a two-way branch)
//   walk <depth> <fn>:B<id> [-> <callee>]                      (--walk)
// <class> is one of
//   resolved                  the graph has an edge for this very call expression, and the callee has a body
//   virtual static=<X>        a virtual call: the graph records only the static callee X
//   missing:indirect          no callee is known (function pointer, block variable): callee is ?
//   missing:implicit-dtor     an implicit destructor call (AddImplicitDtors / AddTemporaryDtors)
//   missing:delete            what `delete` calls: operator delete, and the destructor (DeleteDtor)
//   missing:decl              the callee is known but has no body in this translation unit
//   missing:node              a callee with a body that the graph leaves out altogether (a name starting with __inline)
//   missing:edge              a known callee with a body and a node, yet no edge for this expression (should not happen)

#include "cglab.h"

#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/AnyCall.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p08_sites options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat) // --preset --set --clear --always-add --func
CGLAB_DEFINE_COMMON_FLAGS(Cat) // --sort --emit --with-root --all-files
static llvm::cl::opt<std::string> WalkOpt("walk", llvm::cl::cat(Cat),
                                          llvm::cl::desc("start an interprocedural walk at this function"));
static llvm::cl::opt<unsigned> DepthOpt("depth", llvm::cl::init(2), llvm::cl::cat(Cat),
                                        llvm::cl::desc("--walk: how many calls deep to descend"));
static llvm::cl::opt<bool> SuccsOpt("succs", llvm::cl::cat(Cat),
                                    llvm::cl::desc("also print each block's successors (succ lines)"));
static llvm::cl::opt<bool> UnresolvedOpt("unresolved", llvm::cl::cat(Cat),
                                         llvm::cl::desc("only the sites that are not `resolved`"));

namespace {

int Status = 0;

const char *callKindName(AnyCall::Kind K) {
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

struct Site {
  unsigned Block = 0, Index = 0;  // Index counts from 1, like the elements in a CFG dump
  AnyCall::Kind Kind = AnyCall::Function;
  const Decl *Callee = nullptr;   // null when unknown
  const CallGraphNode *Node = nullptr; // the graph's callee for this very expression, if it has an edge
  std::string Name = "?";         // callee's printed name; "?" when unknown (also for a virtual call)
  std::string Class;              // resolved | missing:...; "virtual" for a virtual call
  std::string Static;             // virtual: the static callee the graph recorded
  bool Resolved = false;          // the walk descends into the callee
  // The class as printed: a virtual call carries its static callee.
  std::string classText() const {
    return Class == "virtual" ? "virtual static=" + cglab::quoteName(Static) : Class;
  }
};

struct FnInfo {
  const cglab::Node *N = nullptr;
  const Decl *Def = nullptr;
  AnalysisDeclContext *AC = nullptr;
  CFG *G = nullptr;
  std::vector<Site> Sites;        // decreasing block id, then element order: the dump's order
};

struct WalkRec {
  unsigned Depth = 0;
  const FnInfo *Fn = nullptr;
  unsigned Block = 0;
  std::string Callee;             // raw printed name; empty for a block line
};

class Tool {
public:
  Tool(ASTContext &C, CallGraph &CG)
      : Ctx(C), G(CG, C, cgGraphOptions()), Mgr(C) {}

  bool setup() {
    if (!cgCommonFlags(Sort, Emit)) return false;
    if (!flagsToOptions(BO)) return false;
    return true;
  }

  void run() {
    std::vector<const FnInfo *> Shown;
    if (!WalkOpt.empty()) {
      std::optional<unsigned> Start = findFunction(WalkOpt);
      const FnInfo *F = Start ? info(G.node(*Start)) : nullptr;
      if (!F) {
        llvm::errs() << "--walk: no function '" << WalkOpt << "' with a body\n";
        Status = 2;
        return;
      }
      walk(*F, 0);
      for (const WalkRec &W : Walks) addUnique(Shown, W.Fn);
    } else {
      for (unsigned Id : G.order(Sort))
        if (wants(G.node(Id)))
          if (const FnInfo *F = info(G.node(Id))) Shown.push_back(F);
      if (!FuncFlag.empty() && Shown.empty()) {
        llvm::errs() << "--func: no function '" << FuncFlag << "' with a body\n";
        Status = 2;
        return;
      }
    }
    switch (Emit) {
    case cglab::EmitKind::Text: text(Shown); break;
    case cglab::EmitKind::Dot: dot(Shown); break;
    case cglab::EmitKind::Json: json(Shown); break;
    }
  }

private:
  ASTContext &Ctx;
  cglab::Graph G;
  AnalysisDeclContextManager Mgr;
  CFG::BuildOptions BO;
  cglab::SortKey Sort = cglab::SortKey::Name;
  cglab::EmitKind Emit = cglab::EmitKind::Text;
  std::map<unsigned, std::unique_ptr<FnInfo>> Infos; // by graph id
  std::vector<WalkRec> Walks;

  static void addUnique(std::vector<const FnInfo *> &V, const FnInfo *F) {
    if (std::find(V.begin(), V.end(), F) == V.end()) V.push_back(F);
  }

  // ---- which functions -------------------------------------------------------

  std::optional<unsigned> findFunction(llvm::StringRef Name) const {
    if (auto Id = G.find(Name)) return Id;
    for (const cglab::Node &N : G.nodes())
      if (const auto *FD = N.Best ? dyn_cast<FunctionDecl>(N.Best) : nullptr)
        if (FD->getQualifiedNameAsString() == Name) return N.Id;
    return std::nullopt;
  }

  // Everything with a body of its own, except implicit members unless they are asked for by name.
  bool wants(const cglab::Node &N) const {
    if (N.Id == 0 || !N.Best || !N.Best->hasBody()) return false;
    if (!FuncFlag.empty()) {
      if (N.Name == FuncFlag) return true;
      const auto *FD = dyn_cast<FunctionDecl>(N.Best);
      return FD && flagWantsFunction(FD);
    }
    return N.K != cglab::Kind::Implicit && N.K != cglab::Kind::Decl;
  }

  // ---- per-function facts -------------------------------------------------------

  const FnInfo *info(const cglab::Node &N) {
    auto It = Infos.find(N.Id);
    if (It != Infos.end()) return It->second.get();
    auto F = std::make_unique<FnInfo>();
    F->N = &N;
    F->Def = N.Best;
    F->AC = N.Best ? Mgr.getContext(N.Best) : nullptr;
    if (F->AC) {
      // Must happen before the first getCFG(): the options are read once.
      cfglab::applyPreset(F->AC->getCFGBuildOptions(), BO);
      F->G = F->AC->getCFG();
    }
    if (F->G) collect(*F);
    const FnInfo *P = F.get();
    Infos[N.Id] = std::move(F);
    return P->G ? P : nullptr;
  }

  // Function nodes are keyed by their canonical declaration, Objective-C method nodes by the
  // implementation (whose getCanonicalDecl() is the @interface declaration): try both.
  const CallGraphNode *nodeFor(const Decl *D) const {
    if (const CallGraphNode *N = G.callGraph().getNode(D)) return N;
    return G.callGraph().getNode(D->getCanonicalDecl());
  }

  // The printed name, unquoted: the same name p08_nodes prints for a declaration that is in the graph.
  std::string calleeName(const Decl *D) const {
    if (const CallGraphNode *N = nodeFor(D))
      if (auto Id = G.find(N)) return G.node(*Id).Name;
    return cglab::baseName(D);
  }

  // A virtual call goes through the vtable unless the member is named with a qualifier (b.Base::run()).
  static bool dispatchesVirtually(const Expr *E, const Decl *D) {
    const auto *MD = dyn_cast_or_null<CXXMethodDecl>(D);
    const auto *CE = dyn_cast<CallExpr>(E);
    if (!MD || !CE || !MD->isVirtual()) return false;
    if (const auto *ME = dyn_cast<MemberExpr>(CE->getCallee()->IgnoreParenImpCasts()))
      if (ME->hasQualifier()) return false;
    return true;
  }

  using EdgeMap = std::map<const Expr *, const CallGraphNode *>;

  Site classifyCall(const Expr *E, const AnyCall &AC, const EdgeMap &Edges) const {
    Site S;
    S.Kind = AC.getKind();
    const Decl *D = AC.getDecl();
    auto It = Edges.find(E);
    if (It != Edges.end()) S.Node = It->second;
    if (S.Kind == AnyCall::Deallocator) {
      S.Callee = D;
      if (D) S.Name = calleeName(D);
      S.Class = "missing:delete";
    } else if (!D) {
      S.Class = "missing:indirect";
    } else if (dispatchesVirtually(E, D)) {
      S.Class = "virtual";
      S.Static = calleeName(D);
    } else if (S.Node) {
      // The graph's own answer: the edge for this expression and the node it points at.
      S.Callee = S.Node->getDecl();
      S.Name = calleeName(S.Callee);
      S.Class = S.Callee->hasBody() ? "resolved" : "missing:decl";
      S.Resolved = S.Callee->hasBody();
    } else {
      S.Callee = D;
      S.Name = calleeName(D);
      if (!D->hasBody()) S.Class = "missing:decl";
      else if (!nodeFor(D)) S.Class = "missing:node";
      else S.Class = "missing:edge";
    }
    return S;
  }

  void collect(FnInfo &F) {
    // The call expressions the graph recorded for this function: an edge for *this* site, not just for the callee.
    EdgeMap Edges;
    for (const CallGraphNode::CallRecord &CR : F.N->CGN->callees())
      if (CR.CallExpr) Edges[CR.CallExpr] = CR.Callee;

    std::vector<const CFGBlock *> Blocks(F.G->begin(), F.G->end());
    std::sort(Blocks.begin(), Blocks.end(),
              [](const CFGBlock *A, const CFGBlock *B) { return A->getBlockID() > B->getBlockID(); });
    for (const CFGBlock *B : Blocks) {
      unsigned I = 0;
      for (const CFGElement &E : *B) {
        ++I;
        std::optional<Site> S;
        if (auto St = E.getAs<CFGStmt>()) {
          if (const auto *X = dyn_cast<Expr>(St->getStmt()))
            if (auto AC = AnyCall::forExpr(X)) S = classifyCall(X, *AC, Edges);
        } else if (auto D = E.getAs<CFGImplicitDtor>()) {
          // The calls only the CFG sees: the graph has no edge for any of them.
          if (const CXXDestructorDecl *DD = D->getDestructorDecl(Ctx)) {
            S.emplace();
            S->Kind = AnyCall::Destructor;
            S->Callee = DD;
            S->Name = calleeName(DD);
            S->Class = E.getKind() == CFGElement::DeleteDtor ? "missing:delete" : "missing:implicit-dtor";
          }
        }
        if (!S) continue;
        S->Block = B->getBlockID();
        S->Index = I;
        F.Sites.push_back(std::move(*S));
      }
    }
  }

  // ---- the interprocedural walk -----------------------------------------------------

  void walk(const FnInfo &F, unsigned Depth) {
    PostOrderCFGView *RPO = F.AC->getAnalysis<PostOrderCFGView>();
    for (const CFGBlock *B : *RPO) {
      Walks.push_back({Depth, &F, B->getBlockID(), ""});
      for (const Site &S : F.Sites) {
        if (S.Block != B->getBlockID() || !S.Resolved) continue;
        Walks.push_back({Depth, &F, B->getBlockID(), S.Name});
        if (Depth >= DepthOpt) continue;
        if (auto Id = G.find(S.Node))
          if (const FnInfo *C = info(G.node(*Id))) walk(*C, Depth + 1);
      }
    }
  }

  // ---- text -----------------------------------------------------------------------

  static bool printed(const Site &S) { return !UnresolvedOpt || !S.Resolved; }

  void text(const std::vector<const FnInfo *> &Shown) {
    if (!WalkOpt.empty()) {
      for (const WalkRec &W : Walks) {
        llvm::outs() << "walk " << W.Depth << " " << G.name(W.Fn->N->Id) << ":B" << W.Block;
        if (!W.Callee.empty()) llvm::outs() << " -> " << cglab::quoteName(W.Callee);
        llvm::outs() << "\n";
      }
      return;
    }
    for (const FnInfo *F : Shown) {
      unsigned Count = 0;
      for (const Site &S : F->Sites) Count += printed(S);
      llvm::outs() << "== " << G.name(F->N->Id) << ": " << F->G->size() << " blocks, " << Count << " sites\n";
      for (const Site &S : F->Sites) {
        if (!printed(S)) continue;
        llvm::outs() << "site " << G.name(F->N->Id) << " B" << S.Block << "." << S.Index << " " << callKindName(S.Kind)
                     << " " << cglab::quoteName(S.Name) << " " << S.classText() << "\n";
      }
      if (SuccsOpt) succs(*F);
    }
  }

  // The CFG's edges, blocks in the dump's order (decreasing id). On a two-way branch the first
  // successor is the true edge, the second the false edge.
  void succs(const FnInfo &F) const {
    std::vector<const CFGBlock *> Blocks(F.G->begin(), F.G->end());
    std::sort(Blocks.begin(), Blocks.end(),
              [](const CFGBlock *A, const CFGBlock *B) { return A->getBlockID() > B->getBlockID(); });
    for (const CFGBlock *B : Blocks) {
      unsigned Pos = 0;
      for (const CFGBlock::AdjacentBlock &S : B->succs()) {
        const CFGBlock *T = S.getReachableBlock();
        unsigned K = Pos++;
        if (!T) continue;
        llvm::outs() << "succ " << G.name(F.N->Id) << " B" << B->getBlockID() << " -> B" << T->getBlockID();
        if (B->succ_size() == 2 && B->getTerminator().isValid()) llvm::outs() << (K == 0 ? " T" : " F");
        llvm::outs() << "\n";
      }
    }
  }

  // ---- json -----------------------------------------------------------------------

  void json(const std::vector<const FnInfo *> &Shown) {
    llvm::json::Array Fns;
    for (const FnInfo *F : Shown) {
      llvm::json::Array Sites;
      for (const Site &S : F->Sites)
        if (printed(S))
          Sites.push_back(llvm::json::Object{{"block", S.Block}, {"callee", S.Name}, {"class", S.classText()},
                                             {"index", S.Index}, {"kind", callKindName(S.Kind)}});
      Fns.push_back(llvm::json::Object{
          {"blocks", F->G->size()}, {"name", G.node(F->N->Id).Name}, {"sites", std::move(Sites)}});
    }
    llvm::json::Object Out{{"file", G.file()}, {"functions", std::move(Fns)}};
    if (!WalkOpt.empty()) {
      llvm::json::Array Ws;
      for (const WalkRec &W : Walks)
        Ws.push_back(llvm::json::Object{{"block", W.Block}, {"callee", W.Callee}, {"depth", W.Depth},
                                        {"function", G.node(W.Fn->N->Id).Name}});
      Out["walk"] = std::move(Ws);
    }
    llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(Out))) << "\n";
  }

  // ---- dot ------------------------------------------------------------------------

  // Node id of a block: "<fn>:B<id>".
  std::string blockId(const FnInfo &F, unsigned Block) const { return G.node(F.N->Id).Name + ":B" + std::to_string(Block); }

  void dot(const std::vector<const FnInfo *> &Shown) {
    llvm::raw_ostream &OS = llvm::outs();
    cglab::DotWriter W(OS, cglab::mainFileStem(G.sm()));
    std::set<std::string> Stubs; // callee names without a drawn function
    std::vector<std::string> Calls;

    auto Drawn = [&](const Decl *D) -> const FnInfo * {
      const CallGraphNode *N = D ? nodeFor(D) : nullptr;
      auto Id = N ? G.find(N) : std::nullopt;
      if (!Id) return nullptr;
      for (const FnInfo *F : Shown)
        if (F->N->Id == *Id) return F;
      return nullptr;
    };

    for (const FnInfo *F : Shown) {
      W.beginCluster(G.node(F->N->Id).Name, G.node(F->N->Id).Name, {"group"});
      for (const CFGBlock *B : *F->G) {
        cglab::DotAttrs A;
        A.Label = "B" + std::to_string(B->getBlockID());
        if (B == &F->G->getEntry()) { A.Label += " (ENTRY)"; A.addClass("entry"); }
        if (B == &F->G->getExit()) { A.Label += " (EXIT)"; A.addClass("exit"); }
        if (B->getTerminator().isValid() && B->succ_size() >= 2) A.addClass("cond");
        for (const Site &S : F->Sites)
          if (S.Block == B->getBlockID() && printed(S)) A.Label += "\n" + std::to_string(S.Index) + ": " + (S.Class == "virtual" ? S.Static : S.Name);
        W.node(blockId(*F, B->getBlockID()), A);
      }
      W.endCluster();
    }
    for (const FnInfo *F : Shown) {
      for (const CFGBlock *B : *F->G) {
        unsigned K = 0;
        for (const CFGBlock::AdjacentBlock &S : B->succs()) {
          const CFGBlock *T = S.getReachableBlock();
          unsigned Pos = K++;
          if (!T) continue;
          cglab::DotAttrs A;
          if (B->succ_size() == 2 && B->getTerminator().isValid()) {
            A.Label = Pos == 0 ? "T" : "F";
            A.addClass(Pos == 0 ? "t" : "f");
          }
          W.edge(blockId(*F, B->getBlockID()), blockId(*F, T->getBlockID()), A);
        }
      }
      for (const Site &S : F->Sites) {
        if (!printed(S)) continue;
        cglab::DotAttrs A;
        std::string To;
        if (S.Class == "missing:indirect") {
          To = "?";
          Stubs.insert(To);
          A.addClass("indirect");
        } else {
          const Decl *Target = S.Callee;
          if (S.Class == "virtual") A.addClass("virtual");
          else if (S.Resolved) A.addClass("call");
          else A.addClass("indirect");
          if (S.Class == "missing:implicit-dtor") A.Label = "implicit";
          if (S.Class == "missing:delete") A.Label = "delete";
          if (const FnInfo *C = Drawn(Target)) To = blockId(*C, C->G->getEntry().getBlockID());
          else {
            To = S.Class == "virtual" ? S.Static : S.Name;
            Stubs.insert(To);
          }
        }
        W.edge(blockId(*F, S.Block), To, A);
      }
    }
    for (const std::string &S : Stubs) {
      cglab::DotAttrs A;
      A.addClass(S == "?" ? "note" : "dim");
      W.node(S, A);
    }
    W.finish();
  }
};

void runTool(ASTContext &Ctx, CallGraph &CG) {
  Tool T(Ctx, CG);
  if (!T.setup()) { Status = 2; return; }
  T.run();
}

} // namespace

int main(int argc, const char **argv) {
  int RC = cglab::runPerTU(argc, argv, Cat, runTool);
  return RC ? RC : Status;
}
