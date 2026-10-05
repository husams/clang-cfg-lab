// p02_options -- toggle CFG::BuildOptions from the command line (Part 2.2, 2.3).
//
//   p02_options <file> [--preset=NAME] [--set=F1,F2] [--clear=F1,F2]
//                      [--always-add=Class1,Class2|all] [--func=NAME]
//                      [--summary] [--fields]
//
//   --preset      default | sema | adorned | analyzer | kitchen | none
//   --set/--clear BuildOptions field names (see --fields), comma separated
//   --always-add  Stmt classes to force into the CFG as their own elements
//                 (BuildOptions::setAlwaysAdd), or `all` (setAllAlwaysAdd)
//   --summary     print counts instead of the whole CFG
//   --fields      print every BuildOptions field with its current value

#include "cfglab.h"

#include <map>
#include <set>

using namespace clang;

static llvm::cl::OptionCategory Cat("p02_options options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat)
static llvm::cl::opt<bool> Summary("summary", llvm::cl::cat(Cat), llvm::cl::desc("print element counts instead of the CFG"));
static llvm::cl::opt<bool> Fields("fields", llvm::cl::cat(Cat), llvm::cl::desc("print every BuildOptions field and exit"));

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat,
      [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!flagWantsFunction(FD)) return;
        CFG::BuildOptions BO;
        if (!flagsToOptions(BO)) std::exit(2);

        if (Fields) {
          llvm::outs() << "== " << FD->getQualifiedNameAsString() << " (preset " << PresetFlag << ")\n";
          for (const cfglab::OptionField &F : cfglab::optionFields())
            llvm::outs() << "  " << (BO.*(F.Ptr) ? "[x] " : "[ ] ") << F.Name << "\n";
          // The alwaysAdd mask has no "list all" getter -- only
          // alwaysAdd(const Stmt*) -- so probe the statements that occur in
          // this function.
          struct Probe : RecursiveASTVisitor<Probe> {
            const CFG::BuildOptions &BO;
            std::set<std::string> On;
            explicit Probe(const CFG::BuildOptions &B) : BO(B) {}
            bool VisitStmt(Stmt *S) {
              if (BO.alwaysAdd(S)) On.insert(S->getStmtClassName());
              return true;
            }
          } P(BO);
          P.TraverseStmt(const_cast<Stmt *>(FD->getBody()));
          llvm::outs() << "  alwaysAdd (of the classes present here):";
          if (P.On.empty()) llvm::outs() << " none";
          for (const std::string &N : P.On) llvm::outs() << " " << N;
          llvm::outs() << "\n";
          std::exit(0);
        }

        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) { llvm::errs() << "buildCFG failed\n"; return; }

        if (!Summary) {
          llvm::outs() << FD->getQualifiedNameAsString() << "\n";
          G->print(llvm::outs(), Ctx.getLangOpts(), false);
          return;
        }
        std::map<std::string, unsigned> Hist;
        unsigned Total = 0;
        for (const CFGBlock *B : *G)
          for (const CFGElement &E : *B) {
            ++Hist[cfglab::kindName(E.getKind())];
            ++Total;
          }
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << ": blocks=" << G->size()
                     << " elements=" << Total << " linear=" << G->isLinear() << "\n";
        for (const auto &[K, N] : Hist)
          llvm::outs() << "   " << llvm::format("%-20s", K.c_str()) << " " << N << "\n";
      });
}
