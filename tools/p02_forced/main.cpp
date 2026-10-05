// p02_forced -- BuildOptions::forcedBlkExprs and setAlwaysAdd (Part 2.3).
//
//   p02_forced <file> [--func=NAME] [--register=DeclRefExpr]
//
// By default the CFG folds sub-expressions into their parent (a DeclRefExpr
// is not a CFGStmt element). forcedBlkExprs lets a client say "make THESE
// particular statements elements, and tell me which block each one landed
// in". It is a pointer to a pointer: you allocate the map, fill in the keys
// before building, and read the blocks after.

#include "cfglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p02_forced options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::cat(Cat));
static llvm::cl::opt<std::string> Register("register", llvm::cl::init("DeclRefExpr"),
                                           llvm::cl::cat(Cat),
                                           llvm::cl::desc("Stmt class to register"));

namespace {
struct Collect : RecursiveASTVisitor<Collect> {
  Stmt::StmtClass Want;
  std::vector<const Stmt *> Found;
  explicit Collect(Stmt::StmtClass W) : Want(W) {}
  bool VisitStmt(Stmt *S) {
    if (S->getStmtClass() == Want) Found.push_back(S);
    return true;
  }
};
} // namespace

static unsigned countElements(const CFG &G) {
  unsigned N = 0;
  for (const CFGBlock *B : G) N += B->size();
  return N;
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        auto It = cfglab::stmtClassesByName().find(Register);
        if (It == cfglab::stmtClassesByName().end()) {
          llvm::errs() << "unknown Stmt class '" << Register << "'\n";
          std::exit(2);
        }
        const SourceManager &SM = Ctx.getSourceManager();

        // Baseline: default options.
        std::unique_ptr<CFG> Base = CFG::buildCFG(FD, FD->getBody(), &Ctx, CFG::BuildOptions());

        // 1. allocate the map, 2. register the statements, 3. point the
        // options at it, 4. build, 5. read the blocks, 6. delete the map.
        CFG::BuildOptions::ForcedBlkExprs *Forced = new CFG::BuildOptions::ForcedBlkExprs();
        Collect C(It->second);
        C.TraverseStmt(const_cast<Stmt *>(FD->getBody()));
        for (const Stmt *S : C.Found) (*Forced)[S]; // value-initialized: null block

        CFG::BuildOptions BO;
        BO.forcedBlkExprs = &Forced; // note: CFG::BuildOptions::ForcedBlkExprs **
        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) { delete Forced; return; }

        llvm::outs() << "== " << FD->getQualifiedNameAsString() << ": " << Register << " nodes="
                     << C.Found.size() << "  elements default=" << countElements(*Base)
                     << " forced=" << countElements(*G) << "\n";
        for (const Stmt *S : C.Found) {
          const CFGBlock *B = (*Forced)[S];
          llvm::outs() << "  line " << cfglab::lineOf(SM, S->getBeginLoc()) << "  `"
                       << cfglab::stmtText(S, Ctx) << "` -> " << cfglab::blockName(B) << "\n";
        }
        delete Forced;
      });
}
