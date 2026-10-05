// p08_resolve -- extend the call graph with edges for indirect and virtual calls (Part 8.8).
//
//   p08_resolve FILE [--fnptr] [--devirt] [--cha] [--all] [--counts] [--sccs]
//                    [--emit=text|dot|json] [--sort=] [--with-root] [--all-files]
//
// The call graph has one edge per call site that names its callee. A call through a
// function pointer has none, and a virtual call has only the *static* callee (the method
// name lookup finds in the object expression's static type). This tool finds those sites
// and adds edges to the real clang::CallGraph (CallGraphNode::addCallee is public) by
// three rules, each trading soundness against precision:
//   --fnptr   every address-taken function whose type matches the pointer's type
//   --devirt  CXXMethodDecl::getDevirtualizedMethod: the one exact target, when the
//             object's type is known (a 'final' class or method, a local object)
//   --cha     class-hierarchy analysis: every overrider in this translation unit
// --all is the three together; a site that --devirt resolves is not given to --cha.
//
// Output, one record per line (names as in cglab.h; a name with a space is quoted):
//   add <caller> -> <callee> @L<line> reason=fnptr-sig|devirt-static|devirt-final|cha
//                                     [candidates=<n>]
//   skip <caller> @L<line> reason=unknown-type|no-overrider
//   stats: edges <before> -> <after>, indirect sites <n>, resolved <m>      (--counts)
//   scc <id> cyclic self|mutual: <members...>      (cyclic SCCs of the extended graph)
//
// 'unknown-type' = no address-taken function in this TU has the pointer's type (or the
// callee is not a function pointer at all: a block variable, a member pointer);
// 'no-overrider' = a pure virtual call with no implementation in the TU.
// 'indirect sites' counts every function-pointer call and every virtual call in the TU,
// whichever rules are on; 'resolved' counts those a rule found a target for. A virtual
// site devirtualised to the very method the graph already has is resolved without an
// 'add' line. The static edge itself is never removed: CallGraph has no way to.
//
// The flag is --counts, not --stats: LLVM registers its own -stats option and the process
// would abort at startup ("Option 'stats' registered more than once").
//
// --emit=dot classes: added edges `indirect` (fnptr) / `cha` / `hl` (devirt), the static
// edge of a virtual call `virtual`, cycle-closing edges `back`, cyclic SCCs as `scc` clusters.

#include "cglab.h"

#include "clang/AST/Attr.h"
#include "clang/AST/StmtVisitor.h"

#include <tuple>

using namespace clang;

static llvm::cl::OptionCategory Cat("p08_resolve options");
CGLAB_DEFINE_COMMON_FLAGS(Cat)
static llvm::cl::opt<bool> OptFnptr("fnptr", llvm::cl::cat(Cat),
                                    llvm::cl::desc("match indirect calls to address-taken functions by type"));
static llvm::cl::opt<bool> OptDevirt("devirt", llvm::cl::cat(Cat),
                                     llvm::cl::desc("devirtualise where the object's type is known"));
static llvm::cl::opt<bool> OptCha("cha", llvm::cl::cat(Cat),
                                  llvm::cl::desc("class-hierarchy analysis for virtual calls"));
static llvm::cl::opt<bool> OptAll("all", llvm::cl::cat(Cat), llvm::cl::desc("--fnptr --devirt --cha"));
static llvm::cl::opt<bool> OptCounts("counts", llvm::cl::cat(Cat), llvm::cl::desc("print edge and site counts"));
static llvm::cl::opt<bool> OptSccs("sccs", llvm::cl::cat(Cat),
                                   llvm::cl::desc("list the cyclic SCCs of the extended graph"));

namespace {

// ---------------------------------------------------------------------------
// What the call graph does not record: indirect call sites.
// ---------------------------------------------------------------------------

enum class SiteKind { FnPtr, Virtual };

struct Site {
  CallGraphNode *Caller;
  const CallExpr *CE;
  unsigned Line;
  SiteKind Kind;
};

bool isVirtualSite(const CallExpr *CE) {
  const auto *MCE = dyn_cast<CXXMemberCallExpr>(CE);
  if (!MCE) return false;
  const CXXMethodDecl *MD = MCE->getMethodDecl();
  const auto *ME = dyn_cast<MemberExpr>(CE->getCallee()->IgnoreParens());
  // x.Base::run() names the method: it is not dispatched.
  return MD && MD->isVirtual() && ME && !ME->hasQualifier();
}

// Walks one function body exactly the way CallGraph's CGBuilder does (children, default
// arguments and default member initialisers; a lambda body is another node's business) and
// records every call that has no direct callee.
struct SiteScan : StmtVisitor<SiteScan> {
  CallGraphNode *Caller;
  const SourceManager &SM;
  std::vector<Site> &Out;
  SiteScan(CallGraphNode *N, const SourceManager &S, std::vector<Site> &O) : Caller(N), SM(S), Out(O) {}

  void VisitStmt(Stmt *S) { VisitChildren(S); }
  void VisitLambdaExpr(LambdaExpr *) {}
  void VisitCXXDefaultArgExpr(CXXDefaultArgExpr *E) { Visit(E->getExpr()); }
  void VisitCXXDefaultInitExpr(CXXDefaultInitExpr *E) { Visit(E->getExpr()); }
  void VisitCallExpr(CallExpr *CE) {
    unsigned Line = cglab::siteLine(SM, CE);
    if (isVirtualSite(CE)) Out.push_back({Caller, CE, Line, SiteKind::Virtual});
    else if (!CE->getDirectCallee() && !isa<BlockExpr>(CE->getCallee()->IgnoreParenImpCasts()))
      Out.push_back({Caller, CE, Line, SiteKind::FnPtr});
    VisitChildren(CE);
  }
  void VisitChildren(Stmt *S) {
    for (Stmt *Sub : S->children())
      if (Sub) Visit(Sub);
  }
};

// The function type a pointer call goes through, or a null type when the callee is not a
// pointer to a prototyped function (a block variable, a member pointer).
QualType pointeeFunctionType(const CallExpr *CE) {
  QualType T = CE->getCallee()->IgnoreParens()->getType();
  if (const auto *PT = T->getAs<PointerType>()) T = PT->getPointeeType();
  return T->isFunctionProtoType() ? T : QualType();
}

// ---------------------------------------------------------------------------
// What the whole TU says: address-taken functions and the class hierarchy.
// ---------------------------------------------------------------------------

struct TUFacts : RecursiveASTVisitor<TUFacts> {
  std::set<const DeclRefExpr *> CalleeRefs;
  std::set<const FunctionDecl *> Taken; // canonical declarations; pointer order: sort before use
  std::vector<const CXXRecordDecl *> Records;

  bool shouldVisitTemplateInstantiations() const { return true; }
  bool VisitCallExpr(CallExpr *CE) {
    if (const auto *DRE = dyn_cast<DeclRefExpr>(CE->getCallee()->IgnoreParenImpCasts())) CalleeRefs.insert(DRE);
    return true;
  }
  // A reference to a function that is not in callee position takes its address: &f, a
  // decay in an initialiser or an argument, a binding to a function reference.
  bool VisitDeclRefExpr(DeclRefExpr *DRE) {
    const auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl());
    if (!FD || CalleeRefs.count(DRE) || FD->isDependentContext()) return true;
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD); MD && !MD->isStatic()) return true; // &C::m is a member pointer
    Taken.insert(FD->getCanonicalDecl());
    return true;
  }
  bool VisitCXXRecordDecl(CXXRecordDecl *RD) {
    if (RD->isCompleteDefinition() && !RD->isDependentContext() && !RD->isLambda()) Records.push_back(RD);
    return true;
  }
};

// The order edges are added in decides the order the SCC walk sees them: never pointer order.
void sortDecls(std::vector<const FunctionDecl *> &V) {
  auto Key = [](const FunctionDecl *D) {
    return std::make_tuple(cglab::baseName(D), D->getLocation().getRawEncoding());
  };
  std::sort(V.begin(), V.end(), [&](const FunctionDecl *A, const FunctionDecl *B) { return Key(A) < Key(B); });
}

struct Added {
  CallGraphNode *Caller;
  CallGraphNode *Callee;
  const CallExpr *CE;
  unsigned Line;
  std::string Reason;
  unsigned Candidates; // 0: not printed
};

struct Skipped {
  CallGraphNode *Caller;
  unsigned Line;
  std::string Reason;
};

struct Resolver {
  ASTContext &Ctx;
  CallGraph &CG;
  TUFacts &Facts;
  std::vector<Added> Adds;
  std::vector<Skipped> Skips;
  unsigned Resolved = 0;

  void addEdge(const Site &S, const FunctionDecl *Target, const char *Reason, unsigned Candidates) {
    CallGraphNode *Callee = CG.getOrInsertNode(const_cast<FunctionDecl *>(Target->getCanonicalDecl()));
    for (const CallGraphNode::CallRecord &R : S.Caller->callees())
      if (R.Callee == Callee && R.CallExpr == S.CE) return; // the static edge already says it
    S.Caller->addCallee({Callee, const_cast<CallExpr *>(S.CE)});
    Adds.push_back({S.Caller, Callee, S.CE, S.Line, Reason, Candidates});
  }

  void fnptr(const Site &S) {
    QualType FT = pointeeFunctionType(S.CE);
    std::vector<const FunctionDecl *> Cands;
    if (!FT.isNull())
      for (const FunctionDecl *F : Facts.Taken)
        if (Ctx.hasSameFunctionTypeIgnoringExceptionSpec(F->getType(), FT)) Cands.push_back(F);
    if (Cands.empty()) {
      Skips.push_back({S.Caller, S.Line, "unknown-type"});
      return;
    }
    ++Resolved;
    sortDecls(Cands);
    for (const FunctionDecl *F : Cands) addEdge(S, F, "fnptr-sig", Cands.size());
  }

  // True when the site was devirtualised (possibly to the very method the graph already has).
  bool devirt(const Site &S) {
    const auto *MCE = cast<CXXMemberCallExpr>(S.CE);
    CXXMethodDecl *MD = MCE->getMethodDecl();
    const Expr *Obj = MCE->getImplicitObjectArgument();
    const CXXMethodDecl *Exact = MD->getDevirtualizedMethod(Obj, /*IsAppleKext=*/false);
    if (!Exact) return false;
    const CXXRecordDecl *Dyn = Obj->getBestDynamicClassType();
    bool Final = MD->hasAttr<FinalAttr>() || Exact->hasAttr<FinalAttr>() || (Dyn && Dyn->isEffectivelyFinal());
    ++Resolved;
    addEdge(S, Exact, Final ? "devirt-final" : "devirt-static", 0);
    return true;
  }

  // Every overrider of the static callee in any class of this TU that derives from the
  // object's static type. A TU cannot see subclasses elsewhere, and it cannot see which
  // classes are ever instantiated: the answer is incomplete in one way and loose in another.
  void cha(const Site &S) {
    const auto *MCE = cast<CXXMemberCallExpr>(S.CE);
    const CXXMethodDecl *MD = MCE->getMethodDecl();
    const CXXRecordDecl *Static = MCE->getRecordDecl();
    std::set<const FunctionDecl *> Targets;
    for (const CXXRecordDecl *RD : Facts.Records) {
      if (RD != Static && !RD->isDerivedFrom(Static)) continue;
      if (RD->isAbstract()) continue; // no direct instances; a concrete subclass is a record too
      const CXXMethodDecl *M = MD->getCorrespondingMethodInClass(RD);
      if (M && !M->isPureVirtual()) Targets.insert(M->getCanonicalDecl());
    }
    if (Targets.empty()) {
      Skips.push_back({S.Caller, S.Line, "no-overrider"});
      return;
    }
    ++Resolved;
    std::vector<const FunctionDecl *> Sorted(Targets.begin(), Targets.end());
    sortDecls(Sorted);
    for (const FunctionDecl *T : Sorted) addEdge(S, T, "cha", Sorted.size());
  }
};

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

struct Stats {
  unsigned Before, After, Indirect, Resolved;
};

using EdgeKey = std::tuple<const CallGraphNode *, const CallGraphNode *, const Expr *>;

// One record per added or skipped site, ordered by caller, then line, then callee.
struct Record {
  unsigned Caller, Callee; // node ids in the extended graph (Callee 0 for a skip)
  unsigned Line;
  const Added *Add;
  const Skipped *Skip;
};

void emitText(const cglab::Graph &G, const Resolver &R, const Stats &St) {
  std::vector<Record> Recs;
  for (const Added &A : R.Adds) Recs.push_back({*G.find(A.Caller), *G.find(A.Callee), A.Line, &A, nullptr});
  for (const Skipped &S : R.Skips) Recs.push_back({*G.find(S.Caller), 0, S.Line, nullptr, &S});
  std::sort(Recs.begin(), Recs.end(), [](const Record &A, const Record &B) {
    return std::tie(A.Caller, A.Line, A.Callee) < std::tie(B.Caller, B.Line, B.Callee);
  });
  for (const Record &Rec : Recs) {
    if (Rec.Add) {
      llvm::outs() << "add " << G.name(Rec.Caller) << " -> " << G.name(Rec.Callee) << " @L" << Rec.Line
                   << " reason=" << Rec.Add->Reason;
      if (Rec.Add->Candidates) llvm::outs() << " candidates=" << Rec.Add->Candidates;
      llvm::outs() << "\n";
    } else {
      llvm::outs() << "skip " << G.name(Rec.Caller) << " @L" << Rec.Line << " reason=" << Rec.Skip->Reason << "\n";
    }
  }
  if (OptCounts)
    llvm::outs() << "stats: edges " << St.Before << " -> " << St.After << ", indirect sites " << St.Indirect
                 << ", resolved " << St.Resolved << "\n";
  if (OptSccs) cglab::printSccs(llvm::outs(), G, G.sccs(), /*CyclicOnly=*/true);
}

void emitTU(ASTContext &Ctx, CallGraph &CG) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) std::exit(2);
  if (OptAll) OptFnptr = OptDevirt = OptCha = true;
  if (!OptFnptr && !OptDevirt && !OptCha && !OptCounts && !OptSccs) {
    llvm::errs() << "p08_resolve: choose at least one of --fnptr --devirt --cha --all --counts --sccs\n";
    std::exit(2);
  }
  const SourceManager &SM = Ctx.getSourceManager();

  // 1. the sites the graph lacks, found in the order of the pre-resolution graph (name order)
  TUFacts Facts;
  Facts.TraverseDecl(Ctx.getTranslationUnitDecl());
  std::vector<Site> Sites;
  {
    cglab::Graph Before(CG, Ctx, cgGraphOptions());
    for (unsigned Id : Before.order(Sort)) {
      const cglab::Node &N = Before.node(Id);
      if (!cglab::inMainFile(SM, N.D)) continue; // only these nodes have their out-edges in the graph
      SiteScan Scan(N.CGN, SM, Sites);
      if (Stmt *Body = N.D->getBody()) Scan.Visit(Body);
      if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(N.D))
        for (CXXCtorInitializer *Init : Ctor->inits()) Scan.Visit(Init->getInit());
    }
  }

  // 2. resolve
  Resolver R{Ctx, CG, Facts, {}, {}, 0};
  std::set<const Expr *> VirtualSites;
  for (const Site &S : Sites) {
    if (S.Kind == SiteKind::FnPtr) {
      if (OptFnptr) R.fnptr(S);
      continue;
    }
    VirtualSites.insert(S.CE);
    if (OptDevirt && R.devirt(S)) continue;
    if (OptCha) R.cha(S);
  }

  // 3. report on the extended graph
  cglab::Graph G(CG, Ctx, cgGraphOptions());
  Stats St{G.numEdges() - static_cast<unsigned>(R.Adds.size()), G.numEdges(), static_cast<unsigned>(Sites.size()),
           R.Resolved};
  std::map<EdgeKey, const Added *> AddedEdge;
  for (const Added &A : R.Adds) AddedEdge[{A.Caller, A.Callee, A.CE}] = &A;
  auto addedOf = [&](const cglab::Edge &E) -> const Added * {
    auto It = AddedEdge.find({G.node(E.From).CGN, G.node(E.To).CGN, E.Site});
    return It == AddedEdge.end() ? nullptr : It->second;
  };

  if (Emit == cglab::EmitKind::Dot) {
    cglab::DotOptions O;
    O.SccClusters = true;
    O.WithRoot = CgWithRootFlag;
    O.Sort = Sort;
    O.EdgeHook = [&](const cglab::Edge &E, cglab::DotAttrs &A) {
      if (const Added *Ad = addedOf(E)) {
        A.addClass(Ad->Reason == "fnptr-sig" ? "indirect" : Ad->Reason == "cha" ? "cha" : "hl");
        A.Label = Ad->Reason;
      } else if (E.Site && VirtualSites.count(E.Site)) {
        A.addClass("virtual");
      }
    };
    cglab::writeDot(llvm::outs(), G, O);
  } else if (Emit == cglab::EmitKind::Json) {
    cglab::JsonOptions O;
    O.WithRoot = CgWithRootFlag;
    O.Sort = Sort;
    O.EdgeHook = [&](const cglab::Edge &E, llvm::json::Object &J) {
      const Added *Ad = addedOf(E);
      J["reason"] = Ad ? llvm::json::Value(Ad->Reason) : llvm::json::Value(nullptr);
      J["virtual"] = !Ad && E.Site && VirtualSites.count(E.Site);
    };
    llvm::json::Object Doc = cglab::toJson(G, O);
    llvm::json::Array Adds, Skips;
    for (const Added &A : R.Adds)
      Adds.push_back(llvm::json::Object{{"caller", G.node(*G.find(A.Caller)).Name},
                                        {"callee", G.node(*G.find(A.Callee)).Name},
                                        {"line", A.Line},
                                        {"reason", A.Reason},
                                        {"candidates", A.Candidates}});
    for (const Skipped &S : R.Skips)
      Skips.push_back(llvm::json::Object{
          {"caller", G.node(*G.find(S.Caller)).Name}, {"line", S.Line}, {"reason", S.Reason}});
    Doc["adds"] = std::move(Adds);
    Doc["skips"] = std::move(Skips);
    Doc["stats"] = llvm::json::Object{{"edgesBefore", St.Before},
                                      {"edgesAfter", St.After},
                                      {"indirectSites", St.Indirect},
                                      {"resolved", St.Resolved}};
    llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(Doc))) << "\n";
  } else {
    emitText(G, R, St);
  }
}

} // namespace

int main(int argc, const char **argv) { return cglab::runPerTU(argc, argv, Cat, emitTU); }
