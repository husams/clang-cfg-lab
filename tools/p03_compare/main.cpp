// p03_compare -- the same function under the Sema, AdornedCFG and analyzer option sets (Part 3.8).
//
//   p03_compare <file> --func=NAME            element counts per kind, one column per preset
//   p03_compare <file> --func=NAME --matrix   BuildOptions fields per preset (function independent)
//   p03_compare <file> --func=NAME --verify   rebuild the CFG the way Sema's AnalysisDeclContext path
//                                             and AdornedCFG::build do, and compare with our presets

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/FlowSensitive/AdornedCFG.h"

#include <map>

using namespace clang;

static llvm::cl::OptionCategory Cat("p03_compare options");
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat), llvm::cl::desc("function (qualified name)"));
static llvm::cl::opt<bool> Matrix("matrix", llvm::cl::cat(Cat), llvm::cl::desc("print the BuildOptions matrix"));
static llvm::cl::opt<bool> Verify("verify", llvm::cl::cat(Cat), llvm::cl::desc("check the presets against the real builders"));

static const char *Presets[] = {"default", "sema", "adorned", "analyzer", "kitchen"};

static std::string printed(const CFG &G, ASTContext &Ctx) {
  std::string S;
  llvm::raw_string_ostream OS(S);
  G.print(OS, Ctx.getLangOpts(), false);
  return OS.str();
}

static void sameOrDiff(const char *Label, const std::string &A, const std::string &B) {
  llvm::outs() << llvm::format("%-50s", Label) << (A == B ? "identical" : "DIFFERENT") << "\n";
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncOpt.empty() && FD->getQualifiedNameAsString() != FuncOpt) return;
        if (Matrix) {
          llvm::outs() << llvm::format("%-42s", "field");
          for (const char *P : Presets) llvm::outs() << llvm::format("%-10s", P);
          llvm::outs() << "\n";
          for (const cfglab::OptionField &F : cfglab::optionFields()) {
            llvm::outs() << llvm::format("%-42s", F.Name);
            for (const char *P : Presets) {
              CFG::BuildOptions BO;
              cfglab::presetByName(P, BO);
              llvm::outs() << llvm::format("%-10s", BO.*(F.Ptr) ? "x" : ".");
            }
            llvm::outs() << "\n";
          }
          std::exit(0);
        }

        if (Verify) {
          llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
          // 1. AdornedCFG::build against adornedPreset()
          auto A = dataflow::AdornedCFG::build(*FD);
          CFG::BuildOptions AB = cfglab::adornedPreset();
          std::unique_ptr<CFG> GA = CFG::buildCFG(FD, FD->getBody(), &Ctx, AB);
          if (!A) { llvm::outs() << "AdornedCFG::build failed\n"; llvm::consumeError(A.takeError()); return; }
          sameOrDiff("AdornedCFG::build  vs adornedPreset()", printed(A->getCFG(), Ctx), printed(*GA, Ctx));
          // 2. a Sema-style AnalysisDeclContext (no manager) vs semaPreset()
          AnalysisDeclContext AC(/*Mgr=*/nullptr, FD);
          CFG::BuildOptions &O = AC.getCFGBuildOptions();
          O.PruneTriviallyFalseEdges = true;
          O.AddEHEdges = false;
          O.AddInitializers = true;
          O.AddImplicitDtors = true;
          O.AddTemporaryDtors = true;
          O.AddCXXNewAllocator = false;
          O.AddCXXDefaultInitExprInCtors = true;
          O.setAllAlwaysAdd();
          CFG *GS = AC.getCFG();
          CFG::BuildOptions SB = cfglab::semaPreset();
          std::unique_ptr<CFG> GS2 = CFG::buildCFG(FD, FD->getBody(), &Ctx, SB);
          sameOrDiff("AnalysisDeclContext(Sema fields) vs semaPreset()", printed(*GS, Ctx), printed(*GS2, Ctx));
          // 3. AdornedCFG differs from sema by exactly AddLifetime
          CFG::BuildOptions SL = cfglab::semaPreset();
          SL.AddLifetime = true;
          std::unique_ptr<CFG> GL = CFG::buildCFG(FD, FD->getBody(), &Ctx, SL);
          sameOrDiff("adornedPreset() vs semaPreset()+AddLifetime", printed(*GA, Ctx), printed(*GL, Ctx));
          sameOrDiff("adornedPreset() vs semaPreset()", printed(*GA, Ctx), printed(*GS2, Ctx));
          return;
        }

        // Default: counts per element kind, one column per preset.
        std::map<std::string, std::map<std::string, unsigned>> Count; // kind -> preset -> n
        std::map<std::string, unsigned> Blocks, Elems;
        for (const char *P : Presets) {
          CFG::BuildOptions BO;
          cfglab::presetByName(P, BO);
          std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
          Blocks[P] = G->size();
          for (const CFGBlock *B : *G)
            for (const CFGElement &E : *B) {
              ++Count[cfglab::kindName(E.getKind())][P];
              ++Elems[P];
            }
        }
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        llvm::outs() << llvm::format("%-22s", "");
        for (const char *P : Presets) llvm::outs() << llvm::format("%-10s", P);
        llvm::outs() << "\n" << llvm::format("%-22s", "blocks");
        for (const char *P : Presets) llvm::outs() << llvm::format("%-10u", Blocks[P]);
        llvm::outs() << "\n";
        for (auto &[K, Row] : Count) {
          if (K == "Statement") continue; // Statement counts only measure always-add, not C++ semantics
          llvm::outs() << llvm::format("%-22s", K.c_str());
          for (const char *P : Presets) llvm::outs() << llvm::format("%-10u", Row.count(P) ? Row[P] : 0u);
          llvm::outs() << "\n";
        }
        llvm::outs() << llvm::format("%-22s", "Statement");
        for (const char *P : Presets) llvm::outs() << llvm::format("%-10u", Count["Statement"][P]);
        llvm::outs() << "\n";
      });
}
