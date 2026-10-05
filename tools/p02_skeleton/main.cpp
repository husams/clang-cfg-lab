// p02_skeleton -- the smallest complete CFG tool (Part 2.1).
//
// For every function definition in the main file: build its CFG with default
// BuildOptions and print how big it is. With -dump also print the CFG text,
// with -conditions also build a CFG for each `if` condition on its own.
//
//   build/bin/p02_skeleton manifests/p01_hello.cpp
//   build/bin/p02_skeleton manifests/p01_hello.cpp -dump
//   build/bin/p02_skeleton manifests/p01_basic.cpp -conditions
//
// This file spells out every layer of a LibTooling tool. Later tools hide the
// layers behind cfglab::runPerFunction (tools/common/cfglab.h).

#include "cfglab.h"

#include "clang/Analysis/CFG.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p02_skeleton options");
static llvm::cl::opt<bool> DumpCFG("dump", llvm::cl::desc("print the CFG text"),
                                   llvm::cl::cat(Cat));
static llvm::cl::opt<bool>
    Conditions("conditions",
               llvm::cl::desc("also build a CFG for each if-condition alone"),
               llvm::cl::cat(Cat));

// Layer 4: the visitor. A CFG is NOT part of the AST -- you build one on
// demand from a Stmt (usually a function body).
class Visitor : public RecursiveASTVisitor<Visitor> {
public:
  explicit Visitor(ASTContext &Ctx) : Ctx(Ctx) {}

  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (!cfglab::isInteresting(FD, Ctx))
      return true;

    // CFG::BuildOptions() is "everything off" except PruneTriviallyFalseEdges.
    CFG::BuildOptions BO;

    // buildCFG(Decl, Stmt, ASTContext*, BuildOptions). The Decl is only the
    // context the Stmt lives in; the Stmt is what gets lowered.
    std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
    if (!G) { // nullptr == the builder gave up (e.g. a body it cannot model)
      llvm::errs() << "buildCFG failed for " << FD->getNameAsString() << "\n";
      return true;
    }

    llvm::outs() << FD->getQualifiedNameAsString() << ": " << G->size()
                 << " blocks, entry=B" << G->getEntry().getBlockID()
                 << " exit=B" << G->getExit().getBlockID() << "\n";

    if (DumpCFG) {
      // CFG::dump() writes to llvm::errs(); print() lets us choose the stream
      // so the output interleaves correctly with the lines above.
      G->print(llvm::outs(), Ctx.getLangOpts(), /*ShowColors=*/false);
    }

    if (Conditions)
      dumpConditionCFGs(FD);
    return true;
  }

private:
  // A CFG can be built from any Stmt -- here, from a bare condition
  // expression. The result shows exactly how && / || / ?: split blocks.
  void dumpConditionCFGs(FunctionDecl *FD) {
    struct Finder : RecursiveASTVisitor<Finder> {
      std::vector<IfStmt *> Ifs;
      bool VisitIfStmt(IfStmt *S) { Ifs.push_back(S); return true; }
    } F;
    F.TraverseStmt(FD->getBody());
    for (IfStmt *IS : F.Ifs) {
      Expr *Cond = IS->getCond();
      std::unique_ptr<CFG> G = CFG::buildCFG(FD, Cond, &Ctx, CFG::BuildOptions());
      if (!G) continue;
      llvm::outs() << "  condition `" << cfglab::stmtText(Cond, Ctx) << "` (line "
                   << cfglab::lineOf(Ctx.getSourceManager(), Cond->getBeginLoc())
                   << "): " << G->size() << " blocks\n";
      if (DumpCFG) G->print(llvm::outs(), Ctx.getLangOpts(), false);
    }
  }

  ASTContext &Ctx;
};

// Layer 3: the consumer sees the finished AST.
class Consumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &Ctx) override {
    Visitor(Ctx).TraverseDecl(Ctx.getTranslationUnitDecl());
  }
};

// Layer 2: the frontend action creates the consumer.
class Action : public ASTFrontendAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &,
                                                 StringRef) override {
    return std::make_unique<Consumer>();
  }
};

// Layer 1: main() -- cfglab::runTool parses argv with CommonOptionsParser,
// adds the macOS platform flags and runs the action.
int main(int argc, const char **argv) {
  return cfglab::runTool<Action>(argc, argv, Cat);
}
