// p05_deadstores -- a dead-store finder built on LiveVariables::Observer (Part 5.2).
//
//   p05_deadstores <file> [--func=NAME] [--relaxed] [--trace]
//
// LiveVariables::runOnAllBlocks(Observer&) walks every block BACKWARDS and
// calls observeStmt(S, Block, V) right before it applies the transfer function
// for S. Walking backwards, "before applying S's transfer function" means V is
// the liveness AFTER S in program order. A store to x whose V does not contain
// x is dead.

#include "cfglab.h"

#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/Analyses/LiveVariables.h"

#include <set>

using namespace clang;

static llvm::cl::OptionCategory Cat("p05_deadstores options");
static llvm::cl::opt<std::string> FuncFlag("func", llvm::cl::desc("only this function"),
                                           llvm::cl::cat(Cat));
static llvm::cl::opt<bool> RelaxedFlag("relaxed", llvm::cl::desc("use RelaxedLiveVariables (killAtAssign=false)"),
                                       llvm::cl::cat(Cat));
static llvm::cl::opt<bool> TraceFlag("trace", llvm::cl::desc("print every observeStmt call"),
                                     llvm::cl::cat(Cat));

namespace {

struct AddrTaken : RecursiveASTVisitor<AddrTaken> {
  std::set<const VarDecl *> Vars;
  bool VisitUnaryOperator(UnaryOperator *U) {
    if (U->getOpcode() == UO_AddrOf)
      if (auto *DR = dyn_cast<DeclRefExpr>(U->getSubExpr()->IgnoreParens()))
        if (auto *VD = dyn_cast<VarDecl>(DR->getDecl())) Vars.insert(VD);
    return true;
  }
};

struct VarList : RecursiveASTVisitor<VarList> {
  std::vector<const VarDecl *> Vars;
  bool VisitVarDecl(VarDecl *VD) { if (VD->hasLocalStorage()) Vars.push_back(VD); return true; }
};

class DeadStoreObserver : public LiveVariables::Observer {
public:
  DeadStoreObserver(ASTContext &C, const std::set<const VarDecl *> &Escaped,
                    const std::vector<const VarDecl *> &All)
      : Ctx(C), Escaped(Escaped), All(All) {}

  void observeStmt(const Stmt *S, const CFGBlock *B, const LiveVariables::LivenessValues &V) override {
    const SourceManager &SM = Ctx.getSourceManager();
    if (TraceFlag) {
      llvm::outs() << "  observe " << llvm::format("%-4s", cfglab::blockName(B).c_str())
                   << llvm::format("%-18s", S->getStmtClassName()) << " live-after={";
      bool First = true;
      for (const VarDecl *VD : All)
        if (V.isLive(VD)) { llvm::outs() << (First ? "" : " ") << VD->getName(); First = false; }
      llvm::outs() << "}  line " << cfglab::lineOf(SM, S->getBeginLoc()) << "\n";
    }
    if (auto *BO = dyn_cast<BinaryOperator>(S)) {
      if (BO->getOpcode() != BO_Assign) return;
      auto *DR = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParens());
      if (!DR) return;
      if (auto *VD = dyn_cast<VarDecl>(DR->getDecl()))
        check(VD, "store", BO->getBeginLoc(), V);
    } else if (auto *DS = dyn_cast<DeclStmt>(S)) {
      for (const Decl *D : DS->decls())
        if (auto *VD = dyn_cast<VarDecl>(D))
          if (VD->hasInit()) check(VD, "initialization", VD->getLocation(), V);
    }
  }

private:
  void check(const VarDecl *VD, const char *What, SourceLocation L, const LiveVariables::LivenessValues &V) {
    if (!VD->hasLocalStorage() || VD->getType()->isReferenceType()) return;
    if (Escaped.count(VD)) return; // address taken: somebody else may read it
    if (V.isLive(VD)) return;
    Found.insert({Ctx.getSourceManager().getSpellingLineNumber(L), std::string("dead ") + What + " to '" + VD->getName().str() + "'"});
  }

public:
  std::set<std::pair<unsigned, std::string>> Found; // sorted by line
private:
  ASTContext &Ctx;
  const std::set<const VarDecl *> &Escaped;
  const std::vector<const VarDecl *> &All;
};

} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!FuncFlag.empty() && FD->getQualifiedNameAsString() != FuncFlag) return;
        AnalysisDeclContext AC(nullptr, FD);
        cfglab::applyPreset(AC.getCFGBuildOptions(), cfglab::semaPreset());
        if (!AC.getCFG()) return;

        AddrTaken AT;
        AT.TraverseStmt(const_cast<Stmt *>(FD->getBody()));
        VarList VL;
        VL.TraverseDecl(const_cast<FunctionDecl *>(FD));

        std::unique_ptr<LiveVariables> LV = RelaxedFlag ? RelaxedLiveVariables::create(AC)
                                                        : LiveVariables::create(AC);
        DeadStoreObserver Obs(Ctx, AT.Vars, VL.Vars);
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        LV->runOnAllBlocks(Obs);
        if (Obs.Found.empty()) llvm::outs() << "  no dead stores\n";
        for (auto &[Line, Msg] : Obs.Found) llvm::outs() << "  line " << Line << ": " << Msg << "\n";
      });
}
