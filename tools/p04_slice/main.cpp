// p04_slice -- control dependence, iterated dominance frontier, a toy slicer (Part 4.5).
//
//   p04_slice <file> --func=NAME [--mode=cd|df|slice] [--var=NAME] [--dump]
//
//   cd     (default) ControlDependencyCalculator: for every block, the blocks it
//          is control dependent on, with the branch condition of each
//   df     dominance frontier of every block (computed by hand from CFGDomTree)
//          and the iterated dominance frontier of the blocks that assign --var,
//          computed by llvm::IDFCalculatorBase -- where an SSA phi would go
//   slice  backward slice of --var at the last 'return' of the function: data
//          dependences through assignments, control dependences through
//          ControlDependencyCalculator
// --dump also calls ControlDependencyCalculator::dump() (stderr).

#include "cfglab.h"

#include "clang/Analysis/Analyses/CFGReachabilityAnalysis.h"
#include "clang/Analysis/Analyses/Dominators.h"
#include "clang/Analysis/AnalysisDeclContext.h"

#include <algorithm>
#include <map>
#include <set>

using namespace clang;

static llvm::cl::OptionCategory Cat("p04_slice options");
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat), llvm::cl::desc("function"));
static llvm::cl::opt<std::string> ModeOpt("mode", llvm::cl::init("cd"), llvm::cl::cat(Cat), llvm::cl::desc("cd|df|slice"));
static llvm::cl::opt<std::string> VarOpt("var", llvm::cl::cat(Cat), llvm::cl::desc("variable name"));
static llvm::cl::opt<bool> Dump("dump", llvm::cl::cat(Cat), llvm::cl::desc("ControlDependencyCalculator::dump()"));

// ---- a tiny def/use extractor over statements (the same idea as Part 5's liveness) ----
struct Event { bool IsDef; const VarDecl *V; };

static const VarDecl *asVar(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (auto *R = dyn_cast<DeclRefExpr>(E))
    return dyn_cast<VarDecl>(R->getDecl());
  return nullptr;
}

static void collect(const Stmt *S, std::vector<Event> &Out) {
  if (!S) return;
  if (auto *BO = dyn_cast<BinaryOperator>(S); BO && BO->isAssignmentOp()) {
    const VarDecl *V = asVar(BO->getLHS());
    if (!V) { collect(BO->getLHS(), Out); collect(BO->getRHS(), Out); return; }
    if (BO->isCompoundAssignmentOp()) Out.push_back({false, V});
    collect(BO->getRHS(), Out);
    Out.push_back({true, V});
    return;
  }
  if (auto *UO = dyn_cast<UnaryOperator>(S); UO && UO->isIncrementDecrementOp()) {
    if (const VarDecl *V = asVar(UO->getSubExpr())) { Out.push_back({false, V}); Out.push_back({true, V}); return; }
  }
  if (auto *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls())
      if (auto *VD = dyn_cast<VarDecl>(D); VD && VD->getInit()) {
        collect(VD->getInit(), Out);
        Out.push_back({true, VD});
      }
    return;
  }
  if (auto *R = dyn_cast<DeclRefExpr>(S)) {
    if (auto *V = dyn_cast<VarDecl>(R->getDecl())) Out.push_back({false, V});
    return;
  }
  for (const Stmt *C : S->children()) collect(C, Out);
}

static std::vector<Event> eventsOf(const Stmt *S) { std::vector<Event> E; collect(S, E); return E; }

static const char *termName(const CFGBlock *B) {
  const Stmt *T = B->getTerminatorStmt();
  return T ? T->getStmtClassName() : "-";
}

static std::string idList(const std::vector<const CFGBlock *> &V) {
  std::string S;
  for (const CFGBlock *B : V) S += (S.empty() ? "" : " ") + cfglab::blockName(B);
  return S.empty() ? "-" : S;
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (FuncOpt.empty() || FD->getQualifiedNameAsString() != FuncOpt) return;
        AnalysisDeclContextManager Mgr(Ctx);
        AnalysisDeclContext *AC = Mgr.getContext(FD);
        CFG *G = AC->getCFG();
        if (!G) return;
        SourceManager &SM = Ctx.getSourceManager();
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << ": " << G->size() << " blocks\n";

        auto condText = [&](const CFGBlock *B) {
          const Stmt *C = B->getTerminatorCondition();
          return C ? cfglab::stmtText(C, Ctx) : std::string("-");
        };

        // ControlDependencyCalculator is a ManagedAnalysis with no getTag(): construct it.
        ControlDependencyCalculator CD(G);

        if (ModeOpt == "cd") {
          for (const CFGBlock *B : *G) {
            if (B == &G->getEntry()) continue;
            std::string Name = cfglab::blockName(B);
            Name.resize(4, ' ');
            llvm::outs() << Name << "depends on: ";
            const auto &Deps = CD.getControlDependencies(const_cast<CFGBlock *>(B));
            std::vector<const CFGBlock *> Sorted(Deps.begin(), Deps.end());
            std::sort(Sorted.begin(), Sorted.end(), [](auto *A, auto *C) { return A->getBlockID() < C->getBlockID(); });
            if (Sorted.empty()) llvm::outs() << "-";
            std::string S;
            for (const CFGBlock *D : Sorted)
              S += (S.empty() ? "" : ", ") + cfglab::blockName(D) + " [" + termName(D) + ": " + condText(D) + "]";
            llvm::outs() << S << "\n";
          }
          if (Dump) { llvm::outs().flush(); CD.dump(); }
          return;
        }

        if (ModeOpt == "df") {
          CFGDomTree DT(G);
          // Dominance frontier, straight from the definition:
          //   DF(N) = { Y : N dominates a predecessor of Y, but N does not strictly dominate Y }
          std::map<unsigned, std::set<unsigned>> DF;
          for (const CFGBlock *N : *G) {
            if (!DT.isReachableFromEntry(N)) continue;
            for (const CFGBlock *Y : *G) {
              if (!DT.isReachableFromEntry(Y) || DT.properlyDominates(N, Y)) continue;
              for (const CFGBlock *P : Y->preds())
                if (P && DT.isReachableFromEntry(P) && DT.dominates(N, P)) { DF[N->getBlockID()].insert(Y->getBlockID()); break; }
            }
          }
          for (const CFGBlock *N : *G) {
            std::string S;
            for (unsigned Y : DF[N->getBlockID()]) S += (S.empty() ? "B" : " B") + std::to_string(Y);
            std::string Name = "DF(" + cfglab::blockName(N) + ")";
            Name.resize(8, ' ');
            llvm::outs() << Name << "= {" << S << "}\n";
          }
          if (VarOpt.empty()) return;

          // Blocks that assign the variable.
          llvm::SmallPtrSet<CFGBlock *, 8> Defs;
          for (const CFGBlock *B : *G)
            for (const CFGElement &E : *B)
              if (auto CS = E.getAs<CFGStmt>())
                for (const Event &Ev : eventsOf(CS->getStmt()))
                  if (Ev.IsDef && Ev.V->getName() == VarOpt) Defs.insert(const_cast<CFGBlock *>(B));
          std::vector<const CFGBlock *> DefV(Defs.begin(), Defs.end());
          std::sort(DefV.begin(), DefV.end(), [](auto *A, auto *C) { return A->getBlockID() < C->getBlockID(); });
          llvm::outs() << "blocks assigning '" << VarOpt << "': " << idList(DefV) << "\n";

          llvm::IDFCalculatorBase<CFGBlock, /*IsPostDom=*/false> IDF(DT.getBase());
          IDF.setDefiningBlocks(Defs);
          llvm::SmallVector<CFGBlock *, 8> Phi;
          IDF.calculate(Phi);
          std::vector<const CFGBlock *> PhiV(Phi.begin(), Phi.end());
          std::sort(PhiV.begin(), PhiV.end(), [](auto *A, auto *C) { return A->getBlockID() < C->getBlockID(); });
          llvm::outs() << "IDFCalculatorBase phi blocks: " << idList(PhiV) << "\n";

          // Same thing by hand: close the defining set under DF.
          std::set<unsigned> Work, Result;
          for (const CFGBlock *B : DefV) Work.insert(B->getBlockID());
          while (!Work.empty()) {
            unsigned X = *Work.begin();
            Work.erase(Work.begin());
            for (unsigned Y : DF[X])
              if (Result.insert(Y).second) Work.insert(Y);
          }
          std::string S;
          for (unsigned Y : Result) S += (S.empty() ? "B" : " B") + std::to_string(Y);
          llvm::outs() << "closure of DF by hand:      " << (S.empty() ? "-" : S) << "\n";
          return;
        }

        // ---- slice ----
        const VarDecl *Target = nullptr;
        for (const CFGBlock *B : *G)
          for (const CFGElement &E : *B)
            if (auto CS = E.getAs<CFGStmt>())
              for (const Event &Ev : eventsOf(CS->getStmt()))
                if (Ev.V->getName() == VarOpt) Target = Ev.V;
        if (!Target) { llvm::outs() << "no variable '" << VarOpt << "'\n"; return; }

        const CFGBlock *CritB = nullptr;
        const Stmt *CritS = nullptr;
        for (const CFGBlock *B : *G)
          for (const CFGElement &E : *B)
            if (auto CS = E.getAs<CFGStmt>())
              if (isa<ReturnStmt>(CS->getStmt())) { CritB = B; CritS = CS->getStmt(); }
        if (!CritB) { llvm::outs() << "no return statement\n"; return; }
        llvm::outs() << "criterion: '" << VarOpt << "' at line " << cfglab::lineOf(SM, CritS->getBeginLoc())
                     << " (" << cfglab::stmtText(CritS, Ctx) << ") in " << cfglab::blockName(CritB) << "\n";

        auto pos = [&](const Stmt *S) {
          return std::make_pair(cfglab::lineOf(SM, S->getBeginLoc()), SM.getSpellingColumnNumber(S->getBeginLoc()));
        };
        CFGReverseBlockReachabilityAnalysis *RA = AC->getCFGReachablityAnalysis();
        std::set<const VarDecl *> Relevant = {Target};
        std::set<const CFGBlock *> InSlice = {CritB};
        std::map<std::pair<unsigned, unsigned>, std::string> Lines;   // (line, column) -> text of kept statements
        Lines[pos(CritS)] = cfglab::stmtText(CritS, Ctx);

        bool Changed = true;
        unsigned Rounds = 0;
        while (Changed) {
          Changed = false;
          ++Rounds;
          // data dependence: any statement that can reach the criterion and defines a relevant variable
          for (const CFGBlock *B : *G) {
            if (B != CritB && !RA->isReachable(B, CritB)) continue;
            for (const CFGElement &E : *B) {
              auto CS = E.getAs<CFGStmt>();
              if (!CS || CS->getStmt() == CritS) continue;
              std::vector<Event> Ev = eventsOf(CS->getStmt());
              bool Defines = false;
              for (const Event &X : Ev) Defines |= X.IsDef && Relevant.count(X.V);
              if (!Defines) continue;
              Lines[pos(CS->getStmt())] = cfglab::stmtText(CS->getStmt(), Ctx);
              if (InSlice.insert(B).second) Changed = true;
              for (const Event &X : Ev) Changed |= Relevant.insert(X.V).second;
            }
          }
          // control dependence: the branches that decide whether the sliced blocks run
          for (const CFGBlock *B : std::vector<const CFGBlock *>(InSlice.begin(), InSlice.end()))
            for (CFGBlock *D : CD.getControlDependencies(const_cast<CFGBlock *>(B))) {
              const Stmt *C = D->getTerminatorCondition();
              if (!C) continue;
              if (InSlice.insert(D).second) Changed = true;
              Lines[pos(C)] = "(" + std::string(termName(D)) + ") " + cfglab::stmtText(C, Ctx);
              for (const Event &X : eventsOf(C)) Changed |= Relevant.insert(X.V).second;
            }
        }
        std::string Vars;
        for (const VarDecl *V : Relevant) Vars += (Vars.empty() ? "" : ", ") + V->getNameAsString();
        std::vector<std::string> VS;
        for (const VarDecl *V : Relevant) VS.push_back(V->getNameAsString());
        std::sort(VS.begin(), VS.end());
        Vars.clear();
        for (auto &X : VS) Vars += (Vars.empty() ? "" : ", ") + X;
        llvm::outs() << "fixpoint after " << Rounds << " rounds; relevant variables: {" << Vars << "}\n";
        llvm::outs() << "slice (" << Lines.size() << " statements):\n";
        for (auto &[L, T] : Lines) llvm::outs() << "  line " << L.first << ": " << T << "\n";
      });
}
