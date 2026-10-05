// p07_tidy -- Part 7.6: the same checker in the shape of a clang-tidy check.
//
// A check is: (1) a ClangTidyCheck subclass, (2) a module that registers it under a name.
// The dataflow analysis itself is untouched: check() hands a FunctionDecl to p07::analyze().

#include "../p07_tu/Driver.h"

#include "clang-tidy/ClangTidyCheck.h"
#include "clang-tidy/ClangTidyModule.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tidy;

namespace {
class UseAfterMoveDataflowCheck : public ClangTidyCheck {
public:
  UseAfterMoveDataflowCheck(StringRef Name, ClangTidyContext *Ctx)
      : ClangTidyCheck(Name, Ctx),
        MaxVisits(Options.get("MaxBlockVisits", (int)clang::dataflow::kDefaultMaxBlockVisits)) {}

  void storeOptions(ClangTidyOptions::OptionMap &Opts) override {
    Options.store(Opts, "MaxBlockVisits", MaxVisits);
  }

  void registerMatchers(MatchFinder *Finder) override {
    // Cheap pre-filter in the matcher, like the Driver's AST scan.
    Finder->addMatcher(functionDecl(isDefinition(), unless(isTemplateInstantiation()),
                                    hasDescendant(callExpr(callee(functionDecl(hasName("::std::move"))))))
                           .bind("fn"),
                       this);
  }

  void check(const MatchFinder::MatchResult &R) override {
    const auto *FD = R.Nodes.getNodeAs<FunctionDecl>("fn");
    p07::Options O;
    O.B.MaxVisits = MaxVisits;
    p07::FnResult Res = p07::analyze(FD, *R.Context, O);
    if (p07::isError(Res.St)) {
      diag(FD->getLocation(), "p07-use-after-move gave up on '%0': %1") << FD->getName() << Res.Message;
      return;
    }
    for (const p07::MoveDiag &D : Res.Diags)
      diag(D.Loc, "'%0' used after move (%1)") << D.Var << (D.Certain ? "certain" : "possible");
  }

private:
  int MaxVisits;
};

class P07Module : public ClangTidyModule {
public:
  void addCheckFactories(ClangTidyCheckFactories &F) override {
    F.registerCheck<UseAfterMoveDataflowCheck>("p07-use-after-move");
  }
};
} // namespace

static ClangTidyModuleRegistry::Add<P07Module> X("p07-module", "Part 7 dataflow checks");
