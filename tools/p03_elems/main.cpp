// p03_elems -- the C++-specific CFG elements, with every accessor (Part 3.1-3.6).
//
//   p03_elems <file> [--preset=..] [--set=..] [--clear=..] [--always-add=..]
//                    [--func=NAME] [--kinds=a,b,..] [--blocks] [--count]
//
//   --kinds   which element kinds to print. Group names: dtors (all six
//             destructor kinds), scopes (ScopeBegin ScopeEnd LifetimeEnds
//             LoopExit CleanupFunction), alloc (NewAllocator DeleteDtor),
//             ctors (Constructor CXXRecordTypedCall Initializer); or any
//             CFGElement::Kind name. Default: every kind except Statement.
//   --blocks  also print one header line per block: terminator, label, edges
//   --count   print per-kind totals instead of elements
//
// Output line:  B<block>.<index> <Kind> <kind-specific accessors>

#include "cfglab.h"

#include <map>
#include <set>

using namespace clang;

static llvm::cl::OptionCategory Cat("p03_elems options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat)
static llvm::cl::list<std::string> KindsFlag("kinds", llvm::cl::CommaSeparated, llvm::cl::cat(Cat),
                                             llvm::cl::desc("element kinds or groups to print"));
static llvm::cl::opt<bool> BlocksFlag("blocks", llvm::cl::cat(Cat),
                                      llvm::cl::desc("print a header line per block"));
static llvm::cl::opt<bool> CountFlag("count", llvm::cl::cat(Cat),
                                     llvm::cl::desc("print per-kind totals"));

using K = CFGElement::Kind;

static std::set<std::string> wantedKinds() {
  static const std::map<std::string, std::vector<std::string>> Groups = {
      {"dtors", {"AutomaticObjectDtor", "TemporaryDtor", "BaseDtor", "MemberDtor", "DeleteDtor"}},
      {"scopes", {"ScopeBegin", "ScopeEnd", "LifetimeEnds", "LoopExit", "CleanupFunction"}},
      {"alloc", {"NewAllocator", "DeleteDtor"}},
      {"ctors", {"Constructor", "CXXRecordTypedCall", "Initializer"}},
  };
  std::set<std::string> W;
  for (const std::string &S : KindsFlag) {
    auto It = Groups.find(S);
    if (It != Groups.end()) W.insert(It->second.begin(), It->second.end());
    else W.insert(S);
  }
  if (W.empty())
    for (K Kd : {K::Initializer, K::ScopeBegin, K::ScopeEnd, K::NewAllocator, K::LifetimeEnds,
                 K::LoopExit, K::Constructor, K::CXXRecordTypedCall, K::AutomaticObjectDtor,
                 K::DeleteDtor, K::BaseDtor, K::MemberDtor, K::TemporaryDtor, K::CleanupFunction})
      W.insert(cfglab::kindName(Kd));
  return W;
}

static unsigned endLine(const Stmt *S, const SourceManager &SM) {
  return S ? cfglab::lineOf(SM, S->getEndLoc()) : 0;
}

static std::string trigger(const Stmt *S, const SourceManager &SM) {
  if (!S) return "trigger=none";
  return std::string("trigger=") + S->getStmtClassName() + "@L" +
         std::to_string(endLine(S, SM));
}

static std::string describe(const CFGElement &E, ASTContext &Ctx) {
  const SourceManager &SM = Ctx.getSourceManager();
  std::string Out;
  llvm::raw_string_ostream OS(Out);
  if (auto D = E.getAs<CFGImplicitDtor>()) {
    // CFGImplicitDtor::isNoReturn() is declared in CFG.h but has no definition in
    // libclang-cpp 22.1.8 (link error), so ask the destructor declaration instead.
    const CXXDestructorDecl *DD = D->getDestructorDecl(Ctx);
    std::string Name = DD ? DD->getParent()->getNameAsString() : std::string("?");
    OS << "dtor=" << (DD ? "~" + Name : std::string("none")) << " noreturn=" << (DD && DD->isNoReturn())
       << "  ";
  }
  switch (E.getKind()) {
  case K::AutomaticObjectDtor: {
    auto A = E.castAs<CFGAutomaticObjDtor>();
    OS << "var=" << A.getVarDecl()->getName() << " type=" << A.getVarDecl()->getType().getAsString()
       << " " << trigger(A.getTriggerStmt(), SM);
    break;
  }
  case K::TemporaryDtor:
    OS << "bte=`" << cfglab::stmtText(E.castAs<CFGTemporaryDtor>().getBindTemporaryExpr(), Ctx) << "`";
    break;
  case K::BaseDtor: {
    const CXXBaseSpecifier *B = E.castAs<CFGBaseDtor>().getBaseSpecifier();
    OS << "base=" << B->getType().getAsString() << (B->isVirtual() ? " (virtual)" : "");
    // getDestructorDecl() returned null for BaseDtor in 22.1.8; go through the class.
    if (const CXXRecordDecl *RD = B->getType()->getAsCXXRecordDecl())
      if (const CXXDestructorDecl *BD = RD->getDestructor()) OS << " via-record=~" << RD->getName() << (BD->isVirtual() ? " (virtual)" : "");
    break;
  }
  case K::MemberDtor: {
    const FieldDecl *F = E.castAs<CFGMemberDtor>().getFieldDecl();
    OS << "field=" << F->getName() << " type=" << F->getType().getAsString();
    break;
  }
  case K::DeleteDtor: {
    auto D = E.castAs<CFGDeleteDtor>();
    OS << "class=" << D.getCXXRecordDecl()->getName() << " delete=`"
       << cfglab::stmtText(D.getDeleteExpr(), Ctx) << "`";
    break;
  }
  case K::ScopeBegin:
    OS << "var=" << E.castAs<CFGScopeBegin>().getVarDecl()->getName() << " "
       << trigger(E.castAs<CFGScopeBegin>().getTriggerStmt(), SM);
    break;
  case K::ScopeEnd:
    OS << "var=" << E.castAs<CFGScopeEnd>().getVarDecl()->getName() << " "
       << trigger(E.castAs<CFGScopeEnd>().getTriggerStmt(), SM);
    break;
  case K::LifetimeEnds:
    OS << "var=" << E.castAs<CFGLifetimeEnds>().getVarDecl()->getName() << " "
       << trigger(E.castAs<CFGLifetimeEnds>().getTriggerStmt(), SM);
    break;
  case K::LoopExit: {
    const Stmt *L = E.castAs<CFGLoopExit>().getLoopStmt();
    OS << "loop=" << L->getStmtClassName() << "@L" << cfglab::lineOf(SM, L->getBeginLoc());
    break;
  }
  case K::CleanupFunction: {
    auto C = E.castAs<CFGCleanupFunction>();
    OS << "var=" << C.getVarDecl()->getName() << " fn=" << C.getFunctionDecl()->getName();
    break;
  }
  case K::NewAllocator: {
    const CXXNewExpr *N = E.castAs<CFGNewAllocator>().getAllocatorExpr();
    OS << "new=`" << cfglab::stmtText(N, Ctx) << "` array=" << N->isArray()
       << " placement=" << N->getNumPlacementArgs();
    break;
  }
  case K::Initializer: {
    const CXXCtorInitializer *I = E.castAs<CFGInitializer>().getInitializer();
    if (I->isBaseInitializer())
      OS << "base " << I->getTypeSourceInfo()->getType().getAsString()
         << (I->isBaseVirtual() ? " (virtual)" : "");
    else if (I->isAnyMemberInitializer())
      OS << "member " << I->getAnyMember()->getName();
    else
      OS << "delegating";
    OS << "  written=" << I->isWritten() << " in-class=" << I->isInClassMemberInitializer()
       << " init=" << I->getInit()->getStmtClassName();
    break;
  }
  case K::Constructor:
  case K::CXXRecordTypedCall:
  case K::Statement: {
    const Stmt *S = E.castAs<CFGStmt>().getStmt();
    if (auto *CE = dyn_cast<CXXConstructExpr>(S)) {
      // stmtText() prints nothing for a default construction; name the type instead.
      OS << llvm::format("%-18s", S->getStmtClassName()) << " `" << CE->getType().getAsString()
         << "(" << CE->getNumArgs() << " args)`";
      break;
    }
    OS << llvm::format("%-18s", S->getStmtClassName()) << " `" << cfglab::stmtText(S, Ctx) << "`";
    break;
  }
  }
  return OS.str();
}

static void blockHeader(const CFGBlock *B, const CFG &G, ASTContext &Ctx) {
  llvm::outs() << cfglab::blockName(B);
  if (B == &G.getEntry()) llvm::outs() << " ENTRY";
  if (B == &G.getExit()) llvm::outs() << " EXIT";
  CFGTerminator T = B->getTerminator();
  if (T.isValid()) {
    llvm::outs() << " term=" << cfglab::termKindName(T);
    if (const Stmt *S = B->getTerminatorStmt()) llvm::outs() << ":" << S->getStmtClassName();
  }
  if (const Stmt *L = B->getLabel()) llvm::outs() << " label=" << L->getStmtClassName();
  if (B->hasNoReturnElement()) llvm::outs() << " noreturn";
  llvm::outs() << " ->";
  for (const CFGBlock::AdjacentBlock &S : B->succs()) {
    if (S.isReachable()) llvm::outs() << " " << cfglab::blockName(S);
    else llvm::outs() << " (pruned)";
  }
  llvm::outs() << "\n";
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!flagWantsFunction(FD)) return;
        CFG::BuildOptions BO;
        if (!flagsToOptions(BO)) std::exit(2);
        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) return;
        std::set<std::string> Want = wantedKinds();
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << " @L"
                     << cfglab::lineOf(Ctx.getSourceManager(), FD->getLocation()) << ": " << G->size()
                     << " blocks\n";
        std::map<std::string, unsigned> Totals;
        for (const CFGBlock *B : *G) {
          for (const CFGElement &E : *B) ++Totals[cfglab::kindName(E.getKind())];
          if (CountFlag) continue;
          if (BlocksFlag) blockHeader(B, *G, Ctx);
          unsigned I = 0;
          for (const CFGElement &E : *B) {
            ++I;
            const char *Kn = cfglab::kindName(E.getKind());
            if (!Want.count(Kn)) continue;
            llvm::outs() << (BlocksFlag ? "  " : "") << cfglab::blockName(B) << "." << I << " "
                         << llvm::format("%-20s", Kn) << " " << describe(E, Ctx) << "\n";
          }
        }
        if (CountFlag)
          for (const auto &[Kn, N] : Totals)
            if (Want.count(Kn)) llvm::outs() << "  " << llvm::format("%-20s", Kn.c_str()) << " " << N << "\n";
      });
}
