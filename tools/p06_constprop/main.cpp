// p06_constprop -- constant propagation on the FlowSensitive framework (Part 6.1, 6.2)
//
//   build/bin/p06_constprop manifests/p06_constprop.cpp --func=diverge
//   build/bin/p06_constprop manifests/p06_constprop.cpp --func=loop --elements
//
// Prints the lattice at the end of every block (what runDataflowAnalysis
// returns), and with --elements the lattice before and after each statement
// (what CFGEltCallbacks sees).
#include "cfglab.h"
#include "constprop.h"

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

#include <map>

using namespace clang;
using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p06_constprop options");
static llvm::cl::opt<std::string> OptFunc("func", llvm::cl::desc("only this function"),
                                          llvm::cl::init(""), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptElements("elements", llvm::cl::desc("per-element Before/After"),
                                       llvm::cl::cat(Cat));
static llvm::cl::opt<int> OptMaxVisits("max-visits", llvm::cl::desc("MaxBlockVisits"),
                                       llvm::cl::init(kDefaultMaxBlockVisits), llvm::cl::cat(Cat));

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!OptFunc.empty() && FD->getNameAsString() != OptFunc) return;
        auto ACFG = AdornedCFG::build(*FD);
        if (!ACFG) {
          llvm::errs() << "AdornedCFG: " << llvm::toString(ACFG.takeError()) << "\n";
          return;
        }
        const SourceManager &SM = Ctx.getSourceManager();

        DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>());
        Environment Env(DACtx, *FD);
        p06::ConstProp Analysis(Ctx);

        llvm::outs() << "== " << FD->getNameAsString() << "\n";
        // The post-analysis callbacks run block by block in block-ID order (the
        // reverse of execution order), so buffer the lines per block and print
        // them entry -> exit.
        std::map<unsigned, std::vector<std::string>> Lines;
        auto record = [&](const CFGElement &E, const char *When,
                          const DataflowAnalysisState<p06::Lat> &St) {
          auto S = E.getAs<CFGStmt>();
          if (!S) return;
          const Stmt *X = S->getStmt();
          if (!isa<DeclStmt>(X) && !isa<BinaryOperator>(X) && !isa<UnaryOperator>(X) &&
              !isa<ReturnStmt>(X))
            return;
          const CFGBlock *B = ACFG->blockForStmt(*X);
          std::string Line = std::string(When) + " " + cfglab::stmtText(X, Ctx, 24);
          Line.resize(std::max<size_t>(Line.size(), 34), ' ');
          Lines[B ? B->getBlockID() : 999].push_back(Line + "[" + p06::show(St.Lattice, SM) + "]");
        };
        CFGEltCallbacks<p06::ConstProp> CB;
        if (OptElements) {
          CB.Before = [&](const CFGElement &E, const DataflowAnalysisState<p06::Lat> &St) {
            record(E, "before", St);
          };
          CB.After = [&](const CFGElement &E, const DataflowAnalysisState<p06::Lat> &St) {
            record(E, "after ", St);
          };
        } else {
          // Report the value of a returned expression.
          CB.Before = [&](const CFGElement &E, const DataflowAnalysisState<p06::Lat> &St) {
            if (auto S = E.getAs<CFGStmt>())
              if (auto *R = dyn_cast<ReturnStmt>(S->getStmt()); R && R->getRetValue())
                llvm::outs() << "  return value: " << p06::eval(R->getRetValue(), St.Lattice).str()
                             << "\n";
          };
        }

        auto Res = runDataflowAnalysis(*ACFG, Analysis, Env, CB, OptMaxVisits);
        if (!Res) {
          llvm::outs() << "  analysis failed: " << llvm::toString(Res.takeError()) << "\n";
          return;
        }
        for (auto It = Lines.rbegin(); It != Lines.rend(); ++It) {
          llvm::outs() << "  B" << It->first << "\n";
          for (auto &L : It->second) llvm::outs() << "    " << L << "\n";
        }
        const CFG &G = ACFG->getCFG();
        for (const CFGBlock *B : llvm::reverse(G)) { // roughly entry -> exit
          unsigned ID = B->getBlockID();
          llvm::outs() << "  B" << ID << ": ";
          if (!(*Res)[ID]) llvm::outs() << "(no state)\n";
          else llvm::outs() << p06::show((*Res)[ID]->Lattice, SM) << "\n";
        }
      });
}
