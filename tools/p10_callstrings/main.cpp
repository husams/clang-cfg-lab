// p10_callstrings -- context sensitivity with k-limited call strings (Part 10.5).
//
//   p10_callstrings <file> [--k=N] [--func=NAME] [--trace] [--edges]
//                          [--sort=..] [--emit=text|dot|json] [--all-files]
//
// The question: "may this int parameter be zero?" A summary (Part 10.3) answers once per
// function and merges every caller; a call string keeps the last k call sites apart, so
// divide() called from safe() with 2 and from risky() with 0 gets two answers instead of one
// "maybe". The analysis is deliberately small and AST-only:
//   * a value is zero, nonzero or top (either); the join of two different values is top;
//   * an argument is zero/nonzero when it is a constant expression, the caller's own value when
//     it is one of the caller's parameters, and top otherwise (n - 1, a call result, ...);
//   * only the call graph's edges whose call expression is a plain call are followed;
//   * a function that nobody calls is an entry: its parameters are top, its string is empty;
//   * a context is (function, call string); the string is at most k call sites, oldest first,
//     and the oldest sites are dropped when a call would make it longer (the string then starts
//     with an ellipsis). Contexts are processed in reverse post-order of the call graph until no
//     value changes; recursion ends because the string is cut at length k, not because of a
//     fixed point over the graph.
// Text output, one record per line:
//   ctx k=<k> <fn>[<site>,<site>...]: <param>=zero|nonzero|top ...   (a site is <caller>@L<line>)
//   trace <fn>[...] <- <caller>[...] @L<line>: <param>=<v> ...       (--trace: the values a call brought in)
//   warn <file>:<line>: <fn>[...] divides by <param>=zero|top        (a / or % by a parameter that may be 0)
//   summary: k=<k> contexts=<n> functions=<m> warnings=<w>
//   edge <caller> -> <callee>                                        (--edges: the call graph's edges)
// An empty string prints [], a cut string starts with "…" ([…,loop@L16,loop@L16]); a function
// without int parameters prints "-". The records of one context stay together (ctx, its trace
// lines, its warn lines); contexts are ordered by function (--sort) and then by call string.
// --func only filters the printed records. The analysis stops with an error at 5000 contexts.

#include "cglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p10_callstrings options");
CGLAB_DEFINE_COMMON_FLAGS(Cat) // --sort --emit --with-root --all-files
static llvm::cl::opt<unsigned> KOpt("k", llvm::cl::init(1), llvm::cl::cat(Cat),
                                    llvm::cl::desc("call sites a call string keeps (0 = one context per function)"));
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat),
                                          llvm::cl::desc("only the contexts of this function"));
static llvm::cl::opt<bool> TraceOpt("trace", llvm::cl::cat(Cat),
                                    llvm::cl::desc("print, per context, the values each call brought in"));
static llvm::cl::opt<bool> EdgesOpt("edges", llvm::cl::cat(Cat),
                                    llvm::cl::desc("also print the call edges between the analysed functions"));

namespace {

int Status = 0;

constexpr size_t kMaxContexts = 5000;
const char *const kEllipsis = "\xE2\x80\xA6"; // "…"

enum class Val : unsigned char { Bot, Zero, NonZero, Top };

Val join(Val A, Val B) {
  if (A == Val::Bot) return B;
  if (B == Val::Bot) return A;
  return A == B ? A : Val::Top;
}

const char *valName(Val V) {
  switch (V) {
  case Val::Bot: return "bottom";
  case Val::Zero: return "zero";
  case Val::NonZero: return "nonzero";
  case Val::Top: return "top";
  }
  return "?";
}

// A call site: the caller (graph node id) and the position of the call expression.
struct Site {
  unsigned From = 0, Line = 0, Col = 0;
  bool operator==(const Site &O) const { return From == O.From && Line == O.Line && Col == O.Col; }
  bool operator<(const Site &O) const {
    return std::tie(From, Line, Col) < std::tie(O.From, O.Line, O.Col);
  }
};

// A division or remainder whose divisor is a parameter of the function.
struct Division {
  unsigned Line = 0;
  unsigned Param = 0;
};

struct FnInfo {
  const FunctionDecl *FD = nullptr; // null: no body to analyse (declaration, lambda, block, ...)
  std::vector<bool> Tracked;        // per parameter: an integer, so it has a value
  std::vector<Division> Divs;
};

// Finds the divisions by a parameter in one body.
class DivFinder : public RecursiveASTVisitor<DivFinder> {
public:
  DivFinder(const FunctionDecl *FD, const SourceManager &SM, std::vector<Division> &Out)
      : FD(FD), SM(SM), Out(Out) {}
  bool VisitBinaryOperator(BinaryOperator *BO) {
    BinaryOperatorKind K = BO->getOpcode();
    if (K != BO_Div && K != BO_Rem && K != BO_DivAssign && K != BO_RemAssign) return true;
    const auto *DR = dyn_cast<DeclRefExpr>(BO->getRHS()->IgnoreParenImpCasts());
    if (!DR) return true;
    for (unsigned I = 0; I < FD->getNumParams(); ++I)
      if (FD->getParamDecl(I) == DR->getDecl())
        Out.push_back({SM.getSpellingLineNumber(BO->getOperatorLoc()), I});
    return true;
  }

private:
  const FunctionDecl *FD;
  const SourceManager &SM;
  std::vector<Division> &Out;
};

// One analysed context: a function reached through a call string, with the join of the
// values every call that reached it passed in.
struct Context {
  unsigned Fn = 0;
  std::vector<Site> Str; // at most k call sites, oldest first
  bool Cut = false;      // older call sites were dropped
  std::vector<Val> Env;  // one value per parameter of Fn (Bot: not an int)
};

using Key = std::tuple<unsigned, bool, std::vector<Site>>;

// A call from one context to another, as the final values see it.
struct CtxEdge {
  unsigned From = 0, To = 0;
  unsigned Edge = 0; // index in the Graph
  std::vector<Val> Args;
  bool Back = false;
};

class Analysis {
public:
  Analysis(const cglab::Graph &G, ASTContext &Ctx, unsigned K, cglab::SortKey Sort)
      : G(G), Ctx(Ctx), K(K), Sort(Sort) {
    const SourceManager &SM = Ctx.getSourceManager();
    Info.resize(G.nodes().size());
    for (const cglab::Node &N : G.nodes()) {
      const auto *FD = N.Id == 0 ? nullptr : dyn_cast_or_null<FunctionDecl>(N.Best);
      if (!FD || !FD->hasBody() || N.K == cglab::Kind::Lambda || N.K == cglab::Kind::Block) continue;
      FnInfo &F = Info[N.Id];
      F.FD = FD->getDefinition() ? FD->getDefinition() : FD;
      for (const ParmVarDecl *P : F.FD->parameters())
        F.Tracked.push_back(P->getType()->isIntegralOrEnumerationType());
      DivFinder(F.FD, SM, F.Divs).TraverseStmt(F.FD->getBody());
    }
  }

  // Run to the fixed point; false when the context limit was hit.
  bool solve() {
    for (unsigned Id : G.order(cglab::SortKey::Name)) {
      if (!Info[Id].FD || !G.callers(Id).empty()) continue;
      std::vector<Val> Env(Info[Id].Tracked.size(), Val::Bot);
      for (size_t I = 0; I < Env.size(); ++I)
        if (Info[Id].Tracked[I]) Env[I] = Val::Top; // nobody calls an entry: anything goes
      add(Id, {}, false, Env);
    }
    while (!Work.empty() && Ctxs.size() <= kMaxContexts) {
      unsigned Ci = Work.begin()->second;
      Work.erase(Work.begin());
      Context C = Ctxs[Ci]; // add() may grow Ctxs
      for (unsigned Ei : G.recordedOut(C.Fn)) {
        std::optional<Call> X = callOf(C, Ei);
        if (X) add(X->Callee, X->Str, X->Cut, X->Args);
      }
    }
    if (Ctxs.size() > kMaxContexts) return false;
    // the converged contexts, and the calls between them
    for (unsigned Ci = 0; Ci < Ctxs.size(); ++Ci)
      for (unsigned Ei : G.recordedOut(Ctxs[Ci].Fn)) {
        std::optional<Call> X = callOf(Ctxs[Ci], Ei);
        if (!X) continue;
        CtxEdge CE;
        CE.From = Ci;
        CE.To = Index.at(Key{X->Callee, X->Cut && !X->Str.empty(), X->Str});
        CE.Edge = Ei;
        CE.Args = X->Args;
        Edges.push_back(std::move(CE));
      }
    markBackEdges();
    sortContexts();
    return true;
  }

  const std::vector<Context> &contexts() const { return Ctxs; }
  const std::vector<CtxEdge> &edges() const { return Edges; }
  const std::vector<unsigned> &printOrder() const { return Order; }
  const FnInfo &info(unsigned Fn) const { return Info[Fn]; }

  bool isEntry(unsigned Ci) const { return Incoming[Ci].empty(); }
  const std::vector<unsigned> &incoming(unsigned Ci) const { return Incoming[Ci]; }

  std::string text(unsigned Ci) const {
    const Context &C = Ctxs[Ci];
    std::string S = G.name(C.Fn) + "[";
    bool First = true;
    if (C.Cut && !C.Str.empty()) {
      S += kEllipsis;
      First = false;
    }
    for (const Site &St : C.Str) {
      S += (First ? "" : ",") + G.name(St.From) + "@L" + std::to_string(St.Line);
      First = false;
    }
    return S + "]";
  }

  // "a=top b=zero" over the tracked parameters, "-" when there are none.
  std::string values(unsigned Fn, const std::vector<Val> &Env) const {
    std::string S;
    const FnInfo &F = Info[Fn];
    for (unsigned I = 0; I < Env.size(); ++I)
      if (F.Tracked[I]) S += (S.empty() ? "" : " ") + paramName(F, I) + "=" + valName(Env[I]);
    return S.empty() ? "-" : S;
  }

  static std::string paramName(const FnInfo &F, unsigned I) {
    std::string N = F.FD->getParamDecl(I)->getNameAsString();
    return N.empty() ? "#" + std::to_string(I + 1) : N;
  }

private:
  // The call string and the argument values a call edge hands to its callee.
  struct Call {
    unsigned Callee = 0;
    std::vector<Site> Str;
    bool Cut = false;
    std::vector<Val> Args;
  };

  std::optional<Call> callOf(const Context &C, unsigned Ei) const {
    const cglab::Edge &E = G.edge(Ei);
    if (E.Root || !Info[E.To].FD) return std::nullopt;
    const auto *CE = dyn_cast_or_null<CallExpr>(E.Site);
    if (!CE || isa<CXXOperatorCallExpr>(CE)) return std::nullopt; // plain and member calls only
    const FnInfo &Callee = Info[E.To];
    Call X;
    X.Callee = E.To;
    X.Args.assign(Callee.Tracked.size(), Val::Bot);
    for (unsigned I = 0; I < Callee.Tracked.size(); ++I)
      if (Callee.Tracked[I]) X.Args[I] = I < CE->getNumArgs() ? argValue(CE->getArg(I), C) : Val::Top;
    X.Str = C.Str;
    X.Cut = C.Cut;
    X.Str.push_back({C.Fn, E.Line, E.Col});
    if (X.Str.size() > K) {
      X.Str.erase(X.Str.begin(), X.Str.end() - K);
      X.Cut = true;
    }
    return X;
  }

  Val argValue(const Expr *A, const Context &C) const {
    if (const auto *D = dyn_cast<CXXDefaultArgExpr>(A)) A = D->getExpr();
    Expr::EvalResult R;
    if (!A->isValueDependent() && A->getType()->isIntegralOrEnumerationType() && A->EvaluateAsInt(R, Ctx))
      return R.Val.getInt().isZero() ? Val::Zero : Val::NonZero;
    if (const auto *DR = dyn_cast<DeclRefExpr>(A->IgnoreParenImpCasts()))
      for (unsigned I = 0; I < Info[C.Fn].FD->getNumParams(); ++I)
        if (Info[C.Fn].FD->getParamDecl(I) == DR->getDecl()) return C.Env[I];
    return Val::Top;
  }

  // Join the values into the context (creating it) and schedule it when anything changed.
  void add(unsigned Fn, std::vector<Site> Str, bool Cut, const std::vector<Val> &Args) {
    Cut = Cut && !Str.empty(); // with k = 0 every string is empty: one context per function
    Key Kx{Fn, Cut, Str};
    auto It = Index.find(Kx);
    if (It == Index.end()) {
      Context C;
      C.Fn = Fn;
      C.Str = std::move(Str);
      C.Cut = Cut;
      C.Env = Args;
      It = Index.emplace(Kx, Ctxs.size()).first;
      Ctxs.push_back(std::move(C));
      Work.insert({G.node(Fn).Rpo, It->second});
      return;
    }
    Context &C = Ctxs[It->second];
    bool Changed = false;
    for (size_t I = 0; I < Args.size(); ++I) {
      Val V = join(C.Env[I], Args[I]);
      if (V != C.Env[I]) {
        C.Env[I] = V;
        Changed = true;
      }
    }
    if (Changed) Work.insert({G.node(Fn).Rpo, It->second});
  }

  // A DFS from the entry contexts in print order; a call into the DFS stack closes a cycle.
  void markBackEdges() {
    Incoming.assign(Ctxs.size(), {});
    std::vector<std::vector<unsigned>> Out(Ctxs.size());
    for (unsigned I = 0; I < Edges.size(); ++I) {
      Out[Edges[I].From].push_back(I);
      Incoming[Edges[I].To].push_back(I);
    }
    std::vector<char> Color(Ctxs.size(), 0); // 0 white, 1 on the stack, 2 done
    std::function<void(unsigned)> Dfs = [&](unsigned V) {
      Color[V] = 1;
      for (unsigned I : Out[V]) {
        if (Color[Edges[I].To] == 1) Edges[I].Back = true;
        else if (Color[Edges[I].To] == 0) Dfs(Edges[I].To);
      }
      Color[V] = 2;
    };
    for (unsigned Ci = 0; Ci < Ctxs.size(); ++Ci)
      if (Incoming[Ci].empty() && !Color[Ci]) Dfs(Ci);
  }

  // By function in the requested order, then by call string.
  void sortContexts() {
    std::map<unsigned, unsigned> Pos;
    unsigned P = 0;
    for (unsigned Id : G.order(Sort)) Pos[Id] = P++;
    Order.resize(Ctxs.size());
    for (unsigned I = 0; I < Order.size(); ++I) Order[I] = I;
    std::stable_sort(Order.begin(), Order.end(), [&](unsigned A, unsigned B) {
      const Context &X = Ctxs[A], &Y = Ctxs[B];
      return std::tie(Pos[X.Fn], X.Cut, X.Str) < std::tie(Pos[Y.Fn], Y.Cut, Y.Str);
    });
  }

  const cglab::Graph &G;
  ASTContext &Ctx;
  unsigned K;
  cglab::SortKey Sort; // the order of the functions in the output
  std::vector<FnInfo> Info;
  std::vector<Context> Ctxs;
  std::map<Key, unsigned> Index;
  std::set<std::pair<unsigned, unsigned>> Work; // (reverse post-order of the function, context)
  std::vector<CtxEdge> Edges;
  std::vector<std::vector<unsigned>> Incoming;
  std::vector<unsigned> Order;
};

struct Warning {
  unsigned Ci = 0; // the context
  unsigned Line = 0;
  unsigned Param = 0;
  Val V = Val::Top;
};

void run(ASTContext &Ctx, CallGraph &CG) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) { Status = 2; return; }
  cglab::Graph G(CG, Ctx, cgGraphOptions());
  std::optional<unsigned> Only;
  if (!FuncOpt.empty()) {
    Only = G.find(llvm::StringRef(FuncOpt));
    if (!Only) {
      llvm::errs() << "no function named '" << FuncOpt << "' in the call graph\n";
      Status = 2;
      return;
    }
  }

  const unsigned K = KOpt;
  Analysis A(G, Ctx, K, Sort);
  if (!A.solve()) {
    llvm::errs() << "error: more than " << kMaxContexts << " contexts; lower --k\n";
    Status = 2;
    return;
  }

  // the warnings: a division by a parameter whose value in this context may be zero
  std::vector<Warning> Warns;
  std::set<unsigned> Warned;
  std::set<unsigned> Functions;
  for (unsigned Ci : A.printOrder()) {
    const Context &C = A.contexts()[Ci];
    Functions.insert(C.Fn);
    for (const Division &D : A.info(C.Fn).Divs) {
      Val V = C.Env[D.Param];
      if (V == Val::Zero || V == Val::Top) {
        Warns.push_back({Ci, D.Line, D.Param, V});
        Warned.insert(Ci);
      }
    }
  }
  auto shown = [&](unsigned Ci) { return !Only || A.contexts()[Ci].Fn == *Only; };

  if (Emit == cglab::EmitKind::Text) {
    for (unsigned Ci : A.printOrder()) {
      if (!shown(Ci)) continue;
      const Context &C = A.contexts()[Ci];
      llvm::outs() << "ctx k=" << K << " " << A.text(Ci) << ": " << A.values(C.Fn, C.Env) << "\n";
      if (TraceOpt)
        for (unsigned Ei : A.incoming(Ci)) {
          const CtxEdge &E = A.edges()[Ei];
          llvm::outs() << "trace " << A.text(Ci) << " <- " << A.text(E.From) << " @L" << G.edge(E.Edge).Line << ": "
                       << A.values(C.Fn, E.Args) << "\n";
        }
      for (const Warning &W : Warns)
        if (W.Ci == Ci)
          llvm::outs() << "warn " << G.file() << ":" << W.Line << ": " << A.text(Ci) << " divides by "
                       << Analysis::paramName(A.info(C.Fn), W.Param) << "=" << valName(W.V) << "\n";
    }
    llvm::outs() << "summary: k=" << K << " contexts=" << A.contexts().size() << " functions=" << Functions.size()
                 << " warnings=" << Warns.size() << "\n";
    if (EdgesOpt) {
      // one line per distinct caller/callee pair among the analysed functions
      for (unsigned Id : G.order(Sort)) {
        if (!Functions.count(Id)) continue;
        std::set<unsigned> Done;
        for (unsigned Ei : G.outEdges(Id, Sort))
          if (Functions.count(G.edge(Ei).To) && Done.insert(G.edge(Ei).To).second)
            cglab::printEdgeLine(llvm::outs(), G, G.edge(Ei), false, false);
      }
    }
    return;
  }

  if (Emit == cglab::EmitKind::Dot) {
    cglab::DotWriter W(llvm::outs(), cglab::mainFileStem(G.sm()) + "_k" + std::to_string(K));
    std::map<unsigned, std::vector<unsigned>> ByFn; // the functions with several contexts get a cluster
    for (unsigned Ci : A.printOrder())
      if (shown(Ci)) ByFn[A.contexts()[Ci].Fn].push_back(Ci);
    auto node = [&](unsigned Ci) {
      const Context &C = A.contexts()[Ci];
      cglab::DotAttrs At;
      At.Label = A.text(Ci) + "\n" + A.values(C.Fn, C.Env);
      if (A.isEntry(Ci)) At.addClass("entry");
      if (Warned.count(Ci)) At.addClass("hl");
      if (G.node(C.Fn).Recursive) At.addClass("recursive");
      W.node(A.text(Ci), At);
    };
    for (unsigned Id : G.order(Sort)) {
      auto It = ByFn.find(Id);
      if (It == ByFn.end()) continue;
      bool Group = It->second.size() > 1;
      if (Group) W.beginCluster("fn" + std::to_string(Id), G.node(Id).Name, {"group"});
      for (unsigned Ci : It->second) node(Ci);
      if (Group) W.endCluster();
    }
    for (const CtxEdge &E : A.edges()) {
      if (!shown(E.From) || !shown(E.To)) continue;
      cglab::DotAttrs At;
      At.Label = "@L" + std::to_string(G.edge(E.Edge).Line);
      if (E.Back) At.addClass("back");
      if (Warned.count(E.To)) At.addClass("hl");
      W.edge(A.text(E.From), A.text(E.To), At);
    }
    W.finish();
    return;
  }

  llvm::json::Array Cs, Es, Ws;
  for (unsigned Ci : A.printOrder()) {
    if (!shown(Ci)) continue;
    const Context &C = A.contexts()[Ci];
    llvm::json::Array Str;
    for (const Site &S : C.Str) Str.push_back(G.node(S.From).Name + "@L" + std::to_string(S.Line));
    llvm::json::Object Params;
    for (unsigned I = 0; I < C.Env.size(); ++I)
      if (A.info(C.Fn).Tracked[I]) Params[Analysis::paramName(A.info(C.Fn), I)] = valName(C.Env[I]);
    Cs.push_back(llvm::json::Object{{"cut", C.Cut},
                                    {"entry", A.isEntry(Ci)},
                                    {"function", G.node(C.Fn).Name},
                                    {"name", A.text(Ci)},
                                    {"params", std::move(Params)},
                                    {"string", std::move(Str)}});
  }
  for (const CtxEdge &E : A.edges()) {
    if (!shown(E.From) || !shown(E.To)) continue;
    Es.push_back(llvm::json::Object{{"back", E.Back},
                                    {"from", A.text(E.From)},
                                    {"line", G.edge(E.Edge).Line},
                                    {"to", A.text(E.To)},
                                    {"values", A.values(A.contexts()[E.To].Fn, E.Args)}});
  }
  for (const Warning &Wn : Warns) {
    if (!shown(Wn.Ci)) continue;
    const Context &C = A.contexts()[Wn.Ci];
    Ws.push_back(llvm::json::Object{{"context", A.text(Wn.Ci)},
                                    {"line", Wn.Line},
                                    {"param", Analysis::paramName(A.info(C.Fn), Wn.Param)},
                                    {"value", valName(Wn.V)}});
  }
  llvm::json::Object Out{{"contexts", std::move(Cs)},
                         {"edges", std::move(Es)},
                         {"file", G.file()},
                         {"k", K},
                         {"summary", llvm::json::Object{{"contexts", A.contexts().size()},
                                                        {"functions", Functions.size()},
                                                        {"warnings", Warns.size()}}},
                         {"warnings", std::move(Ws)}};
  llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(Out))) << "\n";
}

} // namespace

int main(int argc, const char **argv) {
  int RC = cglab::runPerTU(argc, argv, Cat, run);
  return RC ? RC : Status;
}
