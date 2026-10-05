// p02_stmtmap -- CFGStmtMap + ParentMap: AST node -> CFG block (Part 2.7).
//
//   p02_stmtmap <file> [--preset=..] [--set=..] [--clear=..] [--always-add=..]
//                      [--func=NAME] [--line=N] [--adc] [--all]
//
// For every statement in a function body, print which block
// CFGStmtMap::getBlock() says it lives in, and *why*:
//   element     S itself is a CFGStmt element of that block
//   terminator  S is that block's terminator (wins over `element`)
//   label       S is that block's label (case / label / catch)
//   ancestor    S is not in the map; getBlock() walked up the ParentMap
//   none        no ancestor is in the map either (getBlock returned nullptr)
//
//   --line=N  only statements that start on line N
//   --adc     get the CFG, ParentMap and CFGStmtMap from AnalysisDeclContext
//             instead of building them by hand (the options then come from
//             AnalysisDeclContextManager's defaults, not from --preset)
//   --all     include ImplicitCastExpr / ParenExpr nodes

#include "cfglab.h"

#include "clang/AST/ParentMap.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFGStmtMap.h"

#include "llvm/ADT/DenseMap.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p02_stmtmap options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat)
static llvm::cl::opt<unsigned> OnlyLine("line", llvm::cl::cat(Cat),
                                        llvm::cl::desc("only statements starting on this line"));
static llvm::cl::opt<bool> UseADC("adc", llvm::cl::cat(Cat),
                                  llvm::cl::desc("use AnalysisDeclContext's CFG/ParentMap/CFGStmtMap"));
static llvm::cl::opt<bool> ShowAll("all", llvm::cl::cat(Cat),
                               llvm::cl::desc("include implicit casts and parens"));

namespace {
struct Collect : RecursiveASTVisitor<Collect> {
  std::vector<const Stmt *> Stmts;
  bool shouldVisitImplicitCode() const { return true; }
  bool VisitStmt(Stmt *S) {
    Stmts.push_back(S);
    return true;
  }
};
} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!flagWantsFunction(FD)) return;
        const SourceManager &SM = Ctx.getSourceManager();

        // Either route yields a CFG, a ParentMap and a CFGStmtMap.
        std::unique_ptr<CFG> Owned;
        std::unique_ptr<ParentMap> OwnedPM;
        std::unique_ptr<CFGStmtMap> OwnedMap;
        AnalysisDeclContextManager Mgr(Ctx);
        const CFG *G = nullptr;
        const CFGStmtMap *Map = nullptr;
        if (UseADC) {
          AnalysisDeclContext *AC = Mgr.getContext(FD);
          // The CFG is built lazily on first request, and cached. Change
          // AC->getCFGBuildOptions() *before* this call or not at all.
          G = AC->getCFG();
          Map = AC->getCFGStmtMap();
        } else {
          CFG::BuildOptions BO;
          if (!flagsToOptions(BO)) std::exit(2);
          Owned = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
          if (!Owned) return;
          OwnedPM = std::make_unique<ParentMap>(FD->getBody());
          // 22.x: a constructor. (Older releases had CFGStmtMap::Build.)
          OwnedMap = std::make_unique<CFGStmtMap>(*Owned, *OwnedPM);
          G = Owned.get();
          Map = OwnedMap.get();
        }
        if (!G) return;

        // Independent bookkeeping so we can classify *why* a stmt maps where it does.
        llvm::DenseMap<const Stmt *, const CFGBlock *> Elem, Term, Label;
        for (const CFGBlock *B : *G) {
          for (const CFGElement &E : *B)
            if (auto CS = E.getAs<CFGStmt>()) Elem[CS->getStmt()] = B;
          if (const Stmt *T = B->getTerminatorStmt()) Term[T] = B;
          if (const Stmt *L = B->getLabel()) Label[L] = B;
        }

        llvm::outs() << "== " << FD->getQualifiedNameAsString() << (UseADC ? "  (AnalysisDeclContext)" : "")
                     << "\n";
        Collect C;
        C.TraverseStmt(const_cast<Stmt *>(FD->getBody()));
        for (const Stmt *S : C.Stmts) {
          if (!ShowAll && (isa<ImplicitCastExpr>(S) || isa<ParenExpr>(S))) continue;
          unsigned Line = cfglab::lineOf(SM, S->getBeginLoc());
          if (OnlyLine && Line != OnlyLine) continue;

          const CFGBlock *B = Map->getBlock(S);
          const char *Why = !B ? "none"
                            : Term.count(S) ? "terminator"
                            : Label.count(S) ? "label"
                            : Elem.count(S) ? "element"
                                            : "ancestor";
          std::string Text = cfglab::stmtText(S, Ctx, 36);
          llvm::outs() << "  line " << llvm::format("%-3u", Line) << " "
                       << llvm::format("%-22s", S->getStmtClassName()) << " "
                       << llvm::format("%-38s", Text.c_str()) << " -> " << llvm::format("%-5s", cfglab::blockName(B).c_str())
                       << " " << Why << "\n";
        }
      });
}
