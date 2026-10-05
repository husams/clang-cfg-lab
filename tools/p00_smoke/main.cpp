// p00_smoke -- toolchain self-test for the whole lab.
//
//   build/bin/p00_smoke manifests/p01_hello.cpp
//
// For each function it touches every library family that Parts 3-7 use, once,
// and prints one line. If this tool builds and runs, the infrastructure
// (include paths, linking against libclang-cpp, the platform flags) is good
// for every later part.
//
//   CFG + BuildOptions ............. Part 2
//   AnalysisDeclContext ............ Part 4
//   PostOrderCFGView, dominators,
//   reachability, WTO .............. Part 4
//   LiveVariables, UninitializedValues,
//   ReachableCode .................. Part 5
//   AdornedCFG + DataflowAnalysis,
//   WatchedLiteralsSolver .......... Part 6

#include "cfglab.h"

#include "clang/Analysis/Analyses/CFGReachabilityAnalysis.h"
#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/Analyses/IntervalPartition.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/Analyses/ReachableCode.h"
#include "clang/Analysis/Analyses/UninitializedValues.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/NoopAnalysis.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p00_smoke options");

namespace {
struct CountUninit : UninitVariablesHandler {
  unsigned N = 0;
  void handleUseOfUninitVariable(const VarDecl *, const UninitUse &) override { ++N; }
};
struct CountUnreachable : reachable_code::Callback {
  unsigned N = 0;
  void HandleUnreachable(reachable_code::UnreachableKind, SourceLocation, SourceRange,
                         SourceRange, SourceRange, bool) override {
    ++N;
  }
};
} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &PP) {
        llvm::outs() << llvm::format("%-12s", FD->getNameAsString().c_str());

        // --- Part 2: CFG + options ------------------------------------------
        AnalysisDeclContextManager Mgr(Ctx);
        AnalysisDeclContext *AC = Mgr.getContext(FD);
        cfglab::applyPreset(AC->getCFGBuildOptions(), cfglab::semaPreset()); // before the first getCFG()
        CFG *G = AC->getCFG();
        if (!G) { llvm::outs() << "no CFG\n"; return; }
        llvm::outs() << " blocks=" << G->size();

        // --- Part 4: orders, dominators, reachability, WTO ---------------------
        unsigned RPO = 0;
        for (const CFGBlock *B : *AC->getAnalysis<PostOrderCFGView>()) { (void)B; ++RPO; }
        CFGDomTree DT(G);
        CFGPostDomTree PDT(G);
        bool DomOK = DT.dominates(&G->getEntry(), &G->getExit()) ||
                     !DT.isReachableFromEntry(&G->getExit());
        (void)PDT;
        CFGReverseBlockReachabilityAnalysis RA(*G);
        bool Reach = RA.isReachable(&G->getEntry(), &G->getExit());
        bool Wto = getIntervalWTO(*G).has_value();
        llvm::outs() << " rpo=" << RPO << " dom=" << DomOK << " reach=" << Reach
                     << " wto=" << (Wto ? "ok" : "irreducible");

        // --- Part 5: classic analyses -----------------------------------------
        LiveVariables *LV = AC->getAnalysis<LiveVariables>();
        llvm::outs() << " live=" << (LV ? "ok" : "null");
        CountUninit UH;
        UninitVariablesAnalysisStats Stats{};
        runUninitializedVariablesAnalysis(*FD, *G, *AC, UH, Stats);
        CountUnreachable UC;
        reachable_code::FindUnreachableCode(*AC, PP, UC);
        llvm::outs() << " uninit=" << UH.N << " unreachable=" << UC.N;

        // --- Part 6: FlowSensitive ----------------------------------------------
        if (FD->isTemplated()) { llvm::outs() << " dataflow=skipped(template)\n"; return; }
        auto ACFG = dataflow::AdornedCFG::build(*FD);
        if (!ACFG) {
          llvm::outs() << " dataflow=" << llvm::toString(ACFG.takeError()) << "\n";
          return;
        }
        dataflow::DataflowAnalysisContext DACtx(std::make_unique<dataflow::WatchedLiteralsSolver>());
        dataflow::Environment Env(DACtx, *FD);
        dataflow::NoopAnalysis A(Ctx);
        auto Res = dataflow::runDataflowAnalysis(*ACFG, A, Env);
        if (!Res) {
          llvm::outs() << " dataflow=error\n";
          llvm::consumeError(Res.takeError());
          return;
        }
        unsigned Evaluated = 0;
        for (const auto &S : *Res) Evaluated += S.has_value();
        llvm::outs() << " dataflow=" << Evaluated << "/" << Res->size() << " blocks\n";
      });
}
