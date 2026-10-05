// pNN_name -- one-line purpose (Part N.M). Copy this directory to tools/pNN_<name>/.
#include "cfglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("pNN_name options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat) // gives --preset --set --clear --always-add --func

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &PP) {
        if (!flagWantsFunction(FD)) return;
        CFG::BuildOptions BO;
        if (!flagsToOptions(BO)) std::exit(2);
        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) return;
        llvm::outs() << FD->getQualifiedNameAsString() << ": " << G->size() << " blocks\n";
      });
}
