// p07_plugin -- Part 7.6: the checker as a Clang plugin, loaded into the real compiler.
//
//   clang++ -fsyntax-only -Xclang -load -Xclang build/lib/p07_plugin.dylib \
//           -Xclang -add-plugin -Xclang p07-move  file.cpp
//
// Diagnostics are real compiler diagnostics (caret, fix-it capable, -Werror aware),
// not printf lines.

#include "../p07_tu/Driver.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"

using namespace clang;

namespace {
struct Visitor : RecursiveASTVisitor<Visitor> {
  CompilerInstance &CI;
  ASTContext &Ctx;
  unsigned WarnId, NoteId, ErrId;
  p07::Options O;
  Visitor(CompilerInstance &CI, ASTContext &Ctx, p07::Options O) : CI(CI), Ctx(Ctx), O(O) {
    DiagnosticsEngine &D = CI.getDiagnostics();
    WarnId = D.getCustomDiagID(DiagnosticsEngine::Warning, "'%0' used after move (%1) [p07-move]");
    ErrId = D.getCustomDiagID(DiagnosticsEngine::Warning, "p07-move gave up on '%0': %1");
  }
  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (FD->isImplicit()) return true;
    p07::FnResult R = p07::analyze(FD, Ctx, O);
    DiagnosticsEngine &D = CI.getDiagnostics();
    if (p07::isError(R.St)) {
      D.Report(FD->getLocation(), ErrId) << FD->getName() << R.Message;
      return true;
    }
    for (const p07::MoveDiag &Dg : R.Diags)
      D.Report(Dg.Loc, WarnId) << Dg.Var << (Dg.Certain ? "certain" : "possible");
    return true;
  }
};

struct Consumer : ASTConsumer {
  CompilerInstance &CI;
  p07::Options O;
  Consumer(CompilerInstance &CI, p07::Options O) : CI(CI), O(O) {}
  void HandleTranslationUnit(ASTContext &Ctx) override {
    Visitor(CI, Ctx, O).TraverseDecl(Ctx.getTranslationUnitDecl());
  }
};

struct MoveAction : PluginASTAction {
  p07::Options O;
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef) override {
    return std::make_unique<Consumer>(CI, O);
  }
  // -Xclang -plugin-arg-p07-move -Xclang max-visits=50
  bool ParseArgs(const CompilerInstance &, const std::vector<std::string> &Args) override {
    for (const std::string &A : Args) {
      StringRef S = A;
      if (S.consume_front("max-visits=")) S.getAsInteger(10, O.B.MaxVisits);
      else if (S.consume_front("max-sat=")) S.getAsInteger(10, O.B.MaxSat);
      else return false;
    }
    return true;
  }
  ActionType getActionType() override { return AddAfterMainAction; }
};
} // namespace

static FrontendPluginRegistry::Add<MoveAction> X("p07-move", "use-after-move checker (Part 7)");
