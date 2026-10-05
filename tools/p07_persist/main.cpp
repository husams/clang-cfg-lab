// p07_persist -- Part 7.7: what is worth persisting? Compact CFG tables versus full dataflow state.
//
//   build/bin/p07_persist FILE [--func=NAME] [--dump-tables] [--roundtrip]
//
// For each function with a std::move it builds three representations and reports their size:
//   tables   one TSV row per block: id, #elements, terminator, successors   (structure only)
//   verdicts one row per function: status + diagnostics                      (the answer)
//   full     every element's Environment, printed by Environment::dump()    (the whole analysis state)
// --roundtrip re-reads the tables with no Clang at all and recomputes loops and reachability.

#include "cfglab.h"
#include "../p07_tu/Driver.h"

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

#include <set>
#include <sstream>

using namespace clang;
using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p07_persist options");
static llvm::cl::opt<std::string> PsFunc("func", llvm::cl::desc("only this function"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> PsDump("dump-tables", llvm::cl::desc("print the TSV tables"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> PsRound("roundtrip", llvm::cl::desc("recompute loops/reachability from the TSV alone"), llvm::cl::cat(Cat));

namespace {
std::string tablesFor(const FunctionDecl *FD, const CFG &G) {
  std::string S;
  llvm::raw_string_ostream OS(S);
  for (const CFGBlock *B : G) {
    OS << FD->getNameAsString() << "\t" << B->getBlockID() << "\t" << B->size() << "\t"
       << (B->getTerminator().isValid() ? cfglab::termKindName(B->getTerminator()) : "-") << "\t";
    bool First = true;
    for (const CFGBlock *Su : B->succs()) {
      if (!Su) continue;
      OS << (First ? "" : ",") << Su->getBlockID();
      First = false;
    }
    if (First) OS << "-";
    OS << "\n";
  }
  return S;
}

// ---- no Clang below this line: only the persisted text -------------------------------------
struct Table {
  std::map<int, std::vector<int>> Succ;
  int Entry = -1, Exit = -1;
};
Table parse(const std::string &Tsv) {
  Table T;
  std::istringstream In(Tsv);
  std::string L;
  std::set<int> HasPred;
  while (std::getline(In, L)) {
    std::vector<std::string> F;
    std::istringstream LS(L);
    for (std::string X; std::getline(LS, X, '\t');) F.push_back(X);
    int Id = std::stoi(F[1]);
    auto &V = T.Succ[Id];
    if (F[4] != "-") {
      std::istringstream SS(F[4]);
      for (std::string X; std::getline(SS, X, ',');) { V.push_back(std::stoi(X)); HasPred.insert(std::stoi(X)); }
    }
  }
  for (auto &[Id, V] : T.Succ) {
    if (!HasPred.count(Id)) T.Entry = Id;
    if (V.empty()) T.Exit = Id;
  }
  return T;
}
unsigned loopsOf(const Table &T) {
  std::map<int, int> Col;
  unsigned Back = 0;
  std::function<void(int)> Dfs = [&](int B) {
    Col[B] = 1;
    for (int S : T.Succ.at(B)) {
      if (Col[S] == 1) ++Back;
      else if (!Col[S]) Dfs(S);
    }
    Col[B] = 2;
  };
  Dfs(T.Entry);
  return Back;
}
bool reaches(const Table &T, int A, int B) {
  std::set<int> Seen;
  std::function<bool(int)> Go = [&](int X) {
    if (X == B) return true;
    if (!Seen.insert(X).second) return false;
    for (int S : T.Succ.at(X)) if (Go(S)) return true;
    return false;
  };
  return Go(A);
}
unsigned liveLoops(const CFG &G) {
  enum { W, Gr, Bl };
  std::vector<int> Col(G.getNumBlockIDs(), W);
  unsigned Back = 0;
  std::function<void(const CFGBlock *)> Dfs = [&](const CFGBlock *B) {
    Col[B->getBlockID()] = Gr;
    for (const CFGBlock *S : B->succs()) {
      if (!S) continue;
      if (Col[S->getBlockID()] == Gr) ++Back;
      else if (Col[S->getBlockID()] == W) Dfs(S);
    }
    Col[B->getBlockID()] = Bl;
  };
  Dfs(&G.getEntry());
  return Back;
}

size_t TotTables = 0, TotVerdict = 0, TotFull = 0;
} // namespace

int main(int argc, const char **argv) {
  int RC = cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!PsFunc.empty() && FD->getNameAsString() != PsFunc) return;
        p07::Options O;
        if (p07::classify(FD, Ctx, O) != p07::Status::Analyzed) return;
        auto ACFG = AdornedCFG::build(*FD);
        if (!ACFG) return;
        const CFG &G = ACFG->getCFG();

        std::string Tables = tablesFor(FD, G);
        if (PsDump) { llvm::outs() << Tables; return; }

        // full state: re-run the analysis and print every element's Environment
        WatchedLiteralsSolver Solver;
        DataflowAnalysisContext DACtx(Solver);
        Environment Env(DACtx, *FD);
        p07::MoveAnalysis A(Ctx, Env);
        p07::MoveDiagnoser Diag;
        llvm::SmallVector<p07::MoveDiag> Ds;
        size_t Full = 0;
        CFGEltCallbacks<p07::MoveAnalysis> CB;
        CB.After = [&](const CFGElement &E, const DataflowAnalysisState<NoopLattice> &St) {
          std::string Tmp;
          llvm::raw_string_ostream OS(Tmp);
          St.Env.dump(OS);
          Full += OS.str().size();
          llvm::move(Diag(E, Ctx, TransferStateForDiagnostics<NoopLattice>(St.Lattice, St.Env)), std::back_inserter(Ds));
        };
        if (!runDataflowAnalysis(*ACFG, A, Env, CB)) return;

        std::string Verdict = FD->getNameAsString() + "\tok\t" + std::to_string(Ds.size()) + "\n";
        for (auto &D : Ds)
          Verdict += "  " + p07::formatDiag(Ctx.getSourceManager(), D) + "\n";

        if (PsRound) {
          Table T = parse(Tables);
          llvm::outs() << FD->getNameAsString() << ": loops from tables=" << loopsOf(T) << " live=" << liveLoops(G)
                       << ", exit reachable from entry (tables)=" << (reaches(T, T.Entry, T.Exit) ? "yes" : "no") << "\n";
          return;
        }
        TotTables += Tables.size(); TotVerdict += Verdict.size(); TotFull += Full;
        llvm::outs() << llvm::format("%-14s blocks=%-3u tables=%-5zu verdicts=%-4zu full-state ~ %zu x tables\n",
                                     FD->getNameAsString().c_str(), G.size(), Tables.size(), Verdict.size(),
                                     Tables.empty() ? 0 : Full / Tables.size());
      });
  if (!PsDump && !PsRound)
    llvm::outs() << "total: tables=" << TotTables << " verdicts=" << TotVerdict << " full-state=" << TotFull << " bytes\n";
  return RC;
}
