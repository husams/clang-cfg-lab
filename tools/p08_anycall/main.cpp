// p08_anycall -- clang::AnyCall, one interface for every call-like expression (Section 8.7).
//
//   p08_anycall <file> [--func=NAME] [--decls] [--kinds]
//                      [--sort=name|source|rpo] [--emit=text|dot|json] [--all-files]
//                      [-- <compile flags>]
//
// AnyCall::forExpr(E) wraps what the Static Analyzer and the dataflow framework treat as a call:
// CallExpr (functions, members, operators, pointers, blocks), ObjCMessageExpr, CXXNewExpr,
// CXXDeleteExpr, CXXConstructExpr and CXXInheritedCtorInitExpr. AnyCall::forDecl(D) wraps a
// function or an Objective-C method. The tool walks every body of the main file (implicit
// members and template instantiations included, template patterns not: their calls are not
// resolved) and prints what AnyCall says about each expression, in the function it is written
// in. A default argument or an in-class initialiser is printed where it is written (under the
// function that declares the default, under the field), not at each use as CGBuilder does.
//
// Text output, one record per line (names quoted only when they contain a space):
//   call <fn> @L<line> <ExprClass> kind=<Kind> decl=<name|?> params=<n> ret=<type> [ident=<id>]
//   decl <name> kind=<Kind> params=<n> ret=<type>        (--decls: forDecl of every definition)
//   kinds: Function=<n> ObjCMethod=<n> Block=<n> Destructor=<n> Constructor=<n>
//          InheritedConstructor=<n> Allocator=<n> Deallocator=<n>     (--kinds: the call lines; one line)
// <Kind> is AnyCall::Kind; decl=? means getDecl() is null (a pointer, a block variable), params
// is parameters().size() (0 when there is no decl), ret is getReturnType(Ctx), ident is
// getIdentifier() when the declaration has an identifier name (an operator or a constructor
// has none). The Destructor kind never appears on a call line: it has no expression, only
// forDecl(~T) builds it.

#include "cglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p08_anycall options");
CGLAB_DEFINE_COMMON_FLAGS(Cat)
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::cat(Cat),
                                           llvm::cl::desc("only calls written in this function (printed name)"));
static llvm::cl::opt<bool> DeclsFlag("decls", llvm::cl::cat(Cat),
                                     llvm::cl::desc("also print AnyCall::forDecl of every definition"));
static llvm::cl::opt<bool> KindsFlag("kinds", llvm::cl::cat(Cat),
                                     llvm::cl::desc("print the number of call lines per AnyCall::Kind"));

namespace {

struct CallRec {
  const Decl *Caller = nullptr;
  std::string CallerName;
  unsigned Line = 0, Col = 0, Seq = 0;
  std::string ExprClass;
  AnyCall::Kind K = AnyCall::Function;
  std::string DeclName; // "?" when getDecl() is null
  size_t Params = 0;
  std::string Ret, Ident;
};

struct DeclRec {
  const Decl *D = nullptr;
  std::string Name;
  unsigned Line = 0, Col = 0, Seq = 0;
  AnyCall::Kind K = AnyCall::Function;
  size_t Params = 0;
  std::string Ret;
};

struct Collector : cglab::ContextVisitor {
  ASTContext &Ctx;
  const cglab::Graph &G;
  bool MainOnly;
  std::vector<CallRec> Calls;
  std::vector<DeclRec> Decls;
  std::set<std::string> Known; // names of every function or field a call was seen in

  Collector(ASTContext &C, const cglab::Graph &Gr, bool Main) : Ctx(C), G(Gr), MainOnly(Main) {
    VarContexts = true;
    ShouldVisitImplicitCode = true;
    ShouldVisitTemplateInstantiations = true;
  }

  bool wanted(const Decl *D) const { return !MainOnly || cglab::inMainFile(Ctx.getSourceManager(), D); }

  bool TraverseDecl(Decl *D) override {
    if (D && isContext(D)) {
      if (inDependentContext(D)) return true;
      bool Def = false;
      if (const auto *FD = dyn_cast<FunctionDecl>(D)) Def = FD->doesThisDeclarationHaveABody();
      else if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) Def = MD->hasBody();
      if (Def && wanted(D)) {
        if (std::optional<AnyCall> AC = AnyCall::forDecl(D)) {
          DeclRec R;
          R.D = D;
          R.Name = cglab::displayName(G, D);
          R.Line = cglab::declLine(Ctx.getSourceManager(), D);
          R.Seq = Decls.size();
          R.K = AC->getKind();
          R.Params = AC->parameters().size();
          R.Ret = cglab::typeName(AC->getReturnType(Ctx), Ctx);
          Known.insert(R.Name);
          Decls.push_back(std::move(R));
        }
      }
    }
    return ContextVisitor::TraverseDecl(D);
  }

  // written once, where the default is spelled
  bool TraverseCXXDefaultArgExpr(CXXDefaultArgExpr *) override { return true; }
  bool TraverseCXXDefaultInitExpr(CXXDefaultInitExpr *) override { return true; }

  bool VisitExpr(Expr *E) override {
    const Decl *C = cur();
    if (!C || !wanted(C)) return true;
    std::optional<AnyCall> AC = AnyCall::forExpr(E);
    if (!AC) return true;
    const SourceManager &SM = Ctx.getSourceManager();
    CallRec R;
    R.Caller = C;
    R.CallerName = cglab::displayName(G, C);
    R.Line = cglab::siteLine(SM, E);
    R.Col = cglab::siteCol(SM, E);
    R.Seq = Calls.size();
    R.ExprClass = E->getStmtClassName();
    R.K = AC->getKind();
    R.DeclName = AC->getDecl() ? cglab::displayName(G, AC->getDecl()) : "?";
    R.Params = AC->parameters().size();
    // getReturnType casts the declaration for the kinds that have no expression-based answer
    bool FromExpr = R.K == AnyCall::Function || R.K == AnyCall::ObjCMethod || R.K == AnyCall::Block;
    R.Ret = (FromExpr || AC->getDecl()) ? cglab::typeName(AC->getReturnType(Ctx), Ctx) : "?";
    if (const IdentifierInfo *II = AC->getIdentifier()) R.Ident = II->getName().str();
    Known.insert(R.CallerName);
    Calls.push_back(std::move(R));
    return true;
  }
};

const AnyCall::Kind AllKinds[] = {AnyCall::Function,    AnyCall::ObjCMethod,  AnyCall::Block,
                                  AnyCall::Destructor,  AnyCall::Constructor, AnyCall::InheritedConstructor,
                                  AnyCall::Allocator,   AnyCall::Deallocator};

void report(ASTContext &Ctx, CallGraph &CG) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) std::exit(2);

  cglab::Graph G(CG, Ctx, cgGraphOptions());
  Collector V(Ctx, G, !CgAllFilesFlag);
  V.TraverseDecl(Ctx.getTranslationUnitDecl());

  if (!FuncFlag.empty() && !V.Known.count(FuncFlag) && !G.find(FuncFlag)) {
    llvm::errs() << "no function named '" << FuncFlag << "' in " << G.file() << "\n";
    std::exit(2);
  }

  // order: by caller name then position (default), by position, or by the caller's rpo number
  auto Rpo = [&](const Decl *D) {
    std::optional<unsigned> Id = G.find(D);
    return Id ? G.node(*Id).Rpo : ~0u;
  };
  std::vector<CallRec> Calls;
  for (CallRec &R : V.Calls)
    if (FuncFlag.empty() || R.CallerName == FuncFlag) Calls.push_back(R);
  std::stable_sort(Calls.begin(), Calls.end(), [&](const CallRec &A, const CallRec &B) {
    switch (Sort) {
    case cglab::SortKey::Source: return std::tie(A.Line, A.Col, A.Seq) < std::tie(B.Line, B.Col, B.Seq);
    case cglab::SortKey::Rpo:
      return std::make_tuple(Rpo(A.Caller), A.Line, A.Col, A.Seq) < std::make_tuple(Rpo(B.Caller), B.Line, B.Col, B.Seq);
    default: return std::tie(A.CallerName, A.Line, A.Col, A.Seq) < std::tie(B.CallerName, B.Line, B.Col, B.Seq);
    }
  });
  std::vector<DeclRec> Decls;
  if (DeclsFlag)
    for (DeclRec &R : V.Decls)
      if (FuncFlag.empty() || R.Name == FuncFlag) Decls.push_back(R);
  std::stable_sort(Decls.begin(), Decls.end(), [&](const DeclRec &A, const DeclRec &B) {
    if (Sort == cglab::SortKey::Source) return std::tie(A.Line, A.Seq) < std::tie(B.Line, B.Seq);
    return std::tie(A.Name, A.Line, A.Seq) < std::tie(B.Name, B.Line, B.Seq);
  });

  if (Emit == cglab::EmitKind::Json) {
    llvm::json::Array Cs, Ds;
    for (const CallRec &R : Calls)
      Cs.push_back(llvm::json::Object{{"caller", R.CallerName}, {"line", R.Line}, {"col", R.Col},
                                      {"class", R.ExprClass}, {"kind", cglab::anyCallKindName(R.K)},
                                      {"decl", R.DeclName}, {"params", (int64_t)R.Params},
                                      {"ret", R.Ret}, {"ident", R.Ident}});
    for (const DeclRec &R : Decls)
      Ds.push_back(llvm::json::Object{{"name", R.Name}, {"kind", cglab::anyCallKindName(R.K)},
                                      {"params", (int64_t)R.Params}, {"ret", R.Ret}});
    llvm::json::Object Counts;
    for (AnyCall::Kind K : AllKinds) {
      int64_t N = 0;
      for (const CallRec &R : Calls) N += R.K == K;
      Counts[cglab::anyCallKindName(K)] = N;
    }
    llvm::json::Object J{{"file", G.file()}, {"calls", std::move(Cs)}, {"decls", std::move(Ds)},
                         {"kinds", std::move(Counts)}};
    llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(J))) << "\n";
    return;
  }

  if (Emit == cglab::EmitKind::Dot) {
    // one edge per (caller, callee, kind); a pointer or block-variable call goes to the "?" node
    cglab::DotWriter W(llvm::outs(), cglab::mainFileStem(G.sm()));
    std::set<std::string> Nodes;
    auto Node = [&](const std::string &Name, bool Unknown) {
      if (!Nodes.insert(Name).second) return;
      cglab::DotAttrs A;
      if (Unknown) A.addClass("dim");
      W.node(Name, A);
    };
    std::map<std::tuple<std::string, std::string, std::string>, unsigned> Edges;
    for (const CallRec &R : Calls) ++Edges[{R.CallerName, R.DeclName, cglab::anyCallKindName(R.K)}];
    for (const auto &[Key, N] : Edges) {
      Node(std::get<0>(Key), false);
      Node(std::get<1>(Key), std::get<1>(Key) == "?");
    }
    for (const auto &[Key, N] : Edges) {
      cglab::DotAttrs A;
      A.Label = std::get<2>(Key) + (N > 1 ? " \xC3\x97" + std::to_string(N) : "");
      A.addClass(std::get<1>(Key) == "?" ? "indirect" : "call");
      W.edge(std::get<0>(Key), std::get<1>(Key), A);
    }
    W.finish();
    return;
  }

  for (const CallRec &R : Calls) {
    llvm::outs() << "call " << cglab::quoteName(R.CallerName) << " @L" << R.Line << " " << R.ExprClass
                 << " kind=" << cglab::anyCallKindName(R.K) << " decl=" << cglab::quoteName(R.DeclName)
                 << " params=" << R.Params << " ret=" << R.Ret;
    if (!R.Ident.empty()) llvm::outs() << " ident=" << R.Ident;
    llvm::outs() << "\n";
  }
  for (const DeclRec &R : Decls)
    llvm::outs() << "decl " << cglab::quoteName(R.Name) << " kind=" << cglab::anyCallKindName(R.K)
                 << " params=" << R.Params << " ret=" << R.Ret << "\n";
  if (KindsFlag) {
    llvm::outs() << "kinds:";
    for (AnyCall::Kind K : AllKinds) {
      unsigned N = 0;
      for (const CallRec &R : Calls) N += R.K == K;
      llvm::outs() << " " << cglab::anyCallKindName(K) << "=" << N;
    }
    llvm::outs() << "\n";
  }
}

} // namespace

int main(int argc, const char **argv) { return cglab::runPerTU(argc, argv, Cat, report); }
