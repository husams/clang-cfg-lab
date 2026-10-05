// p02_walk -- traverse CFG -> CFGBlock -> CFGElement (Part 2.4).
//
//   p02_walk <file> [--preset=..] [--set=..] [--clear=..] [--always-add=..]
//                   [--func=NAME] [--reverse] [--refs] [--by-kind]
//
// Default output, one block per paragraph, one element per line:
//   B3  elements=7
//     [0] Statement            DeclRefExpr       x
//     ...
// For every element we switch on getKind() and then use getAs<T>() to reach
// the kind-specific accessors.

#include "cfglab.h"

#include <map>

using namespace clang;

static llvm::cl::OptionCategory Cat("p02_walk options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat)
static llvm::cl::opt<bool> Reverse("reverse", llvm::cl::cat(Cat),
                                   llvm::cl::desc("iterate elements with rbegin()/rend()"));
static llvm::cl::opt<bool> Refs("refs", llvm::cl::cat(Cat),
                                llvm::cl::desc("iterate with refs() (ElementRef)"));
static llvm::cl::opt<bool> ByKind("by-kind", llvm::cl::cat(Cat),
                                  llvm::cl::desc("print element-kind totals only"));

static void line(size_t Index, const CFGElement &E, ASTContext &Ctx) {
  llvm::outs() << "  [" << llvm::format("%2zu", Index) << "] " << llvm::format("%-20s", cfglab::kindName(E.getKind()))
               << " " << cfglab::elementDetail(E, Ctx) << "\n";
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!flagWantsFunction(FD)) return;
        CFG::BuildOptions BO;
        if (!flagsToOptions(BO)) std::exit(2);
        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) return;

        llvm::outs() << "== " << FD->getQualifiedNameAsString() << ": " << G->size() << " blocks\n";
        std::map<std::string, unsigned> Totals;

        // Blocks: the CFG is iterable. Order is creation order, which is
        // *reverse* of execution order, so Entry is the last block and Exit
        // (id 0) is the first. Block IDs are stable within one build.
        for (const CFGBlock *B : *G) {
          for (const CFGElement &E : *B) ++Totals[cfglab::kindName(E.getKind())];
          if (ByKind) continue;

          llvm::outs() << cfglab::blockName(B) << "  elements=" << B->size()
                       << (B == &G->getEntry() ? "  (ENTRY)" : "")
                       << (B == &G->getExit() ? "  (EXIT)" : "") << "\n";

          if (Refs) {
            // ElementRef = (block, index) -- a stable handle you can store,
            // compare, and put in a map, unlike a CFGElement value.
            for (CFGBlock::ConstCFGElementRef R : B->refs())
              line(R.getIndexInBlock(), *R, Ctx);
          } else if (Reverse) {
            size_t I = B->size();
            for (auto It = B->rbegin(); It != B->rend(); ++It) line(--I, *It, Ctx);
          } else {
            size_t I = 0;
            for (const CFGElement &E : *B) line(I++, E, Ctx);
          }
        }
        if (ByKind)
          for (const auto &[K, N] : Totals)
            llvm::outs() << "  " << llvm::format("%-20s", K.c_str()) << " " << N << "\n";
      });
}
