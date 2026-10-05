// p03_eh -- try-dispatch blocks, catch blocks, throw blocks (Part 3.6).
//
//   p03_eh <file> [--func=NAME] [--preset=..] [--set=..] [--clear=..]
//
// Options start from --preset (default: the plain defaults); --set=AddEHEdges is
// the interesting switch. Output, per function:
//
//   == name: N blocks, T try-dispatch block(s)
//   try B2  CXXTryStmt@L14  preds=[B6]
//     handler B3  catch (int)
//     handler B5  catch (...)
//     unmatched -> B0
//   throw B4  CXXThrowExpr rethrow=0  -> B2
//   noreturn B7 -> B0

#include "cfglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p03_eh options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat)

static std::string catchText(const CXXCatchStmt *C) {
  return C->getExceptionDecl() ? "catch (" + C->getCaughtType().getAsString() + ")"
                               : std::string("catch (...)");
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!flagWantsFunction(FD)) return;
        const SourceManager &SM = Ctx.getSourceManager();
        CFG::BuildOptions BO;
        if (!flagsToOptions(BO)) std::exit(2);
        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) return;
        unsigned NTry = 0;
        for (const CFGBlock *T : G->try_blocks()) { (void)T; ++NTry; }
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << ": " << G->size() << " blocks, "
                     << NTry << " try-dispatch block(s), AddEHEdges=" << BO.AddEHEdges << "\n";

        for (const CFGBlock *T : G->try_blocks()) {
          llvm::outs() << "try " << cfglab::blockName(T) << "  " << T->getTerminatorStmt()->getStmtClassName()
                       << "@L" << cfglab::lineOf(SM, T->getTerminatorStmt()->getBeginLoc()) << "  preds=[";
          bool First = true;
          for (const CFGBlock::AdjacentBlock &P : T->preds()) {
            llvm::outs() << (First ? "" : ", ") << cfglab::blockName(P);
            First = false;
          }
          llvm::outs() << "]\n";
          for (const CFGBlock::AdjacentBlock &S : T->succs()) {
            const CFGBlock *B = S;
            if (!B) { llvm::outs() << "  (pruned)\n"; continue; }
            if (auto *C = dyn_cast_or_null<CXXCatchStmt>(B->getLabel()))
              llvm::outs() << "  handler " << cfglab::blockName(B) << "  " << catchText(C) << "\n";
            else
              llvm::outs() << "  unmatched -> " << cfglab::blockName(B) << (B == &G->getExit() ? " (exit)" : "")
                           << "\n";
          }
        }

        for (const CFGBlock *B : *G) {
          for (const CFGElement &E : *B) {
            auto S = E.getAs<CFGStmt>();
            if (!S) continue;
            if (auto *Th = dyn_cast<CXXThrowExpr>(S->getStmt())) {
              llvm::outs() << "throw " << cfglab::blockName(B) << "  rethrow=" << (Th->getSubExpr() == nullptr)
                           << "  ->";
              for (const CFGBlock::AdjacentBlock &Su : B->succs())
                llvm::outs() << " " << cfglab::blockName(Su);
              llvm::outs() << "\n";
            }
          }
          if (B->hasNoReturnElement()) {
            llvm::outs() << "noreturn " << cfglab::blockName(B) << " ->";
            for (const CFGBlock::AdjacentBlock &Su : B->succs())
              llvm::outs() << " " << cfglab::blockName(Su);
            llvm::outs() << "\n";
          }
        }
      });
}
