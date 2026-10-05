// p05_unreachable -- reachable_code::FindUnreachableCode with our own Callback (Part 5.4).
//
//   p05_unreachable <file> [--func=NAME] [--cfg=all|minimal|default] [--no-prune]
//
// --cfg chooses the alwaysAdd set exactly as Sema does:
//   all      setAllAlwaysAdd()   (what -Wunreachable-code gets)
//   minimal  the seven classes Sema adds when no linearizing analysis is on
//   default  nothing
// The tool also prints the blocks that are unreachable from Entry, using
// reachable_code::ScanReachableFromBlock.

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Analyses/ReachableCode.h"
#include "llvm/ADT/BitVector.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p05_unreachable options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::desc("only this function"),
                                           llvm::cl::cat(Cat));
static llvm::cl::opt<std::string> CfgFlag("cfg", llvm::cl::desc("all (default) | minimal | default"),
                                          llvm::cl::init("all"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> NoPruneFlag("no-prune", llvm::cl::desc("PruneTriviallyFalseEdges=false"),
                                       llvm::cl::cat(Cat));

namespace {

const char *kindName(reachable_code::UnreachableKind K) {
  switch (K) {
  case reachable_code::UK_Return: return "UK_Return";
  case reachable_code::UK_Break: return "UK_Break";
  case reachable_code::UK_Loop_Increment: return "UK_Loop_Increment";
  case reachable_code::UK_Other: return "UK_Other";
  }
  return "?";
}

class Printer : public reachable_code::Callback {
public:
  explicit Printer(ASTContext &C) : SM(C.getSourceManager()) {}
  void HandleUnreachable(reachable_code::UnreachableKind UK, SourceLocation L, SourceRange Cond,
                         SourceRange R1, SourceRange R2, bool HasFallThroughAttr) override {
    ++Count;
    llvm::outs() << "  " << kindName(UK) << " at " << SM.getSpellingLineNumber(L) << ":"
                 << SM.getSpellingColumnNumber(L);
    if (Cond.isValid()) llvm::outs() << "  silenceable-cond=" << SM.getSpellingColumnNumber(Cond.getBegin()) << "-" << SM.getSpellingColumnNumber(Cond.getEnd());
    if (R1.isValid()) llvm::outs() << "  R1=" << SM.getSpellingColumnNumber(R1.getBegin()) << "-" << SM.getSpellingColumnNumber(R1.getEnd());
    if (R2.isValid()) llvm::outs() << "  R2=valid";
    if (HasFallThroughAttr) llvm::outs() << "  fallthrough-attr";
    llvm::outs() << "\n";
  }
  unsigned Count = 0;

private:
  const SourceManager &SM;
};

} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &PP) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        AnalysisDeclContext AC(nullptr, FD);
        CFG::BuildOptions &BO = AC.getCFGBuildOptions();
        BO.PruneTriviallyFalseEdges = !NoPruneFlag;
        BO.AddEHEdges = false;
        BO.AddInitializers = true;
        BO.AddImplicitDtors = true;
        BO.AddTemporaryDtors = true;
        BO.AddCXXNewAllocator = false;
        BO.AddCXXDefaultInitExprInCtors = true;
        if (CfgFlag == "all") {
          BO.setAllAlwaysAdd();
        } else if (CfgFlag == "minimal") {
          BO.setAlwaysAdd(Stmt::BinaryOperatorClass).setAlwaysAdd(Stmt::CompoundAssignOperatorClass)
              .setAlwaysAdd(Stmt::BlockExprClass).setAlwaysAdd(Stmt::CStyleCastExprClass)
              .setAlwaysAdd(Stmt::DeclRefExprClass).setAlwaysAdd(Stmt::ImplicitCastExprClass)
              .setAlwaysAdd(Stmt::UnaryOperatorClass);
        }
        CFG *G = AC.getCFG();
        if (!G) return;

        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        Printer P(Ctx);
        reachable_code::FindUnreachableCode(AC, PP, P);
        if (P.Count == 0) llvm::outs() << "  (callback never called)\n";

        llvm::BitVector Reach(G->getNumBlockIDs());
        unsigned N = reachable_code::ScanReachableFromBlock(&G->getEntry(), Reach);
        llvm::outs() << "  reachable " << N << " of " << G->getNumBlockIDs() << " blocks; unreachable:";
        bool Any = false;
        for (const CFGBlock *B : *G)
          if (!Reach[B->getBlockID()]) { llvm::outs() << " B" << B->getBlockID(); Any = true; }
        llvm::outs() << (Any ? "" : " none") << "\n";
      });
}
