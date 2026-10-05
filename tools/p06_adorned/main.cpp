// p06_adorned -- AdornedCFG and runDataflowAnalysis, piece by piece (Part 6.2)
//
//   build/bin/p06_adorned manifests/p06_adorned.cpp --func=ternary
//   build/bin/p06_adorned manifests/p06_adorned.cpp --func=ternary --callbacks
//   build/bin/p06_adorned manifests/p06_adorned.cpp --func=ternary --max-visits=3
//   build/bin/p06_adorned manifests/p06_adorned.cpp --try-template
//   build/bin/p06_adorned manifests/p06_adorned.c
#include "cfglab.h"

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/NoopAnalysis.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

#include <algorithm>

using namespace clang;
using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p06_adorned options");
static llvm::cl::opt<std::string> OptFunc("func", llvm::cl::desc("only this function"),
                                          llvm::cl::init(""), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptCallbacks("callbacks", llvm::cl::desc("print every Before/After callback"),
                                        llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptTryTemplate("try-template", llvm::cl::desc("AdornedCFG::build on a function template"),
                                          llvm::cl::cat(Cat));
static llvm::cl::opt<int> OptMaxVisits("max-visits", llvm::cl::desc("MaxBlockVisits"),
                                       llvm::cl::init(kDefaultMaxBlockVisits), llvm::cl::cat(Cat));

static bool Done = false;

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (OptTryTemplate) {
          if (Done) return;
          Done = true;
          for (Decl *D : Ctx.getTranslationUnitDecl()->decls())
            if (auto *FT = dyn_cast<FunctionTemplateDecl>(D)) {
              auto R = AdornedCFG::build(*FT->getTemplatedDecl());
              llvm::outs() << "AdornedCFG::build(" << FT->getNameAsString() << " [template pattern]): "
                           << (R ? std::string("ok") : "error: " + llvm::toString(R.takeError())) << "\n";
            }
          return;
        }
        if (!OptFunc.empty() && FD->getNameAsString() != OptFunc) return;

        llvm::outs() << "== " << FD->getNameAsString() << "\n";
        auto ACFG = AdornedCFG::build(*FD);
        if (!ACFG) {
          llvm::outs() << "  AdornedCFG::build failed: " << llvm::toString(ACFG.takeError()) << "\n";
          return;
        }
        const CFG &G = ACFG->getCFG();

        DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>());
        Environment Env(DACtx, *FD);
        NoopAnalysis A(Ctx);

        std::vector<std::string> Calls; // execution-order callback trace
        auto tag = [&](const char *When, const CFGElement &E) {
          auto S = E.getAs<CFGStmt>();
          if (!S) return;
          const CFGBlock *B = ACFG->blockForStmt(*S->getStmt());
          Calls.push_back("B" + std::to_string(B ? B->getBlockID() : 999) + " " + When + " " +
                          cfglab::stmtText(S->getStmt(), Ctx, 28));
        };
        CFGEltCallbacks<NoopAnalysis> CB;
        if (OptCallbacks) {
          CB.Before = [&](const CFGElement &E, const DataflowAnalysisState<NoopLattice> &) { tag("Before", E); };
          CB.After = [&](const CFGElement &E, const DataflowAnalysisState<NoopLattice> &) { tag("After ", E); };
        }

        auto Res = runDataflowAnalysis(*ACFG, A, Env, CB, OptMaxVisits);
        if (!Res) {
          llvm::outs() << "  runDataflowAnalysis failed: " << llvm::toString(Res.takeError()) << "\n";
          return;
        }
        if (OptCallbacks) {
          for (auto &L : Calls) llvm::outs() << "  " << L << "\n";
          return;
        }
        llvm::outs() << "  result vector has " << Res->size() << " entries, indexed by block ID\n";
        llvm::outs() << "  block  elems  reachable  consumed-in-other-block  state\n";
        for (const CFGBlock *B : llvm::reverse(G)) {
          unsigned ID = B->getBlockID();
          char Buf[96];
          snprintf(Buf, sizeof Buf, "  B%-5u %-6u %-10s %-24s %s", ID, (unsigned)B->size(),
                   ACFG->isBlockReachable(*B) ? "yes" : "NO",
                   ACFG->containsExprConsumedInDifferentBlock(*B) ? "yes" : "no",
                   (*Res)[ID] ? "present" : "std::nullopt");
          llvm::outs() << Buf << "\n";
        }
      });
}
