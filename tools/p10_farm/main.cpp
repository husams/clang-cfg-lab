// p10_farm -- the bodies BodyFarm synthesizes for functions that have none (Part 10.7).
//
//   p10_farm <file> [--func=NAME] [--body=false] [--cfg=false] [-- <compile flags>]
//
// The Static Analyzer asks BodyFarm for a body when a well-known function (dispatch_once,
// std::call_once, ...) has none that it could use: the farm builds the AST of what the
// function does. This tool creates an AnalysisDeclContextManager with synthesizeBodies=true,
// as the analyzer does, and asks it for the body of every function declared in the main file.
// Text output, one record per function (a function declared twice prints once):
//   farm <name>: synthesized=yes|no [kind=dispatch_once|call_once|objc-getter|other]
//   <the synthesized body, pretty-printed>        (--body; only when synthesized=yes)
//   <its CFG, as CFG::print writes it (Part 2)>   (--cfg; only when synthesized=yes)
// A function template is reached through its instantiations: std::call_once<...> has a body
// only for the arguments it was called with. --func matches the printed name or the
// qualified name without template arguments (std::call_once).

#include "cglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p10_farm options");
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat),
                                          llvm::cl::desc("only this function (printed name or qualified name)"));
static llvm::cl::opt<bool> BodyOpt("body", llvm::cl::init(true), llvm::cl::cat(Cat),
                                   llvm::cl::desc("print the synthesized body (--body=false to skip)"));
static llvm::cl::opt<bool> CfgOpt("cfg", llvm::cl::init(true), llvm::cl::cat(Cat),
                                  llvm::cl::desc("print the CFG of the synthesized body (--cfg=false to skip)"));

namespace {

const char *kindOf(const Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->getIdentifier()) {
      if (FD->getName() == "dispatch_once") return "dispatch_once";
      if (FD->getName() == "call_once") return "call_once";
    }
    return "other";
  }
  return "objc-getter"; // BodyFarm::getBody(const ObjCMethodDecl *) only builds property getters
}

class FarmVisitor : public RecursiveASTVisitor<FarmVisitor> {
public:
  FarmVisitor(ASTContext &Ctx, AnalysisDeclContextManager &Mgr) : Ctx(Ctx), Mgr(Mgr) {}

  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return true; } // the getter an @property declares is implicit

  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (FD->isImplicit() || FD->isTemplated()) return true; // a template pattern has no body to farm
    return visit(FD);
  }
  bool VisitObjCMethodDecl(ObjCMethodDecl *MD) { return visit(MD); }

private:
  bool visit(const Decl *D) {
    if (!cglab::inMainFile(Ctx.getSourceManager(), D)) return true;
    if (!Seen.insert(D->getCanonicalDecl()).second) return true;
    std::string Name = cglab::baseName(D);
    if (!FuncOpt.empty() && FuncOpt != Name) {
      const auto *ND = dyn_cast<NamedDecl>(D);
      if (!ND || FuncOpt != ND->getQualifiedNameAsString()) return true;
    }
    AnalysisDeclContext *ADC = Mgr.getContext(D);
    bool Synthesized = false;
    Stmt *Body = ADC->getBody(Synthesized); // ADC->isBodyAutosynthesized() asks the same question
    llvm::outs() << "farm " << cglab::quoteName(Name) << ": synthesized=" << (Synthesized ? "yes" : "no");
    if (Synthesized) llvm::outs() << " kind=" << kindOf(D);
    llvm::outs() << "\n";
    if (!Synthesized) return true;
    if (BodyOpt) {
      // printPretty ends a compound statement with a newline and an expression without one
      std::string Text;
      llvm::raw_string_ostream TS(Text);
      Body->printPretty(TS, nullptr, Ctx.getPrintingPolicy());
      TS.flush();
      llvm::outs() << llvm::StringRef(Text).rtrim() << "\n";
    }
    if (CfgOpt)
      if (CFG *G = ADC->getCFG()) G->print(llvm::outs(), Ctx.getLangOpts(), false); // starts with a blank line
    return true;
  }

  ASTContext &Ctx;
  AnalysisDeclContextManager &Mgr;
  std::set<const Decl *> Seen; // canonical declarations already printed; never iterated
};

struct Consumer : ASTConsumer {
  void HandleTranslationUnit(ASTContext &Ctx) override {
    // The analyzer's own settings (AnalyzerOptions defaults): implicit destructors, initializers
    // and temporary destructors in the CFG, bodies synthesized.
    AnalysisDeclContextManager Mgr(Ctx, /*useUnoptimizedCFG=*/false, /*addImplicitDtors=*/true,
                                   /*addInitializers=*/true, /*addTemporaryDtors=*/true,
                                   /*addLifetime=*/false, /*addLoopExit=*/false, /*addScopes=*/false,
                                   /*synthesizeBodies=*/true);
    FarmVisitor(Ctx, Mgr).TraverseDecl(Ctx.getTranslationUnitDecl());
  }
};

struct Action : ASTFrontendAction {
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef) override {
    return std::make_unique<Consumer>();
  }
};

} // namespace

int main(int argc, const char **argv) { return cfglab::runTool<Action>(argc, argv, Cat); }
