// p02_exercises -- three small analyses over a CFG (Part 2.9).
//
//   p02_exercises <file> --mode=cyclomatic|loops|return-in-loop
//                        [--func=NAME] [--eh]
//
//   cyclomatic       M = E - N + 2 over blocks reachable from Entry,
//                    next to the "count the decision points in the AST" answer
//   loops            loop statements (by terminator) vs. back edges (by DFS)
//   return-in-loop   every `return` that sits inside a loop body: CFGStmtMap
//                    gives the return's block and the loop header's block,
//                    ParentMap gives the nesting
//
//   --eh   build with AddEHEdges (handlers become reachable)

#include "cfglab.h"

#include "clang/AST/ParentMap.h"
#include "clang/Analysis/CFGStmtMap.h"

#include "llvm/ADT/DenseSet.h"

#include <set>

using namespace clang;

static llvm::cl::OptionCategory Cat("p02_exercises options");
static llvm::cl::opt<std::string> Mode("mode", llvm::cl::init("cyclomatic"), llvm::cl::cat(Cat));
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::cat(Cat));
static llvm::cl::opt<bool> EH("eh", llvm::cl::cat(Cat), llvm::cl::desc("AddEHEdges"));

// ---- shared helper: the set of blocks reachable from Entry ---------------
static std::set<const CFGBlock *> reachableFromEntry(const CFG &G) {
  std::set<const CFGBlock *> Seen;
  std::vector<const CFGBlock *> Work{&G.getEntry()};
  while (!Work.empty()) {
    const CFGBlock *B = Work.back();
    Work.pop_back();
    if (!Seen.insert(B).second) continue;
    for (const CFGBlock::AdjacentBlock &S : B->succs())
      if (S.isReachable()) Work.push_back(S); // skip pruned edges
  }
  return Seen;
}

// ---- 1. cyclomatic complexity ---------------------------------------------
namespace {
struct Decisions : RecursiveASTVisitor<Decisions> {
  unsigned N = 0;
  bool VisitIfStmt(IfStmt *) { ++N; return true; }
  bool VisitWhileStmt(WhileStmt *) { ++N; return true; }
  bool VisitDoStmt(DoStmt *) { ++N; return true; }
  bool VisitForStmt(ForStmt *) { ++N; return true; }
  bool VisitCXXForRangeStmt(CXXForRangeStmt *) { ++N; return true; }
  bool VisitCaseStmt(CaseStmt *) { ++N; return true; }
  bool VisitCXXCatchStmt(CXXCatchStmt *) { ++N; return true; }
  bool VisitConditionalOperator(ConditionalOperator *) { ++N; return true; }
  bool VisitBinaryOperator(BinaryOperator *B) { N += B->isLogicalOp(); return true; }
};
} // namespace

static void cyclomatic(const FunctionDecl *FD, const CFG &G) {
  auto Reach = reachableFromEntry(G);
  unsigned E = 0;
  for (const CFGBlock *B : Reach)
    for (const CFGBlock::AdjacentBlock &S : B->succs())
      if (S.isReachable()) ++E;
  unsigned N = Reach.size();
  Decisions D;
  D.TraverseStmt(const_cast<Stmt *>(FD->getBody()));
  llvm::outs() << llvm::format("%-12s", FD->getNameAsString().c_str()) << " N=" << llvm::format("%-2u", N)
               << " E=" << llvm::format("%-2u", E) << "  E-N+2 = " << (E - N + 2)
               << "   decisions+1 = " << (D.N + 1) << "\n";
}

// ---- 2. loop counting ------------------------------------------------------
static void loops(const FunctionDecl *FD, const CFG &G) {
  unsigned Syntactic = 0;
  for (const CFGBlock *B : G)
    if (const Stmt *T = B->getTerminatorStmt())
      if (isa<WhileStmt>(T) || isa<ForStmt>(T) || isa<DoStmt>(T) || isa<CXXForRangeStmt>(T))
        ++Syntactic;

  // Back edges by DFS: an edge to a block that is still on the DFS stack.
  llvm::DenseSet<const CFGBlock *> Done, OnStack;
  unsigned Back = 0;
  std::vector<std::pair<const CFGBlock *, unsigned>> Stack;
  Stack.push_back({&G.getEntry(), 0});
  OnStack.insert(&G.getEntry());
  while (!Stack.empty()) {
    auto &[B, I] = Stack.back();
    if (I == B->succ_size()) {
      OnStack.erase(B);
      Done.insert(B);
      Stack.pop_back();
      continue;
    }
    const CFGBlock::AdjacentBlock &S = *(B->succ_begin() + I++);
    if (!S.isReachable()) continue;
    const CFGBlock *To = S;
    if (OnStack.count(To)) ++Back;
    else if (!Done.count(To)) {
      OnStack.insert(To);
      Stack.push_back({To, 0});
    }
  }
  llvm::outs() << llvm::format("%-12s", FD->getNameAsString().c_str())
               << " loop statements=" << Syntactic << "  back edges=" << Back << "\n";
}

// ---- 3. return inside a loop --------------------------------------------
static void returnInLoop(const FunctionDecl *FD, ASTContext &Ctx, const CFG &G) {
  const SourceManager &SM = Ctx.getSourceManager();
  ParentMap PM(FD->getBody());
  CFGStmtMap Map(G, PM);
  struct Finder : RecursiveASTVisitor<Finder> {
    std::vector<const ReturnStmt *> Rets;
    bool VisitReturnStmt(ReturnStmt *R) { Rets.push_back(R); return true; }
  } F;
  F.TraverseStmt(const_cast<Stmt *>(FD->getBody()));

  bool Any = false;
  for (const ReturnStmt *R : F.Rets) {
    // Walk outwards through ParentMap; every loop we pass makes this "inside".
    std::vector<const Stmt *> Loops;
    for (const Stmt *P = PM.getParent(R); P; P = PM.getParent(P))
      if (isa<WhileStmt>(P) || isa<ForStmt>(P) || isa<DoStmt>(P) || isa<CXXForRangeStmt>(P))
        Loops.push_back(P);
    if (Loops.empty()) continue;
    Any = true;
    llvm::outs() << "  return at line " << cfglab::lineOf(SM, R->getBeginLoc()) << " (in "
                 << cfglab::blockName(Map.getBlock(R)) << ") is inside:";
    for (const Stmt *L : Loops)
      llvm::outs() << " " << L->getStmtClassName() << "@line " << cfglab::lineOf(SM, L->getBeginLoc())
                   << " (header " << cfglab::blockName(Map.getBlock(L)) << ")";
    llvm::outs() << "\n";
  }
  if (!Any) llvm::outs() << "  (no return inside a loop)\n";
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        CFG::BuildOptions BO;
        BO.AddEHEdges = EH;
        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) return;
        if (Mode == "cyclomatic") cyclomatic(FD, *G);
        else if (Mode == "loops") loops(FD, *G);
        else if (Mode == "return-in-loop") {
          llvm::outs() << "== " << FD->getNameAsString() << "\n";
          returnInLoop(FD, Ctx, *G);
        } else {
          llvm::errs() << "--mode must be cyclomatic, loops or return-in-loop\n";
          std::exit(2);
        }
      });
}
