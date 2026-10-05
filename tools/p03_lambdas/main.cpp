// p03_lambdas -- which function-like declarations get a CFG? (Part 3.7)
//
//   p03_lambdas <file> [--instantiations] [--std=c++NN via -- flags]
//
// Visits EVERY FunctionDecl with a body in the main file, including lambda call
// operators and (with --instantiations) template instantiations, and for each:
//   * classifies it
//   * builds a CFG with CFG::buildCFG (analyzer preset) and prints the block count
//   * asks dataflow::AdornedCFG::build and prints its verdict

#include "cfglab.h"

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p03_lambdas options");
static llvm::cl::opt<bool> Dump("dump", llvm::cl::cat(Cat),
                                llvm::cl::desc("print each CFG (analyzer preset) below its line"));
static llvm::cl::opt<std::string> NameSub("name", llvm::cl::cat(Cat),
                                          llvm::cl::desc("only functions whose printed name contains this"));
static llvm::cl::opt<bool> Inst("instantiations", llvm::cl::cat(Cat),
                                llvm::cl::desc("also visit template instantiations"));

namespace {
struct V : RecursiveASTVisitor<V> {
  ASTContext &Ctx;
  explicit V(ASTContext &C) : Ctx(C) {}
  bool shouldVisitTemplateInstantiations() const { return Inst; }
  // The closure class and its operator() are implicit declarations.
  bool shouldVisitImplicitCode() const { return true; }

  static std::string kinds(const FunctionDecl *FD) {
    std::string K;
    auto Add = [&](const char *S) { K += (K.empty() ? "" : ",") + std::string(S); };
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD); MD && MD->getParent()->isLambda()) {
      if (MD->getParent()->isGenericLambda()) Add("generic-lambda-operator()");
      else Add("lambda-operator()");
    }
    if (FD->isTemplated()) Add("templated");
    if (FD->isTemplateInstantiation()) Add("instantiation");
    if (K.empty()) Add("plain");
    return K;
  }

  bool VisitFunctionDecl(FunctionDecl *FD) {
    const auto *Op = dyn_cast<CXXMethodDecl>(FD);
    bool IsLambdaOp = Op && Op->getParent()->isLambda() && Op->getOverloadedOperator() == OO_Call;
    if (!FD->doesThisDeclarationHaveABody() || (FD->isImplicit() && !IsLambdaOp)) return true;
    if (!Ctx.getSourceManager().isInMainFile(FD->getLocation())) return true;
    std::string Name = FD->getQualifiedNameAsString();
    if (const auto *MD = dyn_cast<CXXMethodDecl>(FD); MD && MD->getParent()->isLambda())
      Name = "<lambda@L" + std::to_string(cfglab::lineOf(Ctx.getSourceManager(), MD->getParent()->getLocation())) +
             ">::operator()";
    if (!NameSub.empty() && Name.find(NameSub) == std::string::npos) return true;
    llvm::outs() << llvm::format("%-28s", Name.c_str()) << " " << llvm::format("%-42s", kinds(FD).c_str());

    CFG::BuildOptions BO = cfglab::analyzerPreset();
    if (std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO)) {
      llvm::outs() << " buildCFG=" << G->size() << " blocks";
      if (Dump) { llvm::outs() << "\n"; G->print(llvm::outs(), Ctx.getLangOpts(), false); }
    }
    else
      llvm::outs() << " buildCFG=null";

    llvm::Expected<dataflow::AdornedCFG> A = dataflow::AdornedCFG::build(*FD);
    if (A) {
      llvm::outs() << "  AdornedCFG=ok";
    } else {
      std::string Msg = llvm::toString(A.takeError());
      llvm::outs() << "  AdornedCFG=error(" << Msg << ")";
    }
    llvm::outs() << "\n";
    return true;
  }
};
struct C : ASTConsumer {
  void HandleTranslationUnit(ASTContext &Ctx) override { V(Ctx).TraverseDecl(Ctx.getTranslationUnitDecl()); }
};
struct Act : ASTFrontendAction {
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef) override {
    return std::make_unique<C>();
  }
};
} // namespace

int main(int argc, const char **argv) { return cfglab::runTool<Act>(argc, argv, Cat); }
