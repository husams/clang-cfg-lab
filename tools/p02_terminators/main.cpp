// p02_terminators -- terminators, conditions, labels, loop targets (Part 2.6).
//
//   p02_terminators <file> [--preset=..] [--set=..] [--clear=..] [--func=NAME]
//
// One paragraph per block that has anything interesting to say:
//
//   B3  StmtBranch IfStmt         line 7
//       T:     if (a && b && c)         <- CFGBlock::printTerminator
//       cond:  ...                      <- getTerminatorCondition()
//       last:  ...                      <- getLastCondition()
//       succ0: B5 ...

#include "cfglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p02_terminators options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat)

// Does the block's first element come from inside Sub (a source range test)?
static bool startsInside(const CFGBlock *B, const Stmt *Sub, const SourceManager &SM) {
  if (!B || !Sub || B->empty()) return false;
  auto S = B->front().getAs<CFGStmt>();
  if (!S) return false;
  SourceLocation L = S->getStmt()->getBeginLoc();
  return L.isValid() && !SM.isBeforeInTranslationUnit(L, Sub->getBeginLoc()) &&
         !SM.isBeforeInTranslationUnit(Sub->getEndLoc(), L);
}

static const char *roleOf(const CFGTerminator &Term, unsigned Idx, const CFGBlock *Succ,
                          const SourceManager &SM) {
  // The two non-statement terminator kinds have fixed meanings.
  if (Term.isVirtualBaseBranch())
    return Idx == 0 ? "vbases already initialised: skip" : "not yet: run the base initialisers";
  if (Term.isTemporaryDtorsBranch()) {
    bool RunsDtor = Succ && !Succ->empty() && Succ->front().getAs<CFGTemporaryDtor>();
    return RunsDtor ? "run ~T()" : "skip ~T()";
  }
  const Stmt *T = Term.getStmt();
  if (!T) return "";
  if (isa<IfStmt>(T)) return Idx == 0 ? "then" : "else";
  if (isa<ConditionalOperator>(T) || isa<BinaryConditionalOperator>(T))
    return Idx == 0 ? "lhs" : "rhs";
  if (isa<WhileStmt>(T) || isa<ForStmt>(T) || isa<CXXForRangeStmt>(T) || isa<DoStmt>(T))
    return Idx == 0 ? "body" : "exit";
  if (isa<SwitchStmt>(T)) return "case/default";
  if (auto *BO = dyn_cast<BinaryOperator>(T))
    return startsInside(Succ, BO->getRHS(), SM) ? "evaluates RHS" : "skips RHS";
  if (isa<CXXTryStmt>(T)) return Idx == 0 ? "handler" : "(other)";
  return "";
}

static std::string labelText(const Stmt *L, ASTContext &Ctx) {
  std::string S = L->getStmtClassName();
  if (auto *LS = dyn_cast<LabelStmt>(L)) S += " " + std::string(LS->getName());
  else if (auto *CS = dyn_cast<CaseStmt>(L)) S += " " + cfglab::stmtText(CS->getLHS(), Ctx);
  else if (auto *C = dyn_cast<CXXCatchStmt>(L))
    S += C->getExceptionDecl() ? " (" + C->getExceptionDecl()->getType().getAsString() + ")" : " (...)";
  return S;
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!flagWantsFunction(FD)) return;
        const SourceManager &SM = Ctx.getSourceManager();
        CFG::BuildOptions BO;
        if (!flagsToOptions(BO)) std::exit(2);
        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";

        for (const CFGBlock *B : *G) {
          const CFGTerminator Term = B->getTerminator();
          const Stmt *T = B->getTerminatorStmt();
          // Note: Term.isValid() can be true while getTerminatorStmt() is null
          // (the VirtualBaseBranch kind has no statement).
          bool Interesting = Term.isValid() || B->getLabel() || B->getLoopTarget() ||
                             B->hasNoReturnElement() || B->isInevitablySinking();
          if (!Interesting) continue;

          llvm::outs() << cfglab::blockName(B);
          if (Term.isValid()) {
            llvm::outs() << "  " << cfglab::termKindName(Term) << " "
                         << (T ? T->getStmtClassName() : "(no statement)");
            if (T) llvm::outs() << "  line " << cfglab::lineOf(SM, T->getBeginLoc());
          }
          llvm::outs() << "\n";

          if (Term.isValid()) {
            // printTerminator() can span several lines (a range-for prints its
            // body); squash to one line.
            std::string Text;
            llvm::raw_string_ostream TS(Text);
            B->printTerminator(TS, Ctx.getLangOpts());
            for (char &Ch : TS.str()) if (Ch == '\n') Ch = ' ';
            llvm::outs() << "    T:      " << TS.str() << "\n";
            if (const Stmt *C = T ? B->getTerminatorCondition(/*StripParens=*/true) : nullptr)
              llvm::outs() << "    cond:   " << C->getStmtClassName() << "  `"
                           << cfglab::stmtText(C, Ctx) << "`\n";
            if (const Expr *C = T ? B->getLastCondition() : nullptr)
              llvm::outs() << "    last:   " << C->getStmtClassName() << "  `"
                           << cfglab::stmtText(C, Ctx) << "`\n";
            unsigned I = 0;
            for (const CFGBlock::AdjacentBlock &S : B->succs()) {
              llvm::outs() << "    succ" << I << ":  ";
              if (S.isReachable())
                llvm::outs() << cfglab::blockName(S) << "  " << roleOf(Term, I, S, SM) << "\n";
              else
                llvm::outs() << "(pruned)\n";
              ++I;
            }
          }
          if (const Stmt *L = B->getLabel())
            llvm::outs() << "    label:  " << labelText(L, Ctx) << "\n";
          if (const Stmt *L = B->getLoopTarget())
            llvm::outs() << "    loopTarget: " << L->getStmtClassName() << " (line "
                         << cfglab::lineOf(SM, L->getBeginLoc()) << ")\n";
          if (B->hasNoReturnElement())
            llvm::outs() << "    noreturn element: yes (single successor: "
                         << cfglab::blockName(*B->succ_begin()) << ")\n";
          else if (B->isInevitablySinking())
            llvm::outs() << "    inevitably sinking: yes\n";
        }
      });
}
