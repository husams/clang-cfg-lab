// p08_build -- how clang::CallGraph builds itself (Section 8.4).
//
//   p08_build <file> [--implicit=0|1] [--instantiations=0|1] [--typelocs=0|1] [--lambda-body=0|1]
//                    [--incremental] [--doors] [--edges] [--sites] [--kinds] [--func=NAME]
//                    [--with-root] [--sort=name|source|rpo] [--emit=text|dot|json] [--all-files]
//                    [-- <compile flags>]
//
// CallGraph is a DynamicRecursiveASTVisitor. Its four public data members decide what the
// traversal of the declarations reaches; --implicit, --instantiations, --typelocs and
// --lambda-body set them after construction and before addToCallGraph (default: the library's
// own values). The body of a function is never traversed (CallGraph::TraverseStmt returns
// true): every edge comes from CGBuilder, which addNodeForDecl runs over each body.
//
// Text output, one record per line (names quoted only when they contain a space):
//   flags implicit=<b> instantiations=<b> typelocs=<b> lambda-body=<b>
//   after <fn>: size=<n> new: <name>... | -          (--incremental: one line per function)
//   == <file>: N nodes, M edges
//   node <name> [door=def|callee|block] [kind=...]    (--doors adds door=, --kinds kind=)
//   edge <caller> -> <callee> [@L<line> <ExprClass>] [kind=...]
//   diff: nodes <a> -> <b> [(-<gone>... +<new>...)] edges <c> -> <d>
// The door is how a node entered the graph. def: includeInGraph let a definition in (the
// visitor reached the function and walked its body). callee: only includeCalleeInGraph let it
// in, as the callee of some call (a declaration, or a function whose body the visitor never
// reached). block: a BlockDecl. diff: appears when a flag differs from the library's default;
// it compares this graph with the one the default flags build from the same file.
//
// --incremental feeds the definitions of the main file to addToCallGraph(FD) one at a time, in
// source order, and prints the graph's size() (the <root> counts) and the nodes that appeared
// after each. Implicit members are not definitions the source spells, so they are not fed.
//
// Names are the lab's (cglab.h); a function not defined in the main file is a node only as a
// callee. --func restricts the node and edge lists to one function and its out-edges.

#include "cglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p08_build options");
CGLAB_DEFINE_COMMON_FLAGS(Cat)
static llvm::cl::opt<int> ImplicitFlag("implicit", llvm::cl::init(-1), llvm::cl::cat(Cat),
                                       llvm::cl::desc("ShouldVisitImplicitCode: 0|1 (default: the library's)"));
static llvm::cl::opt<int> InstFlag("instantiations", llvm::cl::init(-1), llvm::cl::cat(Cat),
                                   llvm::cl::desc("ShouldVisitTemplateInstantiations: 0|1"));
static llvm::cl::opt<int> TypeLocsFlag("typelocs", llvm::cl::init(-1), llvm::cl::cat(Cat),
                                       llvm::cl::desc("ShouldWalkTypesOfTypeLocs: 0|1"));
static llvm::cl::opt<int> LambdaBodyFlag("lambda-body", llvm::cl::init(-1), llvm::cl::cat(Cat),
                                         llvm::cl::desc("ShouldVisitLambdaBody: 0|1"));
static llvm::cl::opt<bool> IncrementalFlag("incremental", llvm::cl::cat(Cat),
                                           llvm::cl::desc("addToCallGraph one definition at a time"));
static llvm::cl::opt<bool> DoorsFlag("doors", llvm::cl::cat(Cat),
                                     llvm::cl::desc("add door=def|callee|block to node lines"));
static llvm::cl::opt<bool> EdgesFlag("edges", llvm::cl::cat(Cat), llvm::cl::desc("print edge lines"));
static llvm::cl::opt<bool> SitesFlag("sites", llvm::cl::cat(Cat),
                                     llvm::cl::desc("add the call site (@L<line> <ExprClass>) to edge lines"));
static llvm::cl::opt<bool> KindsFlag("kinds", llvm::cl::cat(Cat),
                                     llvm::cl::desc("add kind=, noreturn, static to node lines and kind= to edge lines"));
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::cat(Cat),
                                           llvm::cl::desc("only this node and its out-edges (printed name)"));

namespace {

// A CallGraph that remembers which declarations went through the definition door, that is,
// which VisitFunctionDecl / VisitObjCMethodDecl calls ended in addNodeForDecl (the library
// keeps no record of it). CGBuilder reaches lambda call operators through the same virtual
// VisitFunctionDecl, so they are recorded too.
struct DoorGraph : CallGraph {
  std::set<const Decl *> DefDoor; // keys as the graph keys them: canonical decls, ObjC methods as is

  bool VisitFunctionDecl(FunctionDecl *FD) override {
    if (includeInGraph(FD) && FD->isThisDeclarationADefinition()) DefDoor.insert(FD->getCanonicalDecl());
    return CallGraph::VisitFunctionDecl(FD);
  }
  bool VisitObjCMethodDecl(ObjCMethodDecl *MD) override {
    if (includeInGraph(MD)) DefDoor.insert(MD);
    return CallGraph::VisitObjCMethodDecl(MD);
  }

  const char *door(const cglab::Node &N) const {
    if (isa<BlockDecl>(N.D)) return "block";
    return DefDoor.count(N.D) ? "def" : "callee";
  }
};

// The function definitions the main file spells, in source order: no implicit members, no
// template patterns, no specialisations (those are not "added by hand").
void collectDefinitions(const SourceManager &SM, DeclContext *DC, std::vector<FunctionDecl *> &Out) {
  for (Decl *D : DC->decls()) {
    if (D->isImplicit()) continue;
    if (auto *FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->doesThisDeclarationHaveABody() && !FD->isDependentContext() &&
          SM.isInMainFile(SM.getExpansionLoc(FD->getLocation())))
        Out.push_back(FD);
    } else if (isa<NamespaceDecl>(D) || isa<LinkageSpecDecl>(D) || isa<CXXRecordDecl>(D)) {
      collectDefinitions(SM, cast<DeclContext>(D), Out);
    }
  }
}

void setFlags(CallGraph &CG) {
  if (ImplicitFlag >= 0) CG.ShouldVisitImplicitCode = ImplicitFlag;
  if (InstFlag >= 0) CG.ShouldVisitTemplateInstantiations = InstFlag;
  if (TypeLocsFlag >= 0) CG.ShouldWalkTypesOfTypeLocs = TypeLocsFlag;
  if (LambdaBodyFlag >= 0) CG.ShouldVisitLambdaBody = LambdaBodyFlag;
}

bool sameFlags(const CallGraph &A, const CallGraph &B) {
  return A.ShouldVisitImplicitCode == B.ShouldVisitImplicitCode &&
         A.ShouldVisitTemplateInstantiations == B.ShouldVisitTemplateInstantiations &&
         A.ShouldWalkTypesOfTypeLocs == B.ShouldWalkTypesOfTypeLocs &&
         A.ShouldVisitLambdaBody == B.ShouldVisitLambdaBody;
}

// Build CG from the whole translation unit, or one definition at a time (with Steps, the
// "after" lines are printed).
void populate(CallGraph &CG, ASTContext &Ctx, bool Incremental, bool Steps) {
  if (!Incremental) {
    CG.addToCallGraph(Ctx.getTranslationUnitDecl());
    return;
  }
  std::vector<FunctionDecl *> Fns;
  collectDefinitions(Ctx.getSourceManager(), Ctx.getTranslationUnitDecl(), Fns);
  std::set<const CallGraphNode *> Seen; // membership only, never iterated
  for (auto &KV : CG) Seen.insert(KV.second.get());
  for (FunctionDecl *FD : Fns) {
    CG.addToCallGraph(FD);
    std::vector<std::string> New;
    for (auto &KV : CG)
      if (Seen.insert(KV.second.get()).second) New.push_back(cglab::baseName(KV.first));
    std::sort(New.begin(), New.end());
    if (!Steps) continue;
    llvm::outs() << "after " << cglab::quoteName(cglab::baseName(FD)) << ": size=" << CG.size() << " new:";
    if (New.empty()) llvm::outs() << " -";
    for (const std::string &N : New) llvm::outs() << " " << cglab::quoteName(N);
    llvm::outs() << "\n";
  }
}

bool checkBool(const char *Name, int V) {
  if (V >= -1 && V <= 1) return true;
  llvm::errs() << "--" << Name << " must be 0 or 1\n";
  return false;
}

void report(ASTContext &Ctx) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) std::exit(2);
  if (!checkBool("implicit", ImplicitFlag) || !checkBool("instantiations", InstFlag) ||
      !checkBool("typelocs", TypeLocsFlag) || !checkBool("lambda-body", LambdaBodyFlag))
    std::exit(2);
  bool Root = CgWithRootFlag;
  bool Text = Emit == cglab::EmitKind::Text;

  DoorGraph CG;
  setFlags(CG);
  CallGraph Defaults;
  bool Changed = !sameFlags(CG, Defaults);

  if (Text) llvm::outs() << cglab::visitorFlagsLine(CG) << "\n";
  populate(CG, Ctx, IncrementalFlag, Text);
  cglab::Graph G(CG, Ctx, cgGraphOptions());

  auto doorOf = [&](const cglab::Node &N) { return CG.door(N); };

  if (Emit == cglab::EmitKind::Dot) {
    cglab::DotOptions O;
    O.WithRoot = Root;
    O.SiteLabels = SitesFlag;
    O.Sort = Sort;
    if (DoorsFlag)
      O.NodeHook = [&](const cglab::Node &N, cglab::DotAttrs &A) {
        if (N.Id != 0 && std::string(doorOf(N)) == "callee") A.addClass("external");
      };
    cglab::writeDot(llvm::outs(), G, O);
    return;
  }
  if (Emit == cglab::EmitKind::Json) {
    cglab::JsonOptions O;
    O.WithRoot = Root;
    O.Sort = Sort;
    O.NodeHook = [&](const cglab::Node &N, llvm::json::Object &J) { J["door"] = N.Id == 0 ? "" : doorOf(N); };
    llvm::json::Object J = cglab::toJson(G, O);
    J["flags"] = llvm::json::Object{{"implicit", CG.ShouldVisitImplicitCode},
                                    {"instantiations", CG.ShouldVisitTemplateInstantiations},
                                    {"typelocs", CG.ShouldWalkTypesOfTypeLocs},
                                    {"lambda-body", CG.ShouldVisitLambdaBody}};
    llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(J))) << "\n";
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
    const cglab::Node &N = G.node(Id);
    if (Id == G.root()) {
      llvm::outs() << "node <root>\n";
      continue;
    }
    llvm::outs() << "node " << cglab::quoteName(N.Name);
    if (DoorsFlag) llvm::outs() << " door=" << doorOf(N);
    if (KindsFlag) {
      llvm::outs() << " kind=" << cglab::kindName(N.K);
      if (N.Noreturn) llvm::outs() << " noreturn";
      if (N.Static) llvm::outs() << " static";
    }
    llvm::outs() << "\n";
  }
  if (EdgesFlag)
    for (unsigned Id : G.order(Sort, Root)) {
      if (Only && Id != *Only) continue;
      for (unsigned Ei : G.outEdges(Id, Sort, Root))
        cglab::printEdgeLine(llvm::outs(), G, G.edge(Ei), SitesFlag, KindsFlag);
    }

  if (!Changed) return;
  // the same file, the library's default flags: what did the flags change?
  CallGraph Ref;
  populate(Ref, Ctx, IncrementalFlag, false);
  cglab::Graph RG(Ref, Ctx, cgGraphOptions());
  std::set<std::string> Mine, Theirs;
  for (unsigned Id : G.order()) Mine.insert(G.node(Id).Name);
  for (unsigned Id : RG.order()) Theirs.insert(RG.node(Id).Name);
  std::string Delta;
  for (const std::string &N : Theirs)
    if (!Mine.count(N)) Delta += (Delta.empty() ? "-" : " -") + cglab::quoteName(N);
  for (const std::string &N : Mine)
    if (!Theirs.count(N)) Delta += (Delta.empty() ? "+" : " +") + cglab::quoteName(N);
  llvm::outs() << "diff: nodes " << RG.numNodes() << " -> " << G.numNodes();
  if (!Delta.empty()) llvm::outs() << " (" << Delta << ")";
  llvm::outs() << " edges " << RG.numEdges() << " -> " << G.numEdges() << "\n";
}

} // namespace

int main(int argc, const char **argv) { return cglab::runPerAST(argc, argv, Cat, report); }
